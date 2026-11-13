// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_asset.hpp"
#include <cstdio>
#include <string>
int main(int argc,char** argv){
    if(argc!=2)return 2;char name[64];
    while(std::scanf("%63s",name)==1){
        bvn::FighterAsset asset;
        if(!asset.open((std::string(argv[1])+"/"+name).c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 3;
        std::printf("bank\t%s\t%u\t%u\n",name,unsigned(asset.assister()),asset.attackerCount());
        for(unsigned i=0;i<asset.attackerCount();++i){const auto& a=asset.attacker(i);
            std::printf("object\t%u\t%u\t%u\t%u\n",i,unsigned(a.clip),unsigned(a.initial),a.key);}
    }
}
