// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_optical_checks.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::optical_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
 std::printf("%s line=%u states=%u half=%u maps=%u variants=%u palette=%u pixels=%u lookups=%u captures=%u objectBanks=%u objectAreas=%u integrated=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.states,c.half,c.maps,c.variants,c.palette,c.pixels,c.lookups,c.captures,c.objectBanks,c.objectAreas,c.integrated,(unsigned long long)c.hash);return pass?0:1;}
