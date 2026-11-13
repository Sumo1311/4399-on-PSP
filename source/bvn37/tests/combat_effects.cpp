// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_effects_checks.hpp"
int main(int argc,char** argv) {
    if(argc<2||argc>3)return 2;
    bvn::combat_effects_qa::Checks c;
    FILE* trace=argc==3?std::fopen(argv[2],"w"):nullptr;
    if(argc==3&&!trace)return 3;
    const bool pass=c.run(std::string(argv[1])+"/",trace);if(trace)std::fclose(trace);
    std::printf("%s line=%u definitions=%u queries=%u clips=%u frames=%u pixels=%u contacts=%u geometry=%u hash=%016llx\n",
        pass?"PASS":"FAIL",c.error,c.definitions,c.queries,c.clips,c.frames,c.pixels,c.contacts,c.geometry,(unsigned long long)c.hash);
    return pass?0:1;
}
