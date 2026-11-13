// SPDX-License-Identifier: GPL-3.0-or-later
#include "ko_background.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::ko_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
 std::printf("%s line=%u states=%u views=%u matches=%u holds=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.states,c.views,c.matches,c.holds,(unsigned long long)c.hash);return pass?0:1;}
