#pragma once
#include <pspkernel.h>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

// Opt-in automatic benchmark only. Records stay in RAM until the input prefix
// has been presented, so this adds no source-profile writes inside the sample.
class SourceProfile {
public:
    struct Stamp { std::uint64_t wall=0,run=0;bool valid=false; };
    struct Record {
        unsigned tick,logic_wall_us,capture_wall_us,post_wall_us,total_wall_us;
        unsigned logic_run_us,capture_run_us,post_run_us,total_run_us,commands,coordinates;
    };
    static_assert(sizeof(Record)==44);
private:
    bool enabled_=false,run_supported_=false;
    unsigned limit_=0,cal_first_=0,cal_second_=0;int calibration_=-1;
    std::vector<Record> rows_;
    static unsigned delta(Stamp a,Stamp b){
        return a.valid&&b.valid&&b.run>=a.run&&b.run-a.run<0xffffffffULL?
            unsigned(b.run-a.run):0xffffffffu;
    }
public:
    void init(bool enabled,unsigned limit){
        if(!enabled||!limit||limit>4096)return;
        limit_=limit;rows_.reserve(limit);enabled_=true;
        SceKernelSysClock known{};
        calibration_=sceKernelUSec2SysClock(1234567,&known);
        if(calibration_>=0)calibration_=sceKernelSysClock2USec(&known,&cal_first_,&cal_second_);
        run_supported_=calibration_>=0&&cal_first_==1&&cal_second_==234567;
    }
    Stamp stamp()const{
        if(!enabled_)return {};
        Stamp out;out.wall=sceKernelGetSystemTimeWide();
        SceKernelThreadRunStatus state{};state.size=sizeof(state);
        if(run_supported_&&sceKernelReferThreadRunStatus(sceKernelGetThreadId(),&state)>=0){
            unsigned seconds=0,micros=0;
            if(sceKernelSysClock2USec(&state.runClocks,&seconds,&micros)>=0&&micros<1000000){
                out.run=std::uint64_t(seconds)*1000000+micros;out.valid=true;
            }
        }
        return out;
    }
    void append(unsigned tick,unsigned logic,unsigned capture,Stamp begin,Stamp logic_begin,
                Stamp logic_end,Stamp captured,Stamp ready,unsigned commands,unsigned coordinates){
        if(!enabled_||tick>limit_||rows_.size()>=limit_)return;
        rows_.push_back({tick,logic,capture,unsigned(ready.wall-captured.wall),unsigned(ready.wall-begin.wall),
            delta(logic_begin,logic_end),delta(logic_end,captured),delta(captured,ready),delta(begin,ready),commands,coordinates});
    }
    void flush(){
        if(!enabled_)return;
        if(FILE* f=std::fopen("SOURCE.BIN","wb")){
            unsigned h[8]={0x31435253,1,sizeof(Record),unsigned(rows_.size()),unsigned(run_supported_),cal_first_,cal_second_,unsigned(calibration_)};
            std::fwrite(h,sizeof(h),1,f);std::fwrite(rows_.data(),sizeof(Record),rows_.size(),f);std::fclose(f);
        }
    }
};
