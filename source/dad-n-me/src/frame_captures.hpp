#pragma once
#include <vector>
#include <algorithm>
#include <cstdio>
bool dump_psp_frame(const char*);
struct FrameCaptures {
    std::vector<unsigned> ticks;
    FrameCaptures(){if(FILE* f=std::fopen("FRAMES.QA","r")){unsigned tick;while(std::fscanf(f,"%u",&tick)==1)ticks.push_back(tick);std::fclose(f);std::sort(ticks.begin(),ticks.end());}}
    void capture(unsigned tick){if(std::binary_search(ticks.begin(),ticks.end(),tick)){char path[64];std::snprintf(path,sizeof(path),"FRAME-%u.RGBA",tick);dump_psp_frame(path);}}
};
