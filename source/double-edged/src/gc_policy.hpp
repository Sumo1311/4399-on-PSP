#pragma once
#include <cstddef>

// Same full collector,less frequent forced scans. A small tracked-growth bound
// complements the VM's own allocation-triggered GC; a120-tick ceiling prevents
// indefinite deferral. No gameplay object is pinned or excluded from scans.
class SourceGcPolicy {
    std::size_t floor_=0;
    unsigned last_=0;
public:
    static constexpr std::size_t Growth=256*1024;
    static constexpr unsigned MinimumTicks=30,MaximumTicks=120;
    void reset(unsigned tick,std::size_t bytes){last_=tick;floor_=bytes;}
    unsigned reason(unsigned tick,std::size_t bytes){
        if(bytes<floor_)floor_=bytes;
        if(tick-last_<MinimumTicks)return 0;
        if(bytes-floor_>=Growth)return 1;
        return tick-last_>=MaximumTicks?2:0;
    }
};
