// SPDX-License-Identifier: GPL-3.0-or-later
#include "start_end.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::start_end_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u rows=%u timelines=%u starts=%u skips=%u ends=%u pauses=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.timelines,c.starts,c.skips,c.ends,c.pauses,(unsigned long long)c.hash);return pass?0:1;}
