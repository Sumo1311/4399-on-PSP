// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_asset.hpp"
#include "fighter_motion.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace bvn;
namespace {
template<class T> bool equal(const T& a, const T& b) { return std::memcmp(&a,&b,sizeof(T))==0; }
}

int main(int argc,char** argv) {
    if(argc!=4&&argc!=5) return 2;
    const std::filesystem::path oldDir(argv[1]),newDir(argv[2]);
    std::ifstream names(argv[3]);assert(names);
    std::string variant,bank,family,line;
    unsigned banks=0,frames=0,recoveryFrames=0,textures=0;
    while(std::getline(names,line)) {
        const auto a=line.find(' '),b=line.find(' ',a+1);bank=line.substr(a+1,b-a-1)+".BVF";
        FighterAsset oldAsset,newAsset;
        if(!oldAsset.open((oldDir/bank).string().c_str())||!newAsset.open((newDir/bank).string().c_str())) {
            std::fprintf(stderr,"Cannot load %s\n",bank.c_str());return 1;
        }
        assert(newAsset.clipCount()>=oldAsset.clipCount()+1);
        assert(newAsset.hurtEscape()==MoveClip(oldAsset.clipCount()));
        assert(newAsset.has(newAsset.hurtEscape()));
        assert(equal(oldAsset.stats(),newAsset.stats()));
        for(unsigned c=0;c<unsigned(FighterCommand::Count);++c) {
            const auto command=FighterCommand(c);
            assert(oldAsset.command(command)==newAsset.command(command));
            assert(equal(oldAsset.aiRange(command),newAsset.aiRange(command)));
        }
        std::set<unsigned> seen;
        std::vector<std::byte> first,second;
        for(unsigned c=0;c<oldAsset.clipCount();++c) {
            const auto clip=MoveClip(c);
            assert(equal(oldAsset.clip(clip),newAsset.clip(clip)));
            for(unsigned phase=0;phase<oldAsset.clip(clip).count;++phase) {
                const auto& old=oldAsset.frame(clip,phase);const auto& now=newAsset.frame(clip,phase);
                assert(equal(old,now));++frames;
                const auto aHits=oldAsset.hits(old),bHits=newAsset.hits(now);
                assert(aHits.size()==bHits.size()&&std::memcmp(aHits.data(),bHits.data(),aHits.size_bytes())==0);
                const auto aEvents=oldAsset.events(old),bEvents=newAsset.events(now);
                assert(aEvents.size()==bEvents.size()&&std::memcmp(aEvents.data(),bEvents.data(),aEvents.size_bytes())==0);
                const auto aTiles=oldAsset.poseTiles(old.pose),bTiles=newAsset.poseTiles(now.pose);
                assert(aTiles.size()==bTiles.size());
                for(unsigned i=0;i<aTiles.size();++i) {
                    const auto& a=aTiles[i];const auto& b=bTiles[i];assert(equal(a,b));
                    if(!a.stride||!a.width||!a.height||!seen.insert(a.offset).second)continue;
                    first.resize(FighterAsset::textureSize(a));second.resize(first.size());
                    assert(oldAsset.decodePixels(a,first)&&newAsset.decodePixels(b,second)&&first==second);++textures;
                }
            }
        }
        const auto escape=newAsset.hurtEscape();
        for(unsigned i=0;i<newAsset.clip(escape).count;++i) {
            const auto& frame=newAsset.frame(escape,i);
            for(const auto& pose:newAsset.poseTiles(frame.pose)) {
                if(!pose.stride||!pose.width||!pose.height)continue;
                first.resize(FighterAsset::textureSize(pose));assert(newAsset.decodePixels(pose,first));
            }
            ++recoveryFrames;
        }
        ++banks;
    }
    assert(banks==78&&recoveryFrames>500);
    std::printf("PASS %u augmented banks: %u original frames and %u original textures unchanged; %u new original recovery frames load/decode\n",banks,frames,textures,recoveryFrames);
    if(argc==5) {
        std::ifstream input(argv[4]);assert(input);
        std::string file,previous;unsigned command,breaking,down,count=0;
        FighterAsset asset;
        while(input>>file>>command>>breaking>>down) {
            if(file!=previous) {assert(asset.open((newDir/file).string().c_str()));previous=file;}
            FighterMotion motion(asset);motion.reset({400,300},1);
            const auto traits=motion.aiHitTraits(FighterCommand(command));
            assert(traits.breaksGuard==bool(breaking)&&traits.knocksDown==bool(down));++count;
        }
        assert(input.eof()&&count==780);
        std::puts("PASS 780 original hit queries after adding recovery hit definitions");
    }
}
