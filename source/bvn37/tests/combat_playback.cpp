// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_playback.hpp"
int main(int argc,char** argv){
    if(argc!=2)return 2;bvn::combat_playback_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
    std::printf("%s line=%u shake=%u caps=%u lifetimes=%u caches=%u freezes=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,
        c.shakeStates,c.caps,c.lifetimes,c.caches,c.freezes,static_cast<unsigned long long>(c.hash));return pass?0:1;
}
