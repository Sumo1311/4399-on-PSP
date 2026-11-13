// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "map_optical.hpp"
#include "stage_asset.hpp"
#include "fighter_asset.hpp"
#include "fight_effects.hpp"
#include "fight_match.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace bvn::optical_qa {
struct Checks {
    unsigned error=0,states=0,half=0,maps=0,variants=0,palette=0,pixels=0,lookups=0,captures=0,objectBanks=0,objectAreas=0,integrated=0;
    std::uint64_t hash=14695981039346656037ull;
    void bytes(std::span<const std::byte> input){for(auto b:input){hash^=std::to_integer<unsigned>(b);hash*=1099511628211ull;}}
    void number(unsigned value){bytes(std::as_bytes(std::span(&value,1)));}
    bool run(const std::string& prefix){
#define CHECK(c) do{if(!(c)){error=__LINE__;return false;}}while(false)
        const auto file=[](const std::string& path){return std::unique_ptr<FILE,decltype(&std::fclose)>(std::fopen(path.c_str(),"r"),std::fclose);};
        auto input=file(prefix+"OPTICAL.IN");CHECK(input);unsigned count=0;CHECK(std::fscanf(input.get(),"%u",&count)==1&&count==5);
        for(unsigned i=0;i<count;++i){
            char name[32];unsigned children=0,rows=0;CHECK(std::fscanf(input.get(),"%31s%u%u",name,&children,&rows)==3&&children<=12&&rows==640);
            std::vector<OpticalRect> bounds(children);
            for(auto& b:bounds)CHECK(std::fscanf(input.get(),"%lf%lf%lf%lf",&b.x,&b.y,&b.width,&b.height)==4);
            MapOpticalState model;model.initialize(bounds);
            for(unsigned row=0;row<rows;++row){
                unsigned tick=0,width=0,n=0,expectedMask=0,expectedCounter=0,expectedCaptures=0;Point camera;
                CHECK(std::fscanf(input.get(),"%u%lf%lf%u%u",&tick,&camera.x,&camera.y,&width,&n)==5&&tick==row&&n<=3);
                std::vector<OpticalSprite> sprites(n);
                for(auto& s:sprites){unsigned valid=0,destroyed=0;auto& b=s.area;CHECK(std::fscanf(input.get(),"%u%u%lf%lf%lf%lf",&valid,&destroyed,&b.x,&b.y,&b.width,&b.height)==6);s.valid=valid;s.destroyed=destroyed;}
                if(width)model.capture(width,0);
                model.advance(camera,sprites);
                CHECK(std::fscanf(input.get(),"%u%u",&expectedMask,&expectedCounter)==2);
                std::array<unsigned,5> cached;for(auto& value:cached)CHECK(std::fscanf(input.get(),"%u",&value)==1);
                CHECK(std::fscanf(input.get(),"%u",&expectedCaptures)==1);
                const auto view=model.view();CHECK(view.active&&view.mask==expectedMask&&view.counter==expectedCounter&&view.cached==cached&&view.captures==expectedCaptures);
                number(view.mask);number(view.counter);for(auto value:view.cached)number(value);number(view.captures);++states;half+=view.mask!=0;
            }
            captures+=model.view().captures;
        }
        input=file(prefix+"OPTICALART.IN");CHECK(input);CHECK(std::fscanf(input.get(),"%u",&count)==1&&count==5);
        for(unsigned i=0;i<count;++i){
            char name[32];unsigned frames=0,masks=0;std::array<unsigned,6> tileCounts{};
            CHECK(std::fscanf(input.get(),"%31s%u%u",name,&frames,&masks)==3);
            for(auto& n:tileCounts)CHECK(std::fscanf(input.get(),"%u",&n)==1);
            StageAsset stage;CHECK(stage.open((prefix+name+".BVN").c_str()));const auto& info=stage.optical();
            CHECK(info.paletteFrames()==frames&&(1u<<info.children().size())==masks);
            for(unsigned kind=0;kind<6;++kind){const auto tiles=info.tiles(kind);CHECK(tiles.size()==tileCounts[kind]);for(const auto& tile:tiles){double x=0,y=0;CHECK(std::fscanf(input.get(),"%lf%lf",&x,&y)==2&&tile.x==float(x)&&tile.y==float(y));}}
            FighterAsset art;CHECK(art.open((prefix+name+".BVO").c_str(),FighterAsset::TextureResidency::LowMemory));CHECK(art.clip(MoveClip(9)).count==frames);
            for(unsigned frame=0;frame<frames;++frame){
                unsigned w=0,h=0,stride=0,rows=0,size=0;unsigned long long expected=0;
                CHECK(std::fscanf(input.get(),"%u%u%u%u%u%llx",&w,&h,&stride,&rows,&size,&expected)==6);
                const auto pose=art.frame(MoveClip(9),frame).pose;CHECK(pose.width==w&&pose.height==h&&pose.stride==stride&&pose.rows==rows&&size==FighterAsset::textureSize(pose));
                std::vector<std::byte> raw(size);CHECK(art.decodePixels(pose,raw));const auto saved=hash;hash=14695981039346656037ull;bytes(raw);CHECK(hash==expected);hash=saved;number(unsigned(expected));number(unsigned(expected>>32));++palette;pixels+=stride*rows;
            }
            unsigned long long expected=0;CHECK(std::fscanf(input.get(),"%llx",&expected)==1);
            const auto saved=hash;hash=14695981039346656037ull;
            for(unsigned mask=0;mask<masks;++mask)for(unsigned kind=0;kind<6;++kind){for(const auto& tile:info.tiles(kind)){number(info.texture(tile,mask));++lookups;}++variants;}
            CHECK(hash==expected);hash=saved;number(unsigned(expected));number(unsigned(expected>>32));++maps;
            const auto originalBytes=stage.residentTextureBytes();std::size_t removed=0;
            std::uint64_t before=14695981039346656037ull,after=before;
            const auto digest=[](std::uint64_t& h,const StageAsset& a,const StageTile& t){
                const auto* p=static_cast<const unsigned char*>(a.pixels(t));for(unsigned n=0;n<t.bytes;++n)h=(h^p[n])*1099511628211ull;
            };
            for(const auto& t:stage.tiles())if(t.layer==StageLayer::Front)removed+=t.bytes;else digest(before,stage,t);
            stage.useOpticalPalette();CHECK(removed&&stage.residentTextureBytes()==originalBytes-removed);
            for(const auto& t:stage.tiles()){CHECK(t.layer!=StageLayer::Front);digest(after,stage,t);}
            CHECK(before==after);stage.useOpticalPalette();CHECK(stage.residentTextureBytes()==originalBytes-removed);
        }
        input=file(prefix+"OBJECTAREAS.IN");CHECK(input);CHECK(std::fscanf(input.get(),"%u",&count)==1&&count==131);
        for(unsigned i=0;i<count;++i){
            char name[32];unsigned n=0;unsigned long long identity=0;
            CHECK(std::fscanf(input.get(),"%31s%llx%u",name,&identity,&n)==3);
            FighterAsset asset;CHECK(asset.open((prefix+name).c_str(),FighterAsset::TextureResidency::LowMemory));
            CHECK(asset.metadataIdentity()==identity&&asset.hasObjectAreas()&&asset.attackerCount()==n);
            number(unsigned(identity));number(unsigned(identity>>32));
            for(unsigned j=0;j<n;++j){
                OpticalRect expected;CHECK(std::fscanf(input.get(),"%lf%lf%lf%lf",&expected.x,&expected.y,&expected.width,&expected.height)==4);
                const auto rect=asset.objectArea(j);CHECK(rect&&rect->x==expected.x&&rect->y==expected.y&&rect->width==expected.width&&rect->height==expected.height);
                bytes(std::as_bytes(std::span(&expected,1)));++objectAreas;
            }
            CHECK(!asset.objectArea(n));++objectBanks;
        }
        // The accepted-blur observer captures optical alpha immediately and reuses it.
        const std::array<OpticalRect,1> bounds{{{0,0,10,10}}};FightEffects effects;auto& state=effects.opticalState();state.initialize(bounds);
        const std::array<OpticalSprite,1> sprites{{{{1,1,2,2},true,false}}};state.advance({0,600},sprites);state.advance({0,600},sprites);
        effects.blur(4,0,500);CHECK(state.view().cached[1]==1);
        const std::array<OpticalSprite,1> away{{{{100,100,2,2},true,false}}};state.advance({0,600},away);state.advance({0,600},away);
        effects.blur(4,0,500);CHECK(!state.view().mask&&state.view().cached[1]==1&&state.view().captures==1);
        FighterAsset actor;CHECK(actor.open((prefix+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        // A shared area is retained until a source body query refreshes it.
        FighterMotion a(actor),b(actor);a.reset({350,315.05},1);b.reset({400,315.05},-1);
        const auto first=a.opticalArea();a.place({700,315.05});b.place({1200,315.05});
        a.resolveBodyContact(b);CHECK(a.opticalArea().left==first.left);
        (void)a.strikeAgainst(b);(void)b.strikeAgainst(a);
        CHECK(a.opticalArea().left==worldBox(actor.frame(MoveClip::Idle,0).body,a.motion().position,a.facing()).left);
        a.place({800,315.05});b.place({1500,315.05});
        const auto previous=a.opticalArea();(void)a.strikeAgainst(b);(void)b.strikeAgainst(a);
        CHECK(a.opticalArea().left==previous.left);
        b.place({850,315.05});a.resolveBodyContact(b);
        CHECK(a.opticalArea().left==worldBox(actor.frame(MoveClip::Idle,0).body,a.motion().position,a.facing()).left);
        a.reset({900,315.05},1);CHECK(a.opticalArea().left!=worldBox(actor.frame(MoveClip::Idle,0).body,a.motion().position,a.facing()).left);
        ++integrated;number(1);
        StageAsset stage,next;CHECK(stage.open((prefix+"SENLIN.BVN").c_str())&&next.open((prefix+"SENLIN.BVN").c_str()));
        for(unsigned hz:{30U,60U}){
            FightMatch match(actor,actor);match.setEffectProfile(hz,30);match.reset(stage,{390,315.05},{435,315.05});
            for(unsigned tick=0;tick<8;++tick)match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
            CHECK(match.scene().optical.mask==1&&match.scene().optical.cached[2]==OpticalView::Missing);
            FighterStrike hit{};hit.key=170;hit.hitType=3;hit.damage=1;hit.hurtFrames=40;hit.slowTimeMs=500;
            CHECK(match.actor(1).receiveStrike({hit,1,390,1,-1,{},true,{420,270,450,310}}));
            match.step(stage.geometry(),{},true,Contacts::Locked);
            CHECK(match.scene().blur.x==6&&match.scene().optical.cached[2]==1&&match.scene().optical.captures==2);
            match.reset(stage,{700,315.05},{745,315.05});
            for(unsigned tick=0;tick<8;++tick)match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
            CHECK(!match.scene().optical.mask&&match.scene().optical.cached[2]==1&&match.scene().optical.captures==2);
            CHECK(match.actor(1).receiveStrike({hit,1,700,1,-1,{},true,{730,270,760,310}}));
            match.step(stage.geometry(),{},true,Contacts::Locked);
            CHECK(match.scene().blur.x==6&&match.scene().optical.cached[2]==1&&match.scene().optical.captures==2);
            match.reset(next,{700,315.05},{745,315.05});
            CHECK(match.scene().optical.cached[2]==OpticalView::Missing&&!match.scene().optical.captures);
            ++integrated;number(hz);
        }
        return true;
#undef CHECK
    }
};
} // namespace bvn::optical_qa
