// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"
#include "roster_asset.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __PSP__
#include "psp_stage_renderer.hpp"
#include "psp_allocation_recovery.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Full Asset QA",PSP_MODULE_USER,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(-1024);
extern "C" {
volatile struct AssetCoverageQA {unsigned status,error,row,textures,poses,commands,steps,command,hz,tick,draws;}g_assetCoverageQa;
extern volatile struct PaintRendererQA {unsigned frames,players,roots,missing,hashLow,hashHigh,sprites,combat,ground,reactions,shadows,blackBack,backgrounds;}g_paintRendererQa;
extern volatile struct TextureCacheQA {unsigned budget,allocation,decode,requested,resident,locked,retryAllocation,retryDecode,evicted;}g_textureCacheQa;
}
#endif

using namespace bvn;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Counts {unsigned textures=0,poses=0,commands=0,steps=0,objects=0,assists=0;std::uint64_t bytes=0,hash=14695981039346656037ull;};
#ifdef __PSP__
static StageRenderer renderer;
static UiAsset cueArt;
static FighterAsset reactionArt,combatArt;
const FighterAsset* mapBlurArt=nullptr;
const FighterAsset* mapOpticalArt=nullptr;
std::vector<unsigned> capturePixels;
std::FILE* pixelOutput=nullptr;
void renderMatch(const FightMatch& match,const StageAsset& stage){
    std::vector<FighterView> views;
    for(auto key:match.sourceScene().paint().players())if(SourcePaint::kind(key)==SourcePaint::Kind::Sprite){
        const auto id=SourcePaint::id(key);
        if(!(id&SourceScene::Object)){
            if(id<2){const auto& actor=match.actor(id);views.push_back({actor.asset(),actor.pose(),actor.motion().position,actor.facing(),actor.screenPose(),actor.opacity(),actor.visualTint(),key,!match.hidesTarget(1-id)});}
        }else{
            const auto at=match.sourceScene().storage(id);if(at>=match.objects().size())continue;
            std::visit([&](const auto& object){views.push_back({object.asset(),object.pose(),object.position(),object.facing(),nullptr,255,{},key});},match.objects()[at]);
        }
    }
    renderer.begin();auto scene=match.scene();scene.shines={};scene.shineAlpha=0;
    renderer.arena(stage,match.camera(),views,scene,&cueArt,&reactionArt,&combatArt,mapBlurArt,mapOpticalArt);
    if(pixelOutput){
        auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());
        const unsigned offset=g_assetCoverageQa.draws%2?512*272*4:0;
        sceKernelDcacheWritebackInvalidateRange(capturePixels.data(),capturePixels.size()*4);
        sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,vram+offset,0,0,512,capturePixels.data());
        sceGuTexSync();
    }
    renderer.end();
    if(pixelOutput){
        const auto* pixels=reinterpret_cast<const unsigned*>(reinterpret_cast<std::uintptr_t>(capturePixels.data())|0x40000000u);
        for(unsigned y=0;y<272;++y)std::fwrite(pixels+y*512,4,480,pixelOutput);
        std::fflush(pixelOutput);
    }
    ++g_assetCoverageQa.draws;
    require(!g_paintRendererQa.missing,"gpu-paint-missing");
    require(!g_textureCacheQa.budget&&!g_textureCacheQa.allocation&&!g_textureCacheQa.decode,
        "gpu-texture-missing budget="+std::to_string(g_textureCacheQa.budget)+
        " allocation="+std::to_string(g_textureCacheQa.allocation)+
        " decode="+std::to_string(g_textureCacheQa.decode)+
        " requested="+std::to_string(g_textureCacheQa.requested)+
        " resident="+std::to_string(g_textureCacheQa.resident)+
        " locked="+std::to_string(g_textureCacheQa.locked));
}
#endif
void textures(const FighterAsset& asset,Counts& n){
    std::set<unsigned> done;
#ifdef __PSP__
    // Each unique tile is inspected on its own completed GE list. Combining
    // unrelated animation frames would manufacture a simultaneous residency
    // requirement that the game never has.
#endif
    for(unsigned c=0;c<asset.clipCount();++c){
        const auto clip=MoveClip(c);
        for(unsigned f=0;f<asset.clip(clip).count;++f){
            ++n.poses;
            for(const auto& pose:asset.poseTiles(asset.frame(clip,f).pose)){
                if(!pose.width||!pose.height||!done.insert(pose.offset).second)continue;
                std::vector<std::byte> pixels(FighterAsset::textureSize(pose));
                require(asset.decodePixels(pose,pixels),"texture-decode");
                ++n.textures;n.bytes+=pixels.size();
                for(auto b:pixels)n.hash=(n.hash^std::to_integer<unsigned>(b))*1099511628211ull;
#ifdef __PSP__
                renderer.begin();
                auto local=pose;local.x=local.y=0;
                renderer.effectPose(asset,local,{160,80},.35,1,0,EffectBlend::Normal);
                renderer.end();++g_assetCoverageQa.draws;
                g_assetCoverageQa.textures=n.textures;g_assetCoverageQa.poses=n.poses;
                require(!g_textureCacheQa.budget&&!g_textureCacheQa.allocation&&!g_textureCacheQa.decode,
                    "gpu-texture-missing budget="+std::to_string(g_textureCacheQa.budget)+" allocation="+std::to_string(g_textureCacheQa.allocation)+" decode="+std::to_string(g_textureCacheQa.decode));
#endif
            }
        }
    }
#ifdef __PSP__
    renderer.clearFighterTextures();
#endif
}
void finite(const FighterMotion& actor){
    const auto p=actor.motion().position;
    require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<1e8&&std::abs(p.y)<1e8,"invalid-position");
    require(std::isfinite(actor.health())&&std::isfinite(actor.qi())&&std::isfinite(actor.energy()),"invalid-gauge");
    (void)actor.pose();
}
void materials(const FightMatch& match,unsigned tick,unsigned command){
    const auto scene=match.scene();if(!scene.paint)return;
    using P=SourcePaint;
    for(auto key:scene.paint->players()){
        const auto id=P::id(key);bool found=false;
        switch(P::kind(key)){
        case P::Kind::Sprite:
            if(!(id&SourceScene::Object))found=id<2;
            else {const auto at=match.sourceScene().storage(id);found=at<match.objects().size();}
            break;
        case P::Kind::Ground:found=id<scene.ground.size()&&scene.ground[id].cue!=GroundCue::Count;break;
        case P::Kind::Reaction:found=id<scene.reactions.size()&&scene.reactions[id].cue!=ReactionCue::Count;break;
        case P::Kind::Shadow:found=std::ranges::any_of(scene.shadows,[&](const auto& v){return unsigned(v.serial)==id;});break;
        case P::Kind::Combat:found=std::ranges::any_of(scene.combat,[&](const auto& v){return v.paintId==id;});break;
        default:break;
        }
        require(found,"paint-player-missing kind="+std::to_string(unsigned(P::kind(key)))+" id="+std::to_string(id)+" command="+std::to_string(command)+" tick="+std::to_string(tick));
    }
    for(auto key:scene.paint->stage())if(P::kind(key)==P::Kind::Combat){
        require(std::ranges::any_of(scene.backgrounds,[&](const auto& v){return v.paintId==P::id(key);}),"paint-background-missing");
    }
    require(!match.sourceScene().stats().rangeFaults,"source-scene-range-fault");
}
std::vector<FighterAsset> family(const RosterAsset& roster,unsigned index,const std::string& folder){
    const auto ids=roster.forms(index);std::vector<FighterAsset> forms(ids.size());
    for(unsigned i=0;i<ids.size();++i)if(ids[i]!=MissingFighter){
        require(forms[i].open((folder+roster.fighter(ids[i]).file).c_str(),FighterAsset::TextureResidency::LowMemory),"family-load");
        require(forms[i].hasObjectAreas(),"family-object-areas");
    }
    return forms;
}
void exercise(std::span<const FighterAsset> forms,unsigned initial,const FighterAsset& rival,const StageAsset& stage,Counts& n){
    for(unsigned hz:{30U,60U})for(unsigned command=0;command<unsigned(FighterCommand::Count);++command){
        if(!forms[initial].has(forms[initial].command(FighterCommand(command))))continue;
        FightMatch match(forms,std::span(&rival,1),{initial,0});
        match.reset(stage,{700,stage.geometry().playerBottom},{790,stage.geometry().playerBottom});
        match.setSourceProfile(hz,15,stage);match.allowLoseHp(false);match.seedObjects(0x12345678);
        for(unsigned side=0;side<2;++side){match.actor(side).setQi(300);match.actor(side).setEnergy(100);}
        match.actor(0).present(FighterCommand(command));++n.commands;
        for(unsigned tick=0;tick<720;++tick){
            match.step(stage.geometry(),{},true,Contacts::Training);++n.steps;
            finite(match.actor(0));finite(match.actor(1));materials(match,tick,command);
            n.objects=std::max<unsigned>(n.objects,match.objects().size());
            for(const auto& object:match.objects())std::visit([&](const auto& item){const auto p=item.position();require(std::isfinite(p.x)&&std::isfinite(p.y),"object-position");(void)item.pose();},object);
#ifdef __PSP__
            g_assetCoverageQa.command=command;g_assetCoverageQa.hz=hz;g_assetCoverageQa.tick=tick;g_assetCoverageQa.commands=n.commands;g_assetCoverageQa.steps=n.steps;
            if(tick==0||tick==8||tick==24||tick==64||tick==128||tick==240||tick==480||tick==719)renderMatch(match,stage);
#endif
        }
    }
#ifdef __PSP__
    renderer.clearFighterTextures();
#endif
}
void assister(const FighterAsset& assist,const FighterAsset& actor,const StageAsset& stage,Counts& n){
    for(unsigned hz:{30U,60U}){
        FightMatch match(std::span(&actor,1),std::span(&actor,1),{},{{{&assist,0},{}}});
        match.reset(stage,{700,stage.geometry().playerBottom},{800,stage.geometry().playerBottom});match.setSourceProfile(hz,15,stage);match.allowLoseHp(false);
        bool seen=false;
        for(unsigned tick=0;tick<720;++tick){
            FighterControls first;first.assistHeld=tick<4;match.step(stage.geometry(),{{first,{}}},true,Contacts::Training);++n.steps;
            materials(match,tick,0);finite(match.actor(0));finite(match.actor(1));
            for(const auto& object:match.objects())std::visit([&](const auto& item){seen|=&item.asset()==&assist;},object);
            n.objects=std::max<unsigned>(n.objects,match.objects().size());
#ifdef __PSP__
            g_assetCoverageQa.hz=hz;g_assetCoverageQa.tick=tick;g_assetCoverageQa.steps=n.steps;
            if(tick==0||tick==8||tick==24||tick==64||tick==128||tick==240||tick==480||tick==719)renderMatch(match,stage);
#endif
        }
        require(seen,"assist-not-spawned");++n.assists;
    }
#ifdef __PSP__
    renderer.clearFighterTextures();
#endif
}
}
int coverage(int argc,char**argv){
    if(argc<2)return 2;
    std::string folder=argv[1];if(folder.back()!='/')folder+='/';
    const std::string only=argc>2?argv[2]:"";
    RosterAsset roster;require(roster.open((folder+"ROSTER.BVR").c_str()),"roster");
    StageAsset stage;require(stage.open((folder+"JIANCHANG.BVN").c_str()),"stage");
    FighterAsset rival;require(rival.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory),"rival");
#ifdef __PSP__
    require(cueArt.open((folder+"UI.BVU").c_str()),"cue-art");
    require(reactionArt.open((folder+"REACTION.BVF").c_str(),FighterAsset::TextureResidency::LowMemory),"reaction-art");
    require(combatArt.open((folder+"COMBAT.BVF").c_str(),FighterAsset::TextureResidency::LowMemory),"combat-art");
    FighterAsset blurDefault;require(blurDefault.open((folder+"JIANCHANG.BVB").c_str(),FighterAsset::TextureResidency::LowMemory),"default-map-blur");mapBlurArt=&blurDefault;
    if(auto* flag=std::fopen((folder+"VALIDPIX.IN").c_str(),"rb")){
        std::fclose(flag);capturePixels.resize(512*272);
        pixelOutput=std::fopen((folder+"PIXELS.OUT").c_str(),"wb");require(pixelOutput,"pixel-capture");
    }
#endif
    auto* list=std::fopen((folder+"VALIDATE.IN").c_str(),"r");require(list,"manifest");
    char kind[16],id[64],file[32];unsigned failures=0,rows=0;
    while(std::fscanf(list,"%15s %63s %31s",kind,id,file)==3){
        if(!only.empty()&&only!=file)continue;
#ifdef __PSP__
        renderer.clearFighterTextures();g_textureCacheQa.budget=g_textureCacheQa.allocation=g_textureCacheQa.decode=0;g_paintRendererQa.missing=0;
#endif
        Counts n;
        try{
            if(std::strcmp(kind,"map")==0){
                StageAsset map;require(map.open((folder+file).c_str(),true),"map-load");
#ifndef __PSP__
                StageAsset reference;require(reference.open((folder+file).c_str()),"map-reference-load");
                reference.useOpticalPalette();
                require(map.tiles().size()==reference.tiles().size()&&map.residentTextureBytes()==reference.residentTextureBytes(),"map-palette-size");
                for(unsigned i=0;i<map.tiles().size();++i){
                    const auto& a=map.tiles()[i];const auto& b=reference.tiles()[i];
                    require(std::memcmp(&a,&b,sizeof(StageTile))==0,"map-palette-tile");
                    require(std::memcmp(map.pixels(a),reference.pixels(b),a.bytes)==0,"map-palette-pixels");
                }
#endif
                require(map.spawn(0).has_value()&&map.spawn(1).has_value(),"map-spawns");
                require(!map.tiles().empty(),"map-tiles");
                FighterAsset blur;auto stem=std::string(file);stem.resize(stem.size()-4);
                require(blur.open((folder+stem+".BVB").c_str(),FighterAsset::TextureResidency::LowMemory),"map-blur-load");textures(blur,n);
                FighterAsset optical;bool hasOptical=false;
                auto* probe=std::fopen((folder+stem+".BVO").c_str(),"rb");
                if(probe){std::fclose(probe);require(optical.open((folder+stem+".BVO").c_str(),FighterAsset::TextureResidency::LowMemory),"map-optical-art");textures(optical,n);hasOptical=true;}
#ifndef __PSP__
                (void)hasOptical;
#endif
#ifdef __PSP__
                mapOpticalArt=hasOptical?&optical:nullptr;if(hasOptical)map.useOpticalPalette();
                mapBlurArt=&blur;
                FightMatch view(rival,rival);view.reset(map,*map.spawn(0),*map.spawn(1));view.setSourceProfile(30,15,map);
                for(unsigned i=0;i<60;++i){view.step(map.geometry(),{},true,Contacts::Training);if(i%10==0)renderMatch(view,map);}
                renderer.clearFighterTextures();mapBlurArt=&blurDefault;mapOpticalArt=nullptr;
#endif
                for(unsigned side=0;side<2;++side){Motion p;p.position=*map.spawn(side);bool landed=false;for(unsigned t=0;t<600;++t){p.step(map.geometry());landed|=!p.airborne;require(std::isfinite(p.position.y),"map-fall-finite");}require(landed,"map-no-landing");}
            }else{
                FighterAsset asset;require(asset.open((folder+file).c_str(),FighterAsset::TextureResidency::LowMemory),"bank-load");
                if(std::strcmp(kind,"aux")!=0)require(asset.hasObjectAreas(),"bank-object-areas");textures(asset,n);
                if(std::strcmp(kind,"fighter")==0){
                    unsigned index=0;for(;index<roster.size();++index)if(std::strcmp(roster.fighter(index).file,file)==0)break;
                    require(index<roster.size(),"roster-binding");auto forms=family(roster,index,folder);exercise(forms,roster.fighter(index).form,rival,stage,n);
                }else if(std::strcmp(kind,"assist")==0)assister(asset,rival,stage,n);
                else if(std::strcmp(kind,"minion")==0)exercise(std::span(&asset,1),0,rival,stage,n);
            }
            std::printf("{\"kind\":\"%s\",\"id\":\"%s\",\"file\":\"%s\",\"pass\":true,\"poses\":%u,\"textures\":%u,\"bytes\":%llu,\"commands\":%u,\"steps\":%u,\"peak_objects\":%u,\"assist_spawns\":%u,\"pixel_hash\":\"%016llx\"}\n",kind,id,file,n.poses,n.textures,(unsigned long long)n.bytes,n.commands,n.steps,n.objects,n.assists,(unsigned long long)n.hash);
        }catch(const std::exception& e){++failures;std::printf("{\"kind\":\"%s\",\"id\":\"%s\",\"file\":\"%s\",\"pass\":false,\"error\":\"%s\",\"commands\":%u,\"steps\":%u,\"poses\":%u,\"textures\":%u}\n",kind,id,file,e.what(),n.commands,n.steps,n.poses,n.textures);}
        ++rows;std::fflush(stdout);
#ifdef __PSP__
        g_assetCoverageQa.row=rows;g_assetCoverageQa.error=failures;
#endif
    }
    std::fclose(list);
#ifdef __PSP__
    if(pixelOutput){std::fclose(pixelOutput);pixelOutput=nullptr;capturePixels.clear();}
#endif
    std::fprintf(stderr,"rows=%u failures=%u\n",rows,failures);return failures?1:0;
}
#ifdef __PSP__
int main(int argc,char**argv){
    const std::string launch=argc?argv[0]:"";const auto at=launch.find_last_of('/');std::string folder=at==std::string::npos?"":launch.substr(0,at+1);
    std::freopen((folder+"VALIDATE.OUT").c_str(),"w",stdout);
    scePowerSetClockFrequency(333,333,166);renderer.start();installGameAllocationRecovery(renderer);g_assetCoverageQa.status=1;
    char* args[]{argv[0],folder.data()};int result=1;
    try{result=coverage(2,args);}catch(const std::exception& e){std::printf("{\"fatal\":\"%s\"}\n",e.what());std::fflush(stdout);g_assetCoverageQa.error=999;}
    g_assetCoverageQa.status=result?3:2;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
#else
int main(int argc,char**argv){return coverage(argc,argv);}
#endif
