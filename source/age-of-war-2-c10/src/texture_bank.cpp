#include "texture_bank.hpp"
#include "runtime_memory.hpp"
#include <lz4.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unistd.h>
#include <string>
#ifdef __PSP__
#include <pspiofilemgr.h>
#include <pspkernel.h>
#endif
namespace aow {
namespace {unsigned char* aligned(unsigned n){
#ifdef __PSP__
 return (unsigned char*)memalign(64,n);
#else
 void* p=nullptr;return posix_memalign(&p,64,n)==0?(unsigned char*)p:nullptr;
#endif
}}
int TextureBank::allocate(unsigned needed,int node,unsigned size,unsigned offset){
 if(nodes[node]==2)return -1;
 if(size==needed){if(nodes[node])return -1;nodes[node]=2;return int(offset);}
 int result=allocate(needed,node*2,size/2,offset);
 if(result<0)result=allocate(needed,node*2+1,size/2,offset+size/2);
 nodes[node]=nodes[node*2]==2&&nodes[node*2+1]==2?2:1;
 return result;
}
void TextureBank::release(unsigned offset,unsigned needed,int node,unsigned size,unsigned base){
 if(size==needed){nodes[node]=0;return;}
 if(offset<base+size/2)release(offset,needed,node*2,size/2,base);
 else release(offset,needed,node*2+1,size/2,base+size/2);
 auto a=nodes[node*2],b=nodes[node*2+1];nodes[node]=a==0&&b==0?0:a==2&&b==2?2:1;
}
void TextureBank::touch(int id){
 auto& e=entries[id];if(head==id)return;
 if(e.prev>=0)entries[e.prev].next=e.next;if(e.next>=0)entries[e.next].prev=e.prev;
 if(tail==id)tail=e.prev;e.prev=-1;e.next=head;
 if(head>=0)entries[head].prev=id;head=id;if(tail<0)tail=id;
}
void TextureBank::evict(int id){
 auto& e=entries[id];release(e.offset,e.size,1,decodedBudget);decodedBytes-=e.size;
 if(e.prev>=0)entries[e.prev].next=e.next;else head=e.next;
 if(e.next>=0)entries[e.next].prev=e.prev;else tail=e.prev;e=Entry{};
}
bool TextureBank::open(const char* path){
 close();char cwd[1024];std::string full;
 if(path[0]!='/'&&!std::strchr(path,':')&&getcwd(cwd,sizeof(cwd))){full=std::string(cwd)+"/"+path;path=full.c_str();}
 file=std::fopen(path,"rb");if(!file)return false;std::setvbuf(file,nullptr,_IONBF,0);
 decodedBudget=textureBudget();blockCount=compressedBlocks();
 pixels=aligned(decodedBudget);compressed=aligned(BlockSize*blockCount);
 if(!pixels||!compressed){close();return false;}
 entries.resize(ART_TEXTURE_COUNT);unsigned maxSize=0;
 for(int i=0;i<ART_TEXTURE_COUNT;i++)maxSize=std::max(maxSize,unsigned(artTextures[i].compressed));
 scratch.resize(maxSize);
#ifdef __PSP__
 asyncFd=sceIoOpen(path,PSP_O_RDONLY,0);
 if(asyncFd>=0)sceIoChangeAsyncPriority(asyncFd,0x30);
#endif
 return true;
}
void TextureBank::finishPending(bool wait){
#ifdef __PSP__
 if(pending<0)return;SceInt64 result=0;
 int status=wait?sceIoWaitAsync(asyncFd,&result):sceIoPollAsync(asyncFd,&result);
 if(status==1)return;
 if(wait)++counters.waits;
 auto& b=blocks[pending];b.id=status==0&&result>0?pendingId:-1;b.size=result>0?unsigned(result):0;b.stamp=++stamp;
 if(b.id>=0){++counters.reads;counters.readBytes+=b.size;}
 pending=pendingId=-1;
#else
 (void)wait;
#endif
}
int TextureBank::findBlock(int id)const{for(unsigned i=0;i<blockCount;i++)if(blocks[i].id==id)return int(i);return -1;}
int TextureBank::victimBlock()const{
 int victim=-1;for(unsigned i=0;i<blockCount;i++)if(int(i)!=pending&&(victim<0||blocks[i].stamp<blocks[victim].stamp))victim=int(i);return victim;
}
const unsigned char* TextureBank::block(int id){
 finishPending(false);int slot=findBlock(id);
 if(slot<0&&pendingId==id){finishPending(true);slot=findBlock(id);}
 if(slot<0){
  slot=victimBlock();auto& b=blocks[slot];std::fseek(file,long(id)*BlockSize,SEEK_SET);
  b.size=std::fread(compressed+slot*BlockSize,1,BlockSize,file);b.id=b.size?id:-1;
  ++counters.reads;counters.readBytes+=b.size;if(!b.size)return nullptr;
 }
 blocks[slot].stamp=++stamp;return compressed+slot*BlockSize;
}
void TextureBank::prefetch(int texture){
#ifdef __PSP__
 if(asyncFd<0||texture<0||texture>=ART_TEXTURE_COUNT)return;finishPending(false);
 int id=artTextures[texture].offset/BlockSize;
 if(pending>=0||findBlock(id)>=0)return;
 int slot=victimBlock();blocks[slot].id=-1;
 if(sceIoLseek32(asyncFd,id*BlockSize,PSP_SEEK_SET)<0)return;
 if(sceIoReadAsync(asyncFd,compressed+slot*BlockSize,BlockSize)<0)return;
 pending=slot;pendingId=id;++counters.prefetches;
#else
 (void)texture;
#endif
}
const unsigned char* TextureBank::get(int id,void(*flush)()){
 if(!pixels||id<0||id>=ART_TEXTURE_COUNT)return nullptr;auto& e=entries[id];
 if(e.offset>=0){touch(id);e.generation=generation;return pixels+e.offset;}
 const auto& t=artTextures[id];unsigned bytes=unsigned(t.width)*t.height*4,needed=std::max(256u,bytes);
 if(needed>decodedBudget)return nullptr;
 int offset=allocate(needed,1,decodedBudget);
 while(offset<0){
  int victim=tail;while(victim>=0&&(entries[victim].pinned||entries[victim].generation==generation))victim=entries[victim].prev;
  if(victim<0){if(!flush)return nullptr;flush();++generation;++counters.flushes;continue;}
  evict(victim);offset=allocate(needed,1,decodedBudget);
 }
 const unsigned char* source=nullptr;unsigned blockId=t.offset/BlockSize,within=t.offset%BlockSize;
 if(within+t.compressed<=BlockSize){auto* p=block(blockId);if(p)source=p+within;}
 else {
  unsigned copied=0;
  while(copied<t.compressed){unsigned pos=t.offset+copied;auto* p=block(pos/BlockSize);if(!p)break;unsigned n=std::min(unsigned(t.compressed)-copied,BlockSize-pos%BlockSize);std::memcpy(scratch.data()+copied,p+pos%BlockSize,n);copied+=n;}
  if(copied==t.compressed)source=scratch.data();
 }
 if(!source||LZ4_decompress_safe((const char*)source,(char*)pixels+offset,t.compressed,bytes)!=int(bytes)){release(offset,needed,1,decodedBudget);return nullptr;}
 e.offset=offset;e.size=needed;e.generation=generation;decodedBytes+=needed;touch(id);++counters.decodes;
#ifdef __PSP__
 sceKernelDcacheWritebackRange(pixels+offset,bytes);
#endif
 return pixels+offset;
}
bool TextureBank::pin(int id){
 if(id<0||id>=ART_TEXTURE_COUNT)return false;
 auto& e=entries[id];if(e.pinned)return true;
 const auto& t=artTextures[id];unsigned bytes=std::max(256u,unsigned(t.width)*t.height*4);
 // UI plus all eras' static backgrounds/bases occupy the same16MiB pool.
 // Half-density backgrounds leave at least6MiB for animated scene textures.
 if(pinned+bytes>(largeMemory()?10*1024*1024:2*1024*1024))return false;
 if(!get(id,nullptr))return false;
 e.pinned=true;pinned+=e.size;return true;
}
void TextureBank::close(){
 finishPending(true);
#ifdef __PSP__
 if(asyncFd>=0)sceIoClose(asyncFd);
#endif
 asyncFd=-1;if(file)std::fclose(file);file=nullptr;std::free(pixels);std::free(compressed);pixels=compressed=nullptr;
 entries.clear();scratch.clear();nodes.fill(0);for(auto& b:blocks)b=Block{};head=tail=-1;decodedBytes=pinned=0;counters=Stats{};generation=1;stamp=0;
}
}
