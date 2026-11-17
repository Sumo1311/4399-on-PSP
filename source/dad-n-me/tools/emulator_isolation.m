// Use a process-specific instance counter, leaving the ordinary PPSSPP profile alone.
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define INTERPOSE(old, replacement) \
    __attribute__((used,section("__DATA,__interpose"))) \
    static const struct {const void *new_fn, *old_fn;} replace_##old = {replacement, old}

static const char* instance(const char* path) {
    const char* id = getenv("DNM_PPSSPP_INSTANCE");
    return id && !strcmp(path, "/PPSSPP_ID") ? id : path;
}
static int private_open(const char* path, int flags, ...) {
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        return shm_open(instance(path), flags, mode);
    }
    return shm_open(instance(path), flags);
}
INTERPOSE(shm_open, private_open);
static int private_unlink(const char* path) { return shm_unlink(instance(path)); }
INTERPOSE(shm_unlink, private_unlink);
static int local_dns(const char* host, const char* service, const struct addrinfo* hints, struct addrinfo** result) {
    if (host && !strcmp(host,"report.ppsspp.org")) return EAI_NONAME;
    return getaddrinfo(host, service, hints, result);
}
INTERPOSE(getaddrinfo, local_dns);

#import <Foundation/Foundation.h>

static id qa_activity;
__attribute__((constructor)) static void configure_stick(void) {
    if(getenv("PSP_QA_ACTIVE")){@autoreleasepool{qa_activity=[[NSProcessInfo processInfo] beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep|NSActivityLatencyCritical) reason:@"Isolated PSP normal-speed frame pacing measurement"];}}
    const char* path = getenv("DNM_PPSSPP_STICK");
    if (path) {
        @autoreleasepool {
            [[NSUserDefaults standardUserDefaults] registerDefaults:@{
                @"UserPreferredMemoryStickDirectoryPath": [NSString stringWithUTF8String:path]
            }];
        }
    }
}

#include "native_wall_sampler.inc"
