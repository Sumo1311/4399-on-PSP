// SPDX-License-Identifier: GPL-3.0-or-later
#include "shine_queue.hpp"
int main(int argc,char** argv){
    if(argc!=3)return 2;
    FILE* trace=std::fopen(argv[2],"w");if(!trace)return 3;
    bvn::shine_qa::Checks c;const bool pass=c.run(std::string(argv[1])+"/",trace);std::fclose(trace);
    std::printf("%s error=%u scenarios=%u states=%u emitted=%u maxLayers=%u emptied=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.scenarios,c.states,c.emitted,c.maxLayers,c.emptied,(unsigned long long)c.hash);return pass?0:1;
}
