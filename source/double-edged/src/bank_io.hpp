#pragma once
#include <pspiofilemgr.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

// newlib's cwd and the kernel's per-thread relative-I/O directory are separate.
// Resolve the actual installation directory, including alternative devices;
// never hardcode a QA directory or ms0. Callers retain their stdio fallback.
inline SceUID open_psp_bank(const char* name){
    char directory[512],path[768];
    if(!getcwd(directory,sizeof(directory)))return -1;
    auto length=std::strlen(directory);
    int n=std::snprintf(path,sizeof(path),"%s%s%s",directory,length&&directory[length-1]=='/'?"":"/",name);
    if(n<0||unsigned(n)>=sizeof(path))return -1;
    return sceIoOpen(path,PSP_O_RDONLY,0);
}
