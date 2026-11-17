#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Optional QA input tape. The VM receives only the original digital key events.
// No game state, enemy health or timeline position is changed by playback.
struct InputReplay {
    struct Entry {uint32_t frames,buttons;};
    uint32_t seed=0,total=0,index=0,entry_end=0;
    std::vector<Entry> entries;
    bool loaded=false,done=false;
    void load() {
        FILE* file=std::fopen("REPLAY.DNM","rb");if(!file)return;
        uint32_t header[5];
        if(std::fread(header,sizeof(header),1,file)!=1 || header[0]!=0x524d4e44 || header[1]!=1 || header[3]>100000 || !header[3])std::abort();
        seed=header[2];total=header[4];entries.resize(header[3]);
        if(std::fread(entries.data(),sizeof(Entry),entries.size(),file)!=entries.size())std::abort();
        std::fclose(file);uint64_t sum=0;
        for(auto e:entries){if(!e.frames || e.buttons&~0x0000c0f0u)std::abort();sum+=e.frames;}
        if(sum!=total)std::abort();
        loaded=true;entry_end=entries[0].frames;
    }
    unsigned buttons(uint32_t tick) {
        if(!loaded)return 0;
        while(index<entries.size() && tick>=entry_end) {
            ++index;if(index<entries.size())entry_end+=entries[index].frames;
        }
        done=index>=entries.size();return done?0:entries[index].buttons;
    }
};
