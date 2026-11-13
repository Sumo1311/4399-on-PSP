// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_clock.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::controller_clock_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u rows=%u display=%u actor=%u effect=%u split=%u matches=%u paused=%u knockouts=%u musou=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.display,c.actor,c.effect,c.split,c.matches,c.paused,c.knockouts,c.musouCases,(unsigned long long)c.hash);return pass?0:1;}
