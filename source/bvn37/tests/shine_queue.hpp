// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_motion.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace bvn::shine_qa {
struct Checks {
    unsigned error=0,scenarios=0,states=0,emitted=0,maxLayers=0,emptied=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned value){for(unsigned b=0;b<4;++b)hash=(hash^((value>>(8*b))&255))*1099511628211ULL;}
    bool run(const std::string& folder,FILE* trace=nullptr){
#define SHINE_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
        FILE* file=std::fopen((folder+"SHINE.IN").c_str(),"r");SHINE_REQUIRE(file);
        unsigned count;SHINE_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==99);
        std::array<unsigned,256> colors{};for(unsigned i=0;i<count;++i)SHINE_REQUIRE(std::fscanf(file,"%x",&colors[i])==1);std::fclose(file);
        constexpr unsigned profiles[][2]{{30,15},{60,30},{60,30},{60,60},{30,30},{30,60},{60,15},{60,0}};
        for(unsigned profile=0;profile<8;++profile)for(unsigned clockMode=0;clockMode<4;++clockMode){
            FightEffects effects;unsigned clock=clockMode==0?0:clockMode==1?0x7fffff00U:clockMode==2?0xffffff00U:1000;
            if(trace)std::fprintf(trace,"R %u\n",scenarios);
            for(unsigned tick=0;tick<300;++tick){
                const unsigned hz=tick>=135&&tick<149?(profiles[profile][0]==30?60:30):profiles[profile][0];
                const unsigned shine=tick>=90&&tick<100?0:profiles[profile][1];
                effects.setProfile(hz,shine);
                const unsigned emit=tick<180?(tick==0?6:tick%11==0?2:tick%7==0?1:0):0;
                if(trace)std::fprintf(trace,"E %u %u %u %u %u",tick,clock,hz,shine,emit);
                for(unsigned value:{tick,clock,hz,shine,emit})word(value);
                FighterEffects request;
                for(unsigned i=0;i<emit;++i){
                    const unsigned color=colors[(scenarios*13+tick*7+i*17)%count],authored=i%3==0;
                    const unsigned percent=authored?(color==0xffffff?30:20):i%3==1?80:15;
                    if(authored)request.addShine(color);else request.addShine(color,percent);
                    if(trace)std::fprintf(trace," %u %u %u",color,percent,authored);
                    for(unsigned value:{color,percent,authored})word(value);
                }
                if(trace)std::fputc('\n',trace);
                emitted+=emit;
                effects.play(std::move(request));
                const auto inspect=[&](unsigned phase){
                    const auto view=effects.scene().shines;
                    if(view.size()>4)return false;
                    maxLayers=std::max(maxLayers,unsigned(view.size()));word(phase);word(unsigned(view.size()));
                    if(trace)std::fprintf(trace,"V %u %u",phase,unsigned(view.size()));
                    for(const auto& layer:view){word(layer.color);word(layer.alpha);if(trace)std::fprintf(trace," %u %u",unsigned(layer.color),layer.alpha);}
                    if(trace)std::fputc('\n',trace);
                    ++states;return true;
                };
                SHINE_REQUIRE(inspect(0));effects.step(false,{},clock);SHINE_REQUIRE(inspect(1));
                clock+=clockMode==3&&tick%19==0?1000:tick%3==0?16:17;
            }
            SHINE_REQUIRE(effects.scene().shines.empty());++emptied;++scenarios;
        }
        SHINE_REQUIRE(scenarios==32&&states==19200&&maxLayers==4&&emptied==32);
        return true;
#undef SHINE_REQUIRE
    }
};
}
