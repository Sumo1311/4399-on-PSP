// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_floor.hpp"
int main(int argc,char** argv){
    if(argc!=2)return 2;
    bvn::floor_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
    std::printf("%s line=%u states=%u banks=%u flights=%u impacts=%u sounds=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.states,c.banks,c.flights,c.impacts,c.sounds,(unsigned long long)c.hash);
    return pass?0:1;
}
