// SPDX-License-Identifier: GPL-3.0-or-later
#include "reaction_lifecycle.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::reaction_lifecycle_qa::Checks c;const bool pass=c.run(argv[1]);std::printf("%s line=%u ref=%u actors=%u banks=%u forced=%u versus=%u musou=%u pauses=%u row=%u col=%u actual=%.12g expected=%.12g hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.referenceRows,c.actors,c.banks,c.forced,c.versus,c.musou,c.pauses,c.failedRow,c.failedColumn,c.actual,c.expected,(unsigned long long)c.hash);return pass?0:1;}
