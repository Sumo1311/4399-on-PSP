// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_order.hpp"
int main(int argc,char** argv){if(argc!=2)return 2;bvn::scene_order_qa::Checks c;const bool pass=c.reference(argv[1]);std::printf("%s line=%u rows=%u range=%u renderActions=%u contactActions=%u row=%u col=%u actual=%d expected=%d hash=%016llx\n",pass?"PASS":"FAIL",c.error,c.rows,c.rangeFaults,c.renderActions,c.contactActions,c.failedRow,c.failedColumn,c.actual,c.expected,(unsigned long long)c.hash);return !pass;}
