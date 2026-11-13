// SPDX-License-Identifier: GPL-3.0-or-later
#include "counter_assets.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3)return 2;unsigned frames=0,error=0;
    if(!bvn::counter_qa::pixels(argv[1],argv[2],frames,error)) {
        std::fprintf(stderr,"FAIL counter FX frames=%u error=%u\n",frames,error);return 1;
    }
    std::printf("PASS %u counter FX pose/decoded-pixel records\n",frames);
}
