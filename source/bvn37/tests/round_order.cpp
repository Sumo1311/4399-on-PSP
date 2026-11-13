// SPDX-License-Identifier: GPL-3.0-or-later
#include "round_order.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::round_order_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u rows=%u cutins=%u manual=%u expiry=%u hp=%u duals=%u banners=%u hooks=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.cutins,c.manual,c.expiry,c.hp,c.duals,c.banners,c.hooks,(unsigned long long)c.hash);return pass?0:1;}
