// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud_session.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::hud_session_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u hud=%u musou=%u versus=%u sessions=%u pauses=%u assets=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.hudRows,c.musouRows,c.versusCases,c.musouCases,c.pauses,c.assets,(unsigned long long)c.hash);return pass?0:1;}
