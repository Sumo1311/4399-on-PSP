// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>
namespace bvn {
class TransitionAsset {
public:
    bool open(const char* path);
    const void* pixels(unsigned frame);
    unsigned width()const{return width_;}unsigned height()const{return height_;}
    unsigned stride()const{return stride_;}unsigned rows()const{return rows_;}
    unsigned decodedFrame()const{return decoded_;}
    std::size_t residentBytes()const{return pixels_.size()*16+compressed_.size();}
    unsigned decodedCrc()const{return crc_;}
private:
    struct Entry{std::uint32_t frame,offset,bytes,crc;};
    struct alignas(16) Block{std::array<std::byte,16> bytes;};
    std::vector<Entry> entries_;
    std::vector<std::byte> compressed_;
    std::vector<Block> pixels_;
    std::unique_ptr<FILE,decltype(&std::fclose)> file_{nullptr,std::fclose};
    unsigned pixelStart_=0;
    unsigned width_=0,height_=0,stride_=0,rows_=0,decoded_=~0u,crc_=0;
};
}
