// SPDX-License-Identifier: GPL-3.0-or-later
#include "emission.hpp"
int main(int n,char**v){if(n!=2)return 2;bvn::emission_qa::Checks c;bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u calls=%u queries=%u peak=%u ordinary=%u musou=%u assists=%u lookups=%u switches=%u roots=%u row=%u col=%u actual=%llu expected=%llu hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.calls,c.queries,c.peak,c.ordinary,c.musou,c.assists,c.lookups,c.switches,c.roots,c.failedRow,c.failedColumn,(unsigned long long)c.actual,(unsigned long long)c.expected,(unsigned long long)c.hash);return !pass;}
