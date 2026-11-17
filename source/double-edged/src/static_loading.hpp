#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>

// One source update owns a loading transaction. The consumer drains all older
// scenes, presents the static card, confirms the displayed buffer, then ACKs.
// The producer cannot execute the initialization before that ACK.
struct StaticLoading {
    struct Row {
        unsigned id=0,tick=0,reason=0,shown_vcount=0,resume_vcount=0;
        uint64_t requested_us=0,shown_us=0,begin_us=0,ready_us=0,submitted_us=0,resumed_us=0;
        bool visible=false;
    };
    static constexpr unsigned Capacity=8;
    Row rows[Capacity]{};
    std::atomic<unsigned> requested{0},acknowledged{0},completed{0};
    std::atomic<bool> screen_active{false};
    std::atomic<unsigned> resume_buttons{0};
    unsigned worker_id=0;
    bool capture=false,log=false;
    // Presentation samples the final loading-window pad immediately before
    // revealing gameplay. The source consumes these bits before its next
    // edge calculation; a released key re-arms through LoadingInputGate.
    void record_resume_buttons(unsigned buttons){if(buttons)resume_buttons.fetch_or(buttons,std::memory_order_release);}
    unsigned take_resume_buttons(){return resume_buttons.load(std::memory_order_relaxed)?resume_buttons.exchange(0,std::memory_order_acquire):0;}
    Row& row(unsigned id){return rows[(id-1)%Capacity];}
    const Row& row(unsigned id)const{return rows[(id-1)%Capacity];}
    unsigned request(unsigned tick,unsigned reason,uint64_t now) {
        if(worker_id)return worker_id;
        unsigned id=requested.load(std::memory_order_relaxed)+1;
        row(id)={};row(id).id=id;row(id).tick=tick;row(id).reason=reason;row(id).requested_us=now;
        worker_id=id;requested.store(id,std::memory_order_release);return id;
    }
    bool pending()const{return requested.load(std::memory_order_acquire)>acknowledged.load(std::memory_order_acquire);}
    void shown(unsigned id,uint64_t now,unsigned vcount,bool visible) {
        auto& r=row(id);r.shown_us=now;r.shown_vcount=vcount;r.visible=visible;
        if(visible){screen_active.store(true,std::memory_order_release);acknowledged.store(id,std::memory_order_release);}
    }
    void began(uint64_t now){if(worker_id)row(worker_id).begin_us=now;}
    unsigned ready(uint64_t now) {
        unsigned id=worker_id;if(id){row(id).ready_us=now;completed.store(id,std::memory_order_release);worker_id=0;}return id;
    }
    void submitted(unsigned id,uint64_t now){row(id).submitted_us=now;}
    void resumed(unsigned id,uint64_t now,unsigned vcount){screen_active.store(false,std::memory_order_release);row(id).resumed_us=now;row(id).resume_vcount=vcount;if(log)write(id);}
    void write(unsigned id)const {
        FILE* f=std::fopen("LOADING.CSV",id==1?"w":"a");if(!f)return;
        if(id==1)std::fputs("id,source_tick,reason,requested_us,shown_us,visible,shown_vcount,init_begin_us,ready_us,first_submit_us,resumed_us,resume_vcount\n",f);
        {auto& r=row(id);std::fprintf(f,"%u,%u,%u,%llu,%llu,%u,%u,%llu,%llu,%llu,%llu,%u\n",r.id,r.tick,r.reason,(unsigned long long)r.requested_us,(unsigned long long)r.shown_us,unsigned(r.visible),r.shown_vcount,(unsigned long long)r.begin_us,(unsigned long long)r.ready_us,(unsigned long long)r.submitted_us,(unsigned long long)r.resumed_us,r.resume_vcount);}
        std::fclose(f);
    }
};

// Inputs seen before/during loading are suppressed until individually released.
// Fresh presses after loading remain usable; analog movement re-arms at center.
struct LoadingInputGate {
    unsigned held=0;bool analog=false;
    void block(unsigned buttons,unsigned x,unsigned y){held|=buttons;analog|=x<98||x>158||y<98||y>158;}
    unsigned filter(unsigned buttons){held&=buttons;return buttons&~held;}
    void axes(unsigned& x,unsigned& y){if(analog){if(x>=98&&x<=158&&y>=98&&y<=158)analog=false;else x=y=128;}}
};
