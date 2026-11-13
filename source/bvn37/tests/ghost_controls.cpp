// SPDX-License-Identifier: GPL-3.0-or-later
#include "ghost_controls.hpp"
#include <cstdio>
#include <fstream>
int main(int argc,char** argv){
    if(argc!=3)return 2;bvn::ghost_qa::Checks checks;std::ifstream input(argv[2]);std::string name;unsigned banks=0;
    while(input>>name){
        bvn::FighterAsset asset;if(!asset.open((std::string(argv[1])+"/"+name).c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 3;
        for(unsigned scenario=0;scenario<4;++scenario)for(unsigned hz:{30U,60U})for(int direction:{-1,1})for(bool cpu:{false,true}){
            if(!checks.exercise(asset,scenario,hz,direction,cpu)){std::fprintf(stderr,"FAIL %s scenario=%u hz=%u dir=%d cpu=%d line=%u\n",name.c_str(),scenario,hz,direction,cpu,checks.error);return 1;}
        }++banks;
    }
    std::array<bvn::FighterAsset,2> forms;
    if(!forms[0].open((std::string(argv[1])+"/ICHIGO.BVF").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)||
       !forms[1].open((std::string(argv[1])+"/ICHIGOB.BVF").c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 3;
    if(!checks.gates(forms)){std::fprintf(stderr,"FAIL gates line=%u\n",checks.error);return 1;}
    std::printf("PASS ghost banks=%u cases=%u starts=%u ends=%u shadowFrames=%u: all three kinds, floor/air jump, directions, human/CPU, two rates and resource gates\n",banks,checks.cases,checks.starts,checks.ends,checks.shadowFrames);
    return banks==78?0:4;
}
