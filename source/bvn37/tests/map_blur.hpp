// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace bvn::blur_qa {
struct Checks {
    unsigned error=0,states=0,banks=0,clips=0,tiles=0,pixels=0,integrated=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(i*8))&255))*1099511628211ULL;}
#define BLUR_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* f=std::fopen((folder+"BLUR.IN").c_str(),"r");BLUR_REQUIRE(f);unsigned rows;
        BLUR_REQUIRE(std::fscanf(f,"%u",&rows)==1&&rows==1920);MapBlurState blur;
        for(unsigned row=0;row<rows;++row){
            unsigned hz,variant,tick,render,animate,action,enabled;int time,frames,slowFrames;double a,b,x,y,rate;
            BLUR_REQUIRE(std::fscanf(f,"%u %u %u %u %u %u %lf %lf %d %lf %lf %d %lf %d %u",
                &hz,&variant,&tick,&render,&animate,&action,&a,&b,&time,&x,&y,&frames,&rate,&slowFrames,&enabled)==15);
            if(!tick){blur=MapBlurState{};blur.gameHz(hz);}
            if(action==1)blur.request(a,b,time);
            if(action==2)blur.slow(a,time);
            if(action==3)blur.enabled(a!=0);
            if(action==4)blur.cancel();
            if(render)blur.render();
            if(animate)blur.animate();
            const auto v=blur.view();
            BLUR_REQUIRE(v.x==x&&v.y==y&&v.frames==frames&&v.slowRate==rate&&v.slowFrames==slowFrames&&v.enabled==bool(enabled));
            word(unsigned(x*8));word(unsigned(y*8));word(unsigned(frames));word(unsigned(rate*2));word(unsigned(slowFrames));word(enabled);++states;
        }
        std::fclose(f);return true;
    }
    bool art(const std::string& folder){
        FILE* f=std::fopen((folder+"BLURART.IN").c_str(),"r");BLUR_REQUIRE(f);unsigned count;BLUR_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==19);
        for(unsigned i=0;i<count;++i){
            char name[64];BLUR_REQUIRE(std::fscanf(f,"%63s",name)==1);FighterAsset asset;
            BLUR_REQUIRE(asset.open((folder+name+".BVB").c_str(),FighterAsset::TextureResidency::LowMemory));
            for(unsigned c=0;c<20;++c){
                unsigned clip,n,bytes;unsigned long long expected;
                BLUR_REQUIRE(std::fscanf(f,"%u %u %u %llx",&clip,&n,&bytes,&expected)==4&&clip==9+c);
                const auto parts=asset.poseTiles(asset.frame(MoveClip(clip),0).pose);BLUR_REQUIRE(parts.size()==n);
                std::uint64_t digest=14695981039346656037ULL;unsigned total=0;
                for(const auto& p:parts){std::vector<std::byte> raw(FighterAsset::textureSize(p));BLUR_REQUIRE(asset.decodePixels(p,raw));
                    for(auto b:raw)digest=(digest^unsigned(b))*1099511628211ULL;
                    total+=raw.size();++tiles;}
                BLUR_REQUIRE(total==bytes&&digest==expected);pixels+=bytes/4;word(unsigned(digest));word(unsigned(digest>>32));++clips;
            }
            ++banks;
        }
        std::fclose(f);return true;
    }
    bool contacts(const std::string& folder){
        FILE* f=std::fopen((folder+"CONTACTBLUR.IN").c_str(),"r");BLUR_REQUIRE(f);unsigned count=0;
        BLUR_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==48);
        for(unsigned row=0;row<count;++row){
            unsigned hz,resume,hitSlow,freezeMs,requests,freezeFrames,paused;double preset,x,y,slow;int frames,slowFrames;
            BLUR_REQUIRE(std::fscanf(f,"%u%lf%u%u%u%lf%lf%lf%d%d%u%u%u",&hz,&preset,&resume,&hitSlow,&freezeMs,&x,&y,&slow,&frames,&slowFrames,&requests,&freezeFrames,&paused)==13);
            MapBlurState model;model.gameHz(hz);
            if(preset)model.slow(preset,1000);if(resume)model.resumeSlow();
            if(hitSlow)model.slow(1.5,hitSlow);model.freeze(freezeMs);
            const auto v=model.view();BLUR_REQUIRE(v.x==x&&v.y==y&&v.slowRate==slow&&v.frames==frames&&v.slowFrames==slowFrames&&model.requests()==requests);
            CombatFreeze hold;hold.request(freezeMs,hz);BLUR_REQUIRE(hold.remaining()==freezeFrames&&hold.paused()==bool(paused));
            for(unsigned value:{unsigned(x*2),unsigned(slow*2),unsigned(frames),unsigned(slowFrames),requests,freezeFrames,paused})word(value);++states;
        }
        std::fclose(f);return true;
    }
    bool match(const std::string& folder){
        FighterAsset asset;StageAsset stage;BLUR_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        BLUR_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        for(unsigned hz:{30U,60U}){
            FightMatch m(asset,asset);m.reset(stage,{350,stage.geometry().playerBottom},{500,stage.geometry().playerBottom});m.setEffectProfile(hz,30);
            FighterStrike hit{};hit.key=17;hit.hitType=3;hit.damage=1;hit.hurtFrames=6;hit.slowTimeMs=500;
            BLUR_REQUIRE(m.actor(1).receiveStrike({hit,1,350,1,-1,{},true,{480,stage.geometry().playerBottom-80,520,stage.geometry().playerBottom}}));
            m.step(stage.geometry(),{},false,Contacts::Locked);BLUR_REQUIRE(m.scene().blur.x==6&&m.scene().blur.slowRate==1.5&&m.scene().blur.frames==15);
            for(unsigned tick=0;tick<50&&m.hitFreezeFrames();++tick)m.step(stage.geometry(),{},false,Contacts::Locked);
            BLUR_REQUIRE(!m.hitFreezeFrames()&&m.scene().blur.slowRate==1.5);
            hit.slowTimeMs=0;
            BLUR_REQUIRE(m.actor(0).receiveStrike({hit,-1,500,1,-1,{},true,{330,stage.geometry().playerBottom-80,370,stage.geometry().playerBottom}}));
            m.step(stage.geometry(),{},false,Contacts::Locked);BLUR_REQUIRE(m.scene().blur.x==6);
            m.flashKnockout(1);BLUR_REQUIRE(m.scene().blur.x==10&&m.scene().blur.frames==60&&!m.scene().blur.enabled);
            for(unsigned tick=0;tick<120;++tick)m.stepScene(tick%2==0);
            BLUR_REQUIRE(m.scene().blur.x==0&&!m.scene().blur.enabled);
            m.resumeKnockout();BLUR_REQUIRE(m.scene().blur.x==4&&m.scene().blur.frames==7&&m.scene().blur.slowRate==2&&m.scene().blur.enabled);
            word(hz);++integrated;
        }
        return true;
    }
    bool run(const std::string& folder){return reference(folder)&&contacts(folder)&&art(folder)&&match(folder);}
#undef BLUR_REQUIRE
};
} // namespace bvn::blur_qa
