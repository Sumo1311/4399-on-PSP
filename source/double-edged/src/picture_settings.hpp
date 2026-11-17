#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
namespace depicture {
static bool read(const char* path,bool& smooth){
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    char value[16]{};size_t n=std::fread(value,1,sizeof(value),f);
    bool valid=!std::ferror(f)&&std::fgetc(f)==EOF;std::fclose(f);
    if(!valid)return false;
    if(n==7&&!std::memcmp(value,"SMOOTH\n",7)){smooth=true;return true;}
    if(n==9&&!std::memcmp(value,"ORIGINAL\n",9)){smooth=false;return true;}
    return false;
}
static bool load(){bool value=true;if(read("SAVE/PICTURE.CFG",value)||read("SAVE/PICTURE.CFG.BAK",value))return value;return true;}
static bool save(bool smooth){
    bool old=false,valid=read("SAVE/PICTURE.CFG",old);if(valid&&old==smooth)return true;
    if(mkdir("SAVE",0777)&&errno!=EEXIST)return false;
    const char* temp="SAVE/PICTURE.CFG.TMP";const char* backup="SAVE/PICTURE.CFG.BAK";
    const char* value=smooth?"SMOOTH\n":"ORIGINAL\n";size_t length=std::strlen(value);
    FILE* f=std::fopen(temp,"wb");if(!f)return false;
    bool written=std::fwrite(value,1,length,f)==length&&std::fflush(f)==0;
    if(std::fclose(f))written=false;
    bool checked=false;if(!written||!read(temp,checked)||checked!=smooth){std::remove(temp);return false;}
    // Match the existing SharedObject transaction: retain a valid previous
    // generation, preserve a valid backup when replacing damaged primary data.
    if(valid){
        if(std::remove(backup)&&errno!=ENOENT)return false;
        if(std::rename("SAVE/PICTURE.CFG",backup))return false;
    }else if(std::remove("SAVE/PICTURE.CFG")&&errno!=ENOENT)return false;
    return std::rename(temp,"SAVE/PICTURE.CFG")==0;
}
}
