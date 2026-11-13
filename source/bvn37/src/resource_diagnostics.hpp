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
struct ResourceLoadContext {
    char step[24] = "OPEN";
    unsigned index = 0, expected = 0, actual = 0;
    long fileLength = -1, position = -1;
    int streamError = 0, eof = 0;
    unsigned long long metadata = 0;
};
struct ResourceDiagnostics {
    bool synchronousOnly = false;
    unsigned failures = 0;
    int error = 0;
    unsigned texture = 0, bytes = 0;
    unsigned heapFree = 0, partitionFree = 0, largestPartition = 0;
    char attempted[192]{}, failed[192]{}, operation[32]{};
    ResourceLoadContext loading{}, failedLoad{};
    unsigned expectedFileBytes = 0, expectedCrc = 0, actualCrc = 0, verifiedBytes = 0;
    int integrity = 0, verifyErrno = 0;
};
inline ResourceDiagnostics resourceDiagnostics;
inline void diagnosticAttempt(const char* path) {
    std::snprintf(resourceDiagnostics.attempted, sizeof(resourceDiagnostics.attempted), "%s", path);
    resourceDiagnostics.loading = {};
}
inline void diagnosticFailure(const char* path, const char* operation, int error,
                              unsigned texture = 0, unsigned bytes = 0) {
    ++resourceDiagnostics.failures;
    resourceDiagnostics.error = error;
    resourceDiagnostics.texture = texture;
    resourceDiagnostics.bytes = bytes;
    resourceDiagnostics.failedLoad = resourceDiagnostics.loading;
    resourceDiagnostics.integrity = 0;
    resourceDiagnostics.expectedFileBytes = resourceDiagnostics.expectedCrc = 0;
    resourceDiagnostics.actualCrc = resourceDiagnostics.verifiedBytes = 0;
    resourceDiagnostics.verifyErrno = 0;
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
