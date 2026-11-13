// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_ai_policy.hpp"
#include <cstdio>
#include <optional>

struct PolicyTraceResult { unsigned rows = 0, resets = 0; int error = 0; };

inline PolicyTraceResult traceCpuPolicy(FILE* input, FILE* output, volatile unsigned* progress = nullptr) {
    using namespace bvn;
    PolicyTraceResult result;
    std::optional<FighterAiPolicy> policy;
    const auto fighter = [&](AiFighterState& state) {
        int action;
        unsigned isFighter;
        if (std::fscanf(input, "%lf %lf %lf %lf %lf %lf %d %d %u", &state.x, &state.y, &state.health,
                &state.maxHealth, &state.qi, &state.energy, &state.facing, &action, &isFighter) != 9) return false;
        state.state = AiState(action);
        state.fighter = isFighter != 0;
        return true;
    };
    char row;
    while (std::fscanf(input, " %c", &row) == 1) {
        if (row == 'R') {
            unsigned level, seed;
            if (std::fscanf(input, "%u %u", &level, &seed) != 2) { result.error = 1; break; }
            policy.emplace(level, seed);
            ++result.resets;
            continue;
        }
        AiObservation view;
        if (row != 'F' || !policy || !fighter(view.self) || !fighter(view.target)) { result.error = 2; break; }
        unsigned body, hittable, basic;
        if (std::fscanf(input, "%u %u %u %u", &body, &hittable, &basic, &view.gameHz) != 4) { result.error = 3; break; }
        view.targetBody = body != 0;
        view.targetHittable = hittable != 0;
        view.basicAttack = basic != 0;
        for (auto& action : view.actions) {
            unsigned bits;
            if (std::fscanf(input, "%u", &bits) != 1 || bits > 15) { result.error = 4; break; }
            action = {bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8)};
        }
        if (result.error) break;
        std::array<AiThreat, 64> threats;
        unsigned count;
        if (std::fscanf(input, "%u", &count) != 1 || count > threats.size()) { result.error = 5; break; }
        for (auto& threat : std::span(threats).first(count)) {
            unsigned kind, active;
            if (std::fscanf(input, "%u %lf %lf %u", &kind, &threat.x, &threat.y, &active) != 4 || kind > 3) {
                result.error = 6;
                break;
            }
            threat.kind = AiThreatKind(kind);
            threat.attacking = active != 0;
        }
        if (result.error) break;
        view.threats = std::span(threats).first(count);
        const auto decisions = policy->step(view);
        std::fprintf(output, "%u %u %u %u\n", result.rows, decisions, policy->randomState(), policy->movementFrames());
        ++result.rows;
        if (progress) *progress = result.rows;
    }
    if (std::ferror(input) || std::ferror(output)) result.error = 7;
    return result;
}
