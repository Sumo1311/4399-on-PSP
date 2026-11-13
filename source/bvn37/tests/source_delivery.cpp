// SPDX-License-Identifier: GPL-3.0-or-later
#include "source_delivery.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::source_delivery_qa::Checks c;const bool pass=c.run(argv[1]);
 std::printf("%s line=%u motion=%u bullets=%u calls=%u inputs=%u rounds=%u freezes=%u musou=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.motions,c.bullets,c.calls,c.inputs,c.rounds,c.freezes,c.musou,(unsigned long long)c.hash);return pass?0:1;}
