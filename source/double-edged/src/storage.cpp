// Checksummed SharedObject payloads, with a complete prior generation retained.
#include "storage.hpp"
#include "quickjs.h"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <sys/stat.h>
#include <zlib.h>
namespace {
constexpr unsigned Limit=1024*1024;
bool cooperative=false;std::string shared_settings,shared_progress;
std::string save_key(JSContext* c,JSValueConst name){
    size_t length=0;const char* p=JS_ToCStringLen(c,&length,name);if(!p)return {};
    if(!length||length>96){JS_FreeCString(c,p);JS_ThrowRangeError(c,"Invalid save name");return {};}
    std::string key(p,length);JS_FreeCString(c,p);return key;
}
std::string filename(const std::string& key){
    const char* digits="0123456789abcdef";std::string result="SAVE/SO_";
    for(unsigned i=0;i<key.size();i++){unsigned ch=static_cast<unsigned char>(key[i]);result+=digits[ch>>4];result+=digits[ch&15];}
    return result+".SAV";
}
void put32(unsigned char* p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(n>>(i*8))&255;}
uint32_t get32(const unsigned char* p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=uint32_t(p[i])<<(i*8);return n;}
bool load(const std::string& path,std::string& text){
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;unsigned char header[16];
    bool valid=std::fread(header,1,sizeof(header),f)==sizeof(header)&&!std::memcmp(header,"DESV",4)&&get32(header+4)==1;
    unsigned size=valid?get32(header+8):0;if(size>Limit)valid=false;
    if(valid){text.resize(size);valid=std::fread(text.data(),1,size,f)==size&&std::fgetc(f)==EOF&&uint32_t(crc32(0,reinterpret_cast<const Bytef*>(text.data()),size))==get32(header+12);}
    std::fclose(f);if(!valid)text.clear();return valid;
}
JSValue load_save(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(!argc)return JS_ThrowTypeError(c,"Save name required");std::string key=save_key(c,argv[0]),text;if(key.empty())return JS_EXCEPTION;
    if(cooperative){const std::string& value=key=="so_doubleedged"?shared_settings:key=="so_doubleedged1"?shared_progress:std::string("{}");return JS_NewStringLen(c,value.data(),value.size());}
    std::string name=filename(key);
    if(load(name,text)||load(name+".BAK",text))return JS_NewStringLen(c,text.data(),text.size());return JS_NULL;
}
JSValue store_save(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(argc<2)return JS_ThrowTypeError(c,"Save payload required");std::string key=save_key(c,argv[0]);if(key.empty())return JS_EXCEPTION;
    size_t size=0;const char* p=JS_ToCStringLen(c,&size,argv[1]);if(!p)return JS_EXCEPTION;
    if(size>Limit){JS_FreeCString(c,p);return JS_ThrowRangeError(c,"Save payload too large");}
    std::string text(p,size),old;JS_FreeCString(c,p);
    if(cooperative){if(key=="so_doubleedged"){shared_settings=text;return JS_TRUE;}if(key!="so_doubleedged1")return JS_TRUE;shared_progress=text;key="coop_"+key;}
    std::string name=filename(key);bool valid=load(name,old);
    if(valid&&old==text)return JS_TRUE;
    if(mkdir("SAVE",0777)&&errno!=EEXIST)return JS_ThrowInternalError(c,"Cannot create save directory");
    std::string temp=name+".TMP",backup=name+".BAK";unsigned char header[16];std::memcpy(header,"DESV",4);put32(header+4,1);put32(header+8,size);put32(header+12,crc32(0,reinterpret_cast<const Bytef*>(text.data()),size));
    FILE* f=std::fopen(temp.c_str(),"wb");if(!f)return JS_ThrowInternalError(c,"Cannot open save transaction");
    bool written=std::fwrite(header,1,sizeof(header),f)==sizeof(header)&&std::fwrite(text.data(),1,size,f)==size&&std::fflush(f)==0;
    if(std::fclose(f))written=false;
    if(!written){std::remove(temp.c_str());return JS_ThrowInternalError(c,"Cannot finish save transaction");}
    std::string checked;if(!load(temp,checked)||checked!=text)return JS_ThrowInternalError(c,"Save verification failed");
    if(valid){
        if(std::remove(backup.c_str())&&errno!=ENOENT)return JS_ThrowInternalError(c,"Cannot rotate save backup");
        if(std::rename(name.c_str(),backup.c_str()))return JS_ThrowInternalError(c,"Cannot retain save backup");
    }else if(std::remove(name.c_str())&&errno!=ENOENT)return JS_ThrowInternalError(c,"Cannot replace damaged save");
    if(std::rename(temp.c_str(),name.c_str()))return JS_ThrowInternalError(c,"Cannot commit save transaction");
    return JS_TRUE;
}
}
int de_storage_bind(JSContext* c){JSValue g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"nativeLoadSave",JS_NewCFunction(c,load_save,"nativeLoadSave",1));JS_SetPropertyStr(c,g,"nativeStoreSave",JS_NewCFunction(c,store_save,"nativeStoreSave",2));JS_FreeValue(c,g);return 0;}
bool de_storage_read_payload(const char* name,std::string& payload){std::string key(name);if(key.empty()||key.size()>96)return false;std::string path=filename(key);return load(path,payload)||load(path+".BAK",payload);}
void de_storage_use_coop(const std::string& settings,const std::string& progress){cooperative=true;shared_settings=settings;shared_progress=progress;}
