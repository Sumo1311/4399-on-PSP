// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_animation_clock.hpp"
#include <cstdio>
#include <cstdint>
#include <string>
namespace bvn::game_clock_qa {
struct Checks {
    unsigned error=0,rows=0,actorFrames=0,effectFrames=0,splitFrames=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(8*i))&255))*1099511628211ull;}
    bool run(const std::string& path){
#define REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
        FILE* input=std::fopen(path.c_str(),"r");REQUIRE(input);unsigned count=0;
        REQUIRE(std::fscanf(input,"%u",&count)==1&&count==900);GameAnimationClocks clocks;
        for(unsigned i=0;i<count;++i){
            unsigned fps,scenario,tick,action,manual,logical,actor,effect;int milliseconds,actorGap,actorPhase,effectGap,effectPhase,slowFrames;double rate,slowRate;
            REQUIRE(std::fscanf(input,"%u%u%u%u%lf%d%u%u%u%u%d%d%d%d%lf%d",&fps,&scenario,&tick,&action,&rate,&milliseconds,&manual,&logical,&actor,&effect,&actorGap,&actorPhase,&effectGap,&effectPhase,&slowRate,&slowFrames)==16);
            if(!tick)clocks.initialize(fps);
            if(action==1)clocks.slow(rate,milliseconds);if(action==2)clocks.resume();
            const auto step=clocks.render(logical,manual);
            REQUIRE(step.actor==bool(actor)&&step.effect==bool(effect));
            REQUIRE(clocks.actor().gap()==actorGap&&clocks.actor().frame()==actorPhase);
            REQUIRE(clocks.effect().gap()==effectGap&&clocks.effect().frame()==effectPhase);
            REQUIRE(clocks.slowRate()==slowRate&&clocks.slowFrames()==slowFrames);
            for(unsigned value:{actor,effect,unsigned(actorGap),unsigned(actorPhase),unsigned(effectGap),unsigned(effectPhase),unsigned(slowRate*4),unsigned(slowFrames)})word(value);
            ++rows;actorFrames+=actor;effectFrames+=effect;splitFrames+=actor!=effect;
        }
        std::fclose(input);return true;
#undef REQUIRE
    }
};
} // namespace bvn::game_clock_qa
