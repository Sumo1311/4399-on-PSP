#pragma once
#include "art_data.hpp"
#include <array>
#include <vector>
#include <cstdio>
namespace aow {
// PSP-2000 candidate: lossless swizzled RGBA8888 with more decoded residency.
class TextureBank {
public:
 static constexpr unsigned DecodedBudget=16*1024*1024,BlockSize=64*1024,BlockCount=48;
 struct Stats {unsigned decodes=0,reads=0,readBytes=0,prefetches=0,waits=0,flushes=0;};
 bool open(const char*);void close();void beginFrame(){++generation;}
 const unsigned char* get(int,void(*flush)());void prefetch(int);
 bool pin(int);
 unsigned pinnedBytes()const{return pinned;}
 unsigned residentBytes()const{return decodedBytes;}
 const Stats& stats()const{return counters;}
 ~TextureBank(){close();}
private:
 struct Entry {int offset=-1,prev=-1,next=-1;unsigned size=0,generation=0;bool pinned=false;};
 struct Block {int id=-1;unsigned stamp=0,size=0;};
 std::vector<Entry> entries;std::array<Block,BlockCount> blocks{};
 std::array<unsigned char,DecodedBudget/128> nodes{};
 unsigned char *pixels=nullptr,*compressed=nullptr;std::vector<unsigned char> scratch;
 FILE* file=nullptr;int asyncFd=-1,pending=-1,pendingId=-1,head=-1,tail=-1;
 unsigned decodedBytes=0,pinned=0,generation=1,stamp=0;Stats counters{};
 unsigned decodedBudget=DecodedBudget,blockCount=BlockCount;
 int allocate(unsigned,int=1,unsigned=DecodedBudget,unsigned=0);
 void release(unsigned,unsigned,int=1,unsigned=DecodedBudget,unsigned=0);
 void touch(int);void evict(int);void finishPending(bool);
 int findBlock(int)const;int victimBlock()const;
 const unsigned char* block(int);
};
}
