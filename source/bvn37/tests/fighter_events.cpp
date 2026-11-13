// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_events.hpp"
int main(int n,char** v){if(n!=2)return 2;bvn::fighter_events_qa::Checks c;const bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u actors=%u bosses=%u minions=%u selfs=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.actors,c.bosses,c.minions,c.selfs,(unsigned long long)c.hash);return pass?0:1;}
