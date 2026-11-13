// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_effects.hpp"
int main(int argc,char** argv){
 if(argc!=3)return 2;bvn::frame_effects_qa::Checks c;
 if(!c.run(std::string(argv[1])+"/",argv[2])){std::fprintf(stderr,"FAIL bank=%u line=%u cases=%u\n",c.banks,c.error,c.cases);return 1;}
 std::printf("PASS banks=%u cases=%u walks=%u puffs=%u tints=%u lifetimes=%u\n",c.banks,c.cases,c.walks,c.puffs,c.tints,c.lifetimes);
}
