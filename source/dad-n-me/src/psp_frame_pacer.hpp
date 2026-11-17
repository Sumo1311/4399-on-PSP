#pragma once
#include <cstdint>
#include <pspdisplay.h>

// Compute immediately after the previous swap, then present on the second
// vblank. Starting work on a separate 30 Hz clock gave only half a refresh
// period of CPU headroom and alternated 16/50 ms swaps as work crossed it.
// A late frame uses the next vblank; no source tick or drawing is skipped.
class PspFramePacer {
    std::uint32_t last_=0;
    bool primed_=false;
public:
    void wait() {
        auto current=std::uint32_t(sceDisplayGetVcount());
        auto target=primed_?last_+1:current+1;
        for(;;) {
            current=std::uint32_t(sceDisplayGetVcount());
            if(std::int32_t(current-target)<0){sceDisplayWaitVblankStart();continue;}
            if(current==target && sceDisplayIsVblank())break;
            target=current+1;
        }
        last_=std::uint32_t(sceDisplayGetVcount());primed_=true;
    }
};
