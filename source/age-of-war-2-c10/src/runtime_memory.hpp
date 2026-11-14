#pragma once
extern "C" unsigned boot10_heap_capacity(void);
namespace aow {
inline bool largeMemory(){return boot10_heap_capacity()>=36u*1024*1024;}
inline unsigned textureBudget(){return largeMemory()?16u*1024*1024:boot10_heap_capacity()>=17920u*1024?8u*1024*1024:4u*1024*1024;}
inline unsigned compressedBlocks(){return largeMemory()?48u:8u;}
}
