// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_status.hpp"
int main(int argc,char**argv){
    if(argc!=2)return 2;bvn::status_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
    std::printf("%s line=%u states=%u colors=%u views=%u callbacks=%u zero=%u integrated=%u defenses=%u hash=%016llx\n",pass?"PASS":"FAIL",
        c.error,c.states,c.colors,c.views,c.callbacks,c.zeroAngles,c.integrated,c.defenses,static_cast<unsigned long long>(c.hash));return pass?0:1;
}
