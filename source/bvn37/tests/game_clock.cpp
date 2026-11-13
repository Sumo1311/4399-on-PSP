// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_clock.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::game_clock_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u rows=%u actor=%u effect=%u split=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.actorFrames,c.effectFrames,c.splitFrames,(unsigned long long)c.hash);return pass?0:1;}
