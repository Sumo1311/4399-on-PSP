// SPDX-License-Identifier: GPL-3.0-or-later
#include "lifecycle.hpp"
int main(int n,char** v){if(n!=2)return 2;bvn::lifecycle_qa::Checks c;const bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u alpha=%u races=%u fades=%u actors=%u row=%u col=%u actual=%u expected=%u hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.alphaRows,c.races,c.fades,c.actors,c.failedRow,c.failedColumn,c.actual,c.expected,(unsigned long long)c.hash);return !pass;}
