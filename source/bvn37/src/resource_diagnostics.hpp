// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef BVN_RESOURCE_DIAGNOSTICS
#include <cstddef>
#include <cstdio>
#include <cstring>
#ifdef __PSP__
#include <malloc.h>
#include <pspkernel.h>
#endif

namespace bvn {
struct ResourceDiagnostics {
    bool synchronousOnly = false;
    unsigned failures = 0;
    int error = 0;
    unsigned texture = 0, bytes = 0;
    unsigned heapFree = 0, partitionFree = 0, largestPartition = 0;
    char attempted[192]{}, failed[192]{}, operation[32]{};
};
inline ResourceDiagnostics resourceDiagnostics;
inline void diagnosticAttempt(const char* path) {
    std::snprintf(resourceDiagnostics.attempted, sizeof(resourceDiagnostics.attempted), "%s", path);
}
inline void diagnosticFailure(const char* path, const char* operation, int error,
                              unsigned texture = 0, unsigned bytes = 0) {
    ++resourceDiagnostics.failures;
    resourceDiagnostics.error = error;
    resourceDiagnostics.texture = texture;
    resourceDiagnostics.bytes = bytes;
#ifdef __PSP__
    resourceDiagnostics.heapFree = mallinfo().fordblks;
    resourceDiagnostics.partitionFree = sceKernelTotalFreeMemSize();
    resourceDiagnostics.largestPartition = sceKernelMaxFreeMemSize();
#endif
    std::snprintf(resourceDiagnostics.failed, sizeof(resourceDiagnostics.failed), "%s", path);
    std::snprintf(resourceDiagnostics.operation, sizeof(resourceDiagnostics.operation), "%s", operation);
}
inline const char* diagnosticFileName() {
    const auto* name = std::strrchr(resourceDiagnostics.failed, '/');
    return name ? name + 1 : resourceDiagnostics.failed;
}
} // namespace bvn
#endif
