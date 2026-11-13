// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include <filesystem>
int main(int argc,char** argv){
 if(argc!=4)return 2;std::filesystem::create_directory(argv[2]);FILE* trace=std::fopen(argv[3],"w");if(!trace)return 3;
 bvn::settings_qa::Checks checks;const bool pass=checks.run(std::string(argv[1])+"/",argv[2],trace);std::fclose(trace);
 std::printf("%s line=%u variants=%u journal=%u rows=%u rounds=%u classic=%u standard=%u inputHash=%016llx taps=%u\n",pass?"PASS":"FAIL",checks.error,checks.variants,checks.saves,checks.inputRows,checks.rounds,checks.classicActions,checks.standardActions,(unsigned long long)checks.inputHash,checks.tapCases);return pass?0:1;
}
