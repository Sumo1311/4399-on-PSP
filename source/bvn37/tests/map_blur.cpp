// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_blur.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::blur_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/");
 std::printf("%s line=%u states=%u banks=%u clips=%u tiles=%u pixels=%u integrated=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.states,c.banks,c.clips,c.tiles,c.pixels,c.integrated,(unsigned long long)c.hash);return pass?0:1;}
