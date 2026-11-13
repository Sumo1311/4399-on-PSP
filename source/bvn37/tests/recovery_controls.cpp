// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_controls.hpp"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    bvn::recovery_qa::Checks checks;
    std::ifstream input(argv[2]);
    std::string name;
    unsigned banks = 0;
    while (input >> name) {
        bvn::FighterAsset asset;
        if (!asset.open((std::string(argv[1]) + "/" + name).c_str(), bvn::FighterAsset::TextureResidency::LowMemory)) return 3;
        std::array<bvn::FighterAsset, 3> byakuya;
        std::span<const bvn::FighterAsset> forms;
        if (name == "BYAKUYNS.BVF") {
            if (!byakuya[0].open((std::string(argv[1]) + "/BYAKUYA.BVF").c_str(), bvn::FighterAsset::TextureResidency::LowMemory) ||
                !byakuya[1].open((std::string(argv[1]) + "/BYAKUYB.BVF").c_str(), bvn::FighterAsset::TextureResidency::LowMemory) ||
                !byakuya[2].open((std::string(argv[1]) + "/BYAKUYNS.BVF").c_str(), bvn::FighterAsset::TextureResidency::LowMemory)) return 3;
            forms = byakuya;
        }
        for (unsigned hz : {30U, 60U}) for (int direction : {-1, 1}) for (bool cpu : {false, true}) {
            if (!checks.escape(asset, hz, direction, cpu, forms)) {
                std::fprintf(stderr, "FAIL %s hz=%u direction=%d cpu=%d line=%u\n", name.c_str(), hz, direction, cpu, checks.error);
                return 1;
            }
        }
        ++banks;
    }
    bvn::FighterAsset ichigo;
    if (!ichigo.open((std::string(argv[1]) + "/ICHIGO.BVF").c_str())) return 3;
    if (!checks.limits(ichigo) || !checks.mutableHits(ichigo)) {
        std::fprintf(stderr, "FAIL limits/shared HitVO line=%u\n", checks.error); return 1;
    }
    std::printf("PASS banks=%u cases=%u escapes=%u; two game rates, both directions, human/CPU, resource limits and retained mutable hit references\n", banks, checks.cases, checks.escapes);
    return banks == 78 ? 0 : 4;
}
