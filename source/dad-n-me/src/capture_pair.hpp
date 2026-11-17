#pragma once
#include <cstdio>
#include "render_psp.hpp"

// Explicit image fixture only. Never installed in an ordinary game package.
class CapturePair {
    unsigned target=0;
    bool done[2]{false,false},valid[2]{false,false};
public:
    CapturePair(){if(FILE* f=std::fopen("CAPTURE60.QA","r")){std::fscanf(f,"%u",&target);std::fclose(f);}}
    void capture(unsigned tick,unsigned subframe){
        if(!target||tick!=target||done[subframe])return;
        valid[subframe]=dump_psp_frame(subframe?"EXACT60.RGBA":"MID60.RGBA");done[subframe]=true;
        if(done[0]&&done[1])if(FILE* f=std::fopen("CAPTURE60.JSON","w")){
            std::fprintf(f,"{\"scope\":\"Explicit native GE image-copy fixture; excluded from FPS measurements\",\"source_tick\":%u,\"midpoint\":%s,\"exact\":%s}\n",tick,valid[0]?"true":"false",valid[1]?"true":"false");std::fclose(f);
        }
    }
};
