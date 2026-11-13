// SPDX-License-Identifier: GPL-3.0-or-later
#include "paint.hpp"
int main(int n,char** v){if(n!=2)return 2;bvn::paint_qa::Checks c;const bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u faults=%u changes=%u matches=%u switches=%u layers=%u row=%u col=%u actual=%llu expected=%llu hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.faults,c.changes,c.matches,c.switches,c.layers,c.failedRow,c.failedColumn,(unsigned long long)c.actual,(unsigned long long)c.expected,(unsigned long long)c.hash);return !pass;}
