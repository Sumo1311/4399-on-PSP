#pragma once
#include "art_data.hpp"
#include <string>
#include <cstdint>
namespace aow {
class Gfx {
public:
 static void init();static void shutdown();static void begin();static void end();
 static void clip(bool enabled);
 static void rect(float x,float y,float w,float h,uint32_t color);
 static void border(float x,float y,float w,float h,uint32_t color);
 static const ArtAsset* asset(const std::string&);
 static void frame(int frame,float x,float y,float sx=1,float sy=1,float rotation=0,uint32_t color=0xffffffff,bool sharp=false);
 static void sprite(const std::string&,int frame,float x,float y,float sx=1,float sy=1,float rotation=0,uint32_t color=0xffffffff);
 static void text(const std::string&,float x,float y,uint32_t color=0xffffffff,float scale=1,float maxWidth=10000);
 static unsigned long long presentTime();
 static unsigned presentVcount();
 static int cachedBytes();static int misses();static unsigned diskReads();static unsigned diskBytes();static unsigned prefetches();
 static unsigned pinnedBytes();static unsigned submissions();static unsigned listPeakBytes();
};
}
