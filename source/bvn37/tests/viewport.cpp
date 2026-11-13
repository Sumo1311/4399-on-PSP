// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewport.hpp"
int main(int n,char** v){if(n!=2)return 2;bvn::viewport_qa::Checks c;const bool pass=c.run(v[1]);std::printf("%s line=%u rows=%u faults=%u forced=%u scenes=%u row=%u col=%u actual=%.17g expected=%.17g hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.faults,c.forced,c.scenes,c.failedRow,c.failedColumn,c.actual,c.expected,(unsigned long long)c.hash);return !pass;}
