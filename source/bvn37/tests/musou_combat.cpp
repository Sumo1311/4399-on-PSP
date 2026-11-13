// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_combat.hpp"
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    bvn::musou_combat_qa::Checks c; const bool pass=c.run(std::string(argv[1])+"/");
    std::printf("%s line=%u styles=%u guards=%u freezes=%u battles=%u held=%u effects=%u bosses=%u projectiles=%u fades=%u switches=%u deaths=%u hash=%016llx\n",
        pass?"PASS":"FAIL",c.error,c.styles,c.guards,c.freezes,c.battles,c.held,c.effects,c.bosses,c.projectiles,
        c.fades,c.switches,c.deaths,
        static_cast<unsigned long long>(c.hash)); return pass?0:1;
}
