// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_allocation_recovery.hpp"
#include "psp_stage_renderer.hpp"

#include <new>
#include <pspkernel.h>

extern "C" {
volatile struct AllocationRecoveryQA {
    unsigned installed, ownerThread, requests, releases, bytes, unavailable;
} g_allocationRecoveryQa;
}

namespace bvn {
namespace {
// Written once before publishing the handler and creating worker threads.
// The main game's renderer has static storage duration.
StageRenderer* target = nullptr;
SceUID owner = -1;
std::new_handler previous = nullptr;

void recoverAllocation() {
    if (sceKernelGetThreadId() == owner) {
        g_allocationRecoveryQa.requests = g_allocationRecoveryQa.requests + 1;
        std::size_t bytes = 0;
        if (target->reclaimCompletedTexture(&bytes)) {
            g_allocationRecoveryQa.releases = g_allocationRecoveryQa.releases + 1;
            g_allocationRecoveryQa.bytes = g_allocationRecoveryQa.bytes + bytes;
            return; // The allocating operator retries the original request.
        }
        g_allocationRecoveryQa.unavailable = g_allocationRecoveryQa.unavailable + 1;
    }
    if (previous) { previous(); return; }
    throw std::bad_alloc();
}
}

bool installGameAllocationRecovery(StageRenderer& renderer) {
    if (target) return target == &renderer && owner == sceKernelGetThreadId();
    target = &renderer;
    owner = sceKernelGetThreadId();
    previous = std::set_new_handler(recoverAllocation);
    g_allocationRecoveryQa.ownerThread = owner;
    g_allocationRecoveryQa.installed = 1;
    return true;
}
}
