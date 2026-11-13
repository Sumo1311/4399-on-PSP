// SPDX-License-Identifier: GPL-3.0-or-later
#include "dispatch_transition.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::dispatch_transition_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u root=%u timeout=%u quick=%u assets=%u integration=%u pauses=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rootRows,c.timeoutRows,c.quickRows,c.assets,c.integration,c.pauses,(unsigned long long)c.hash);return pass?0:1;}
