// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpu_policy_trace.hpp"

int main() {
    const auto result = traceCpuPolicy(stdin, stdout);
    if (result.error || !result.rows) {
        std::fprintf(stderr, "Policy trace failed: error=%d rows=%u\n", result.error, result.rows);
        return 1;
    }
    std::fprintf(stderr, "Traced %u decisions across %u original policy sequences\n", result.rows, result.resets);
}
