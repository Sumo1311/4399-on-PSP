// SPDX-License-Identifier: GPL-3.0-or-later
#include "counter_controls.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    bvn::FighterAsset first,second;
    if(!first.open(argv[1],bvn::FighterAsset::TextureResidency::LowMemory)||
       !second.open(argv[2],bvn::FighterAsset::TextureResidency::LowMemory))return 3;
    bvn::counter_qa::Checks checks;
    for(unsigned faction:{0U,1U})for(unsigned hz:{30U,60U})for(int facing:{-1,1})for(bool cpu:{false,true})for(double energy:{50.,100.}) {
        if(!checks.start(faction?second:first,faction,hz,facing,cpu,energy)) {
            std::fprintf(stderr,"FAIL faction=%u hz=%u facing=%d cpu=%d energy=%g line=%u\n",faction,hz,facing,cpu,energy,checks.error);return 1;
        }
    }
    if(!checks.gates(first)){std::fprintf(stderr,"FAIL gates line=%u\n",checks.error);return 1;}
    std::printf("PASS counter cases=%u starts=%u: both factions, two game rates, both directions, human/CPU, resource boundaries, source pause and animation timers\n",checks.cases,checks.starts);
}
