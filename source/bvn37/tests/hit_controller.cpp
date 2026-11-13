// SPDX-License-Identifier: GPL-3.0-or-later
#include "hit_controller.hpp"
int main(int n,char** v){if(n!=2)return 2;bvn::hit_controller_qa::Checks c;const bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u actors=%u sessions=%u row=%u col=%u actual=%.12g expected=%.12g hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.actors,c.sessions,c.failedRow,c.failedColumn,c.actual,c.expected,(unsigned long long)c.hash);return pass?0:1;}
