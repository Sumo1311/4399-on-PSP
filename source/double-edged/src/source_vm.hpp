#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "quickjs.h"
class SourceVM {
    JSRuntime* runtime_=nullptr;
    JSContext* context_=nullptr;
    std::string error_;
    uint32_t parked_bytecodes_=0;
public:
    SourceVM();
    ~SourceVM();
    SourceVM(const SourceVM&)=delete;
    bool initialize(uint32_t seed,bool realtime_clock);
    bool eval_file(const char* path);
    bool eval(const char* source,const char* label);
    bool call(JSValueConst function,int argc=0,JSValueConst* argv=nullptr);
    void collect();
    void capture_exception();
    JSContext* context()const{return context_;}
    uint32_t parked_bytecodes()const{return parked_bytecodes_;}
    const std::string& error()const{return error_;}
    static size_t heap_bytes();
    static size_t peak_bytes();
};
