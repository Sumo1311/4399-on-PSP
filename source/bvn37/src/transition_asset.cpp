// SPDX-License-Identifier: GPL-3.0-or-later
#include "transition_asset.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <zlib.h>
namespace bvn {
bool TransitionAsset::open(const char* path){
    struct Header{char magic[8];std::uint32_t count;std::uint16_t width,height,stride,rows;std::uint32_t pixels,length,reserved;};
    static_assert(sizeof(Header)==32&&sizeof(Entry)==16);
    std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(path,"rb"),std::fclose);
    if(!file||std::fseek(file.get(),0,SEEK_END))return false;
    const long length=std::ftell(file.get());std::rewind(file.get());Header h{};
    if(std::fread(&h,sizeof h,1,file.get())!=1||std::memcmp(h.magic,"BVNTRA01",8)||h.reserved||!h.count||h.count>64||
       !h.width||h.width>480||h.height!=272||!std::has_single_bit(h.stride)||!std::has_single_bit(h.rows)||h.stride<h.width||h.stride>512||h.rows<h.height||h.rows>512||
       h.pixels<sizeof h+h.count*sizeof(Entry)||h.pixels%16||length!=long(h.length)||h.length<=h.pixels||h.length-h.pixels>2*1024*1024)return false;
    TransitionAsset next;next.entries_.resize(h.count);
    if(std::fread(next.entries_.data(),sizeof(Entry),h.count,file.get())!=h.count)return false;
    unsigned prior=~0u;
    for(const auto& e:next.entries_){if((prior!=~0u&&e.frame<=prior)||e.frame>69||!e.bytes||e.offset>h.length-h.pixels||e.bytes>h.length-h.pixels-e.offset)return false;prior=e.frame;}
    const auto largest=std::max_element(next.entries_.begin(),next.entries_.end(),[](const Entry& a,const Entry& b){return a.bytes<b.bytes;})->bytes;
    next.compressed_.resize(largest);next.pixels_.resize(std::size_t(h.stride)*h.rows/16);
    next.file_=std::move(file);next.pixelStart_=h.pixels;
    next.width_=h.width;next.height_=h.height;next.stride_=h.stride;next.rows_=h.rows;*this=std::move(next);return true;
}
const void* TransitionAsset::pixels(unsigned frame){
    if(frame==decoded_)return pixels_.data();
    const auto it=std::lower_bound(entries_.begin(),entries_.end(),frame,[](const Entry& e,unsigned f){return e.frame<f;});
    if(it==entries_.end()||it->frame!=frame)return nullptr;
    decoded_=~0u;
    if(std::fseek(file_.get(),pixelStart_+it->offset,SEEK_SET)||std::fread(compressed_.data(),1,it->bytes,file_.get())!=it->bytes)return nullptr;
    uLongf count=pixels_.size()*16;
    if(uncompress(reinterpret_cast<Bytef*>(pixels_.data()),&count,reinterpret_cast<const Bytef*>(compressed_.data()),it->bytes)!=Z_OK||count!=pixels_.size()*16)return nullptr;
    const auto crc=crc32(0,reinterpret_cast<const Bytef*>(pixels_.data()),count);if(crc!=it->crc)return nullptr;
    decoded_=frame;crc_=crc;return pixels_.data();
}
}
