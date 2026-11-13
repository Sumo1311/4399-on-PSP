// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_order.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::controller_order_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u rows=%u cutins=%u ghosts=%u ghostFrames=%u hooks=%u dual=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.cutins,c.ghosts,c.ghostFrames,c.hooks,c.dual,(unsigned long long)c.hash);return pass?0:1;}
