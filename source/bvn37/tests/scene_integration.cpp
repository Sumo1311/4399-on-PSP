// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_integration.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::scene_order_qa::Integration c;const bool pass=c.run(argv[1]);std::printf("%s line=%u bulletRows=%u matches=%u switches=%u clashes=%u births=%u skips=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.bulletRows,c.matches,c.switches,c.clashes,c.births,c.skips,(unsigned long long)c.hash);return !pass;}
