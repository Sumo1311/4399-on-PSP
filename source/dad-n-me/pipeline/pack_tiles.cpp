#include "../vendor/lz4/lz4.h"
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

struct Entry { uint32_t id; uint16_t ratio, reserved; uint32_t first, count; };
struct SourceTile { uint32_t offset,size; uint16_t tw,th,width,height; float left,top,w,h; };
struct Tile { SourceTile t; uint32_t format; };
static_assert(sizeof(Entry)==16 && sizeof(SourceTile)==32 && sizeof(Tile)==36);
unsigned power2(unsigned v) { unsigned n=1; while(n<v)n*=2; return n; }
int main(int argc,char**argv) {
    if(argc!=4)return 2;
    FILE* in=std::fopen(argv[1],"rb"); if(!in)return 2;
    uint32_t header[5];
    if(std::fread(header,sizeof(header),1,in)!=1||header[0]!=0x52414e44||header[1]!=1)return 3;
    std::vector<Entry> entries(header[2]); std::vector<SourceTile> original(header[3]);
    if(std::fread(entries.data(),sizeof(Entry),entries.size(),in)!=entries.size() ||
       std::fread(original.data(),sizeof(SourceTile),original.size(),in)!=original.size())return 3;
    std::vector<Tile> tiles; std::vector<std::vector<char>> payloads;
    FILE* mapping=std::fopen(argv[3],"w");if(!mapping)return 2;
    std::fputs("tile,parent,x,y,width,height,gutter\n",mapping);
    uint64_t source_bytes=0,storage_bytes=0,pixel_bytes=0;unsigned palette_tiles=0,split_parents=0;
    for(auto& entry:entries) {
        unsigned first=entry.first,count=entry.count;entry.first=tiles.size();
        for(unsigned parent=first;parent<first+count;++parent) {
            if(parent>=original.size())return 3;
            const auto& old=original[parent];
            if(!old.width||!old.height||old.width>old.tw||old.height>old.th||old.tw>512||old.th>512)return 3;
            std::vector<char> packed(old.size);std::vector<uint32_t> rgba(unsigned(old.tw)*old.th);
            uLongf decoded=rgba.size()*4;
            if(std::fseek(in,old.offset,SEEK_SET)||std::fread(packed.data(),1,packed.size(),in)!=packed.size() ||
               uncompress(reinterpret_cast<uint8_t*>(rgba.data()),&decoded,reinterpret_cast<const uint8_t*>(packed.data()),packed.size())!=Z_OK||decoded!=rgba.size()*4)return 4;
            source_bytes+=decoded;
            bool split=old.tw>128||old.th>128;unsigned step=split?126:512;if(split)++split_parents;
            for(unsigned y=0;y<old.height;y+=step)for(unsigned x=0;x<old.width;x+=step) {
                unsigned w=std::min(step,unsigned(old.width)-x),h=std::min(step,unsigned(old.height)-y),g=split?1:0;
                unsigned tw=split?std::max(8u,power2(w+2)):old.tw,th=split?std::max(4u,power2(h+2)):old.th;
                std::vector<uint32_t> pixels(tw*th);
                for(unsigned py=0;py<th;++py)for(unsigned px=0;px<tw;++px) {
                    int sx=split?std::clamp(int(x+px)-1,0,int(old.tw)-1):int(px);
                    int sy=split?std::clamp(int(y+py)-1,0,int(old.th)-1):int(py);
                    pixels[py*tw+px]=rgba[sy*old.tw+sx];
                }
                std::vector<uint8_t> storage(pixels.size()*4);std::memcpy(storage.data(),pixels.data(),storage.size());
                uint32_t format=2|(g?4:0);std::unordered_map<uint32_t,uint8_t> colors;std::vector<uint32_t> palette;
                for(auto p:pixels)if(!colors.count(p)) { if(palette.size()==256){palette.clear();break;} colors.emplace(p,uint8_t(palette.size()));palette.push_back(p); }
                size_t aligned=(pixels.size()+63)&~size_t(63);
                if(!palette.empty()&&tw>=16&&aligned+1024<storage.size()) {
                    storage.assign(aligned+1024,0);
                    for(size_t i=0;i<pixels.size();++i)storage[i]=colors[pixels[i]];
                    std::memcpy(storage.data()+aligned,palette.data(),palette.size()*4);
                    // Reverse mapping checks every channel, including transparent RGB.
                    for(size_t i=0;i<pixels.size();++i){uint32_t p;std::memcpy(&p,storage.data()+aligned+storage[i]*4,4);if(p!=pixels[i])return 5;}
                    format|=1;++palette_tiles;
                }
                std::vector<char> encoded(LZ4_compressBound(storage.size()));
                int n=LZ4_compress_default(reinterpret_cast<const char*>(storage.data()),encoded.data(),storage.size(),encoded.size());
                std::vector<uint8_t> verified(storage.size());
                if(n<=0||LZ4_decompress_safe(encoded.data(),reinterpret_cast<char*>(verified.data()),n,verified.size())!=int(verified.size())||verified!=storage)return 6;
                encoded.resize(n);Tile t{{0,uint32_t(n),uint16_t(tw),uint16_t(th),uint16_t(w),uint16_t(h),old.left,old.top,old.w,old.h},format};
                if(split) {
                    float right=x+w==old.width?old.left+old.w:old.left+old.w*(float(x+w)/old.width);
                    float bottom=y+h==old.height?old.top+old.h:old.top+old.h*(float(y+h)/old.height);
                    t.t.left=old.left+old.w*(float(x)/old.width);t.t.top=old.top+old.h*(float(y)/old.height);
                    t.t.w=right-t.t.left;t.t.h=bottom-t.t.top;
                }
                std::fprintf(mapping,"%zu,%u,%u,%u,%u,%u,%u\n",tiles.size(),parent,x,y,w,h,g);
                storage_bytes+=storage.size();pixel_bytes+=pixels.size()*4;tiles.push_back(t);payloads.push_back(std::move(encoded));
            }
        }
        entry.count=tiles.size()-entry.first;
    }
    std::fclose(in);if(std::fclose(mapping))return 6;
    if(tiles.size()>20000)return 7;
    uint32_t offset=20+entries.size()*16+tiles.size()*36;
    for(auto& tile:tiles){tile.t.offset=offset;offset+=tile.t.size;}
    FILE* out=std::fopen(argv[2],"wb");if(!out)return 2;header[1]=4;header[3]=tiles.size();
    if(std::fwrite(header,sizeof(header),1,out)!=1||std::fwrite(entries.data(),sizeof(Entry),entries.size(),out)!=entries.size() ||
       std::fwrite(tiles.data(),sizeof(Tile),tiles.size(),out)!=tiles.size())return 6;
    for(const auto& p:payloads)if(std::fwrite(p.data(),1,p.size(),out)!=p.size())return 6;
    if(std::fclose(out))return 6;
    std::printf("{\"source_tiles\":%zu,\"split_parents\":%u,\"output_tiles\":%zu,\"paletted_tiles\":%u,\"source_storage_bytes\":%llu,\"output_storage_bytes\":%llu,\"output_rgba_bytes\":%llu,\"bank_bytes\":%u,\"maximum_edge\":128,\"gutter\":1,\"pixel_recode\":\"exact\"}\n",original.size(),split_parents,tiles.size(),palette_tiles,(unsigned long long)source_bytes,(unsigned long long)storage_bytes,(unsigned long long)pixel_bytes,offset);
}
