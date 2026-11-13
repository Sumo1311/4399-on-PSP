// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_session.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::controller_session_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u reference=%u counters=%u duals=%u musou=%u ghosts=%u bossUi=%u manual=%u bossAi=%u banks=%u ends=%u authored=%u transforms=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.referenceRows,c.counterRows,c.duals,c.musou,c.ghosts,c.bossUi,c.manual,c.bossAi,c.typedBanks,c.typedEnds,c.authoredGhosts,c.transformations,(unsigned long long)c.hash);return pass?0:1;}
