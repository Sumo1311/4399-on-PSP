// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_buff.hpp"
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    bvn::buff_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
    std::printf("%s line=%u states=%u views=%u integrated=%u holds=%u renewals=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,
        c.states,c.views,c.integrated,c.holds,c.renewals,static_cast<unsigned long long>(c.hash));return pass?0:1;
}
