#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
struct Entry{uint32_t id;uint16_t ratio,reserved;uint32_t first,count;};
struct Tile{uint32_t offset,size;uint16_t tw,th,width,height;float left,top,w,h;};
static_assert(sizeof(Entry)==16&&sizeof(Tile)==32);
int main(int argc,char**argv){
 if(argc!=3)return 2;FILE*in=std::fopen(argv[1],"rb");if(!in)return 2;
 uint32_t header[5];if(std::fread(header,sizeof(header),1,in)!=1||header[0]!=0x52414e44||header[1]!=1)return 3;
 std::vector<Entry> entries(header[2]);std::vector<Tile>tiles(header[3]);
 if(std::fread(entries.data(),sizeof(Entry),entries.size(),in)!=entries.size()||std::fread(tiles.data(),sizeof(Tile),tiles.size(),in)!=tiles.size())return 3;
 std::vector<std::vector<uint8_t>> payloads;uint64_t changed=0,affected=0;
 for(auto&t:tiles){
  std::vector<uint8_t> packed(t.size);std::vector<uint32_t> rgba(unsigned(t.tw)*t.th);uLongf size=rgba.size()*4;
  if(std::fseek(in,t.offset,SEEK_SET)||std::fread(packed.data(),1,packed.size(),in)!=packed.size()||uncompress(reinterpret_cast<uint8_t*>(rgba.data()),&size,packed.data(),packed.size())!=Z_OK||size!=rgba.size()*4)return 4;
  const auto original=rgba;std::vector<uint8_t> distance(rgba.size(),255);std::vector<unsigned> queue;queue.reserve(rgba.size());
  for(unsigned i=0;i<rgba.size();++i)if(rgba[i]>>24){distance[i]=0;queue.push_back(i);}
  for(size_t at=0;at<queue.size();++at){unsigned i=queue[at];if(distance[i]>=2)continue;int x=i%t.tw,y=i/t.tw;
   for(int dir=0;dir<4;++dir){int nx=x+(dir==0?-1:dir==1?1:0),ny=y+(dir==2?-1:dir==3?1:0);if(nx<0||nx>=t.tw||ny<0||ny>=t.th)continue;unsigned j=ny*t.tw+nx;
    if(distance[j]!=255)continue;distance[j]=distance[i]+1;rgba[j]=rgba[i]&0x00ffffffu;queue.push_back(j);
   }
  }
  unsigned count=0;for(unsigned i=0;i<rgba.size();++i){if((rgba[i]>>24)!=(original[i]>>24))return 5;if(original[i]>>24){if(rgba[i]!=original[i])return 5;}else if(rgba[i]!=original[i])++count;}
  changed+=count;affected+=count>0;
  uLongf maximum=compressBound(size);packed.resize(maximum);if(compress2(packed.data(),&maximum,reinterpret_cast<uint8_t*>(rgba.data()),size,6)!=Z_OK)return 4;packed.resize(maximum);t.size=packed.size();payloads.push_back(std::move(packed));
 }
 std::fclose(in);uint32_t offset=20+entries.size()*16+tiles.size()*32;for(auto&t:tiles){t.offset=offset;offset+=t.size;}
 FILE*out=std::fopen(argv[2],"wb");if(!out)return 2;if(std::fwrite(header,sizeof(header),1,out)!=1||std::fwrite(entries.data(),sizeof(Entry),entries.size(),out)!=entries.size()||std::fwrite(tiles.data(),sizeof(Tile),tiles.size(),out)!=tiles.size())return 6;
 for(auto&p:payloads)if(std::fwrite(p.data(),1,p.size(),out)!=p.size())return 6;if(std::fclose(out))return 6;
 std::printf("{\"changed_transparent_rgb_pixels\":%llu,\"affected_source_tiles\":%llu,\"source_tiles\":%zu,\"alpha_unchanged\":true,\"all_nontransparent_rgba_unchanged\":true,\"bleed_radius\":2}\n",(unsigned long long)changed,(unsigned long long)affected,tiles.size());
}
