#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Opt-in RAM telemetry. No target file writes, input injection or clock changes.
struct DnmResponseRecord {
    uint32_t tick,buttons,queued,subframe,fresh,phase;
    uint32_t logic_us,capture_us,draw_us,submit_us,worker_wait_us,source_tick_now;
    uint64_t sampled_us,published_us,presented_us;
};
static_assert(sizeof(DnmResponseRecord)==72);
struct DnmResponseDescriptor { uint32_t magic,count,capacity,address; };
extern "C" {
__attribute__((used,externally_visible)) volatile DnmResponseDescriptor g_dnm_response_ring{};
}
namespace response_metrics {
inline bool enabled=false;
inline unsigned written=0;
inline DnmResponseRecord records[512];
inline void init() {
    if(FILE* f=std::fopen("RESPONSE.QA","r")){enabled=true;std::fclose(f);}
    if(!enabled)return;
    g_dnm_response_ring.capacity=512;
    g_dnm_response_ring.address=uint32_t(reinterpret_cast<uintptr_t>(records));
    g_dnm_response_ring.count=0;
    std::atomic_thread_fence(std::memory_order_release);
    g_dnm_response_ring.magic=0x31505352; // RSP1
}
inline uint64_t now(){return enabled?sceKernelGetSystemTimeWide():0;}
inline void append(const DnmResponseRecord& row){
    if(!enabled)return;
    records[written%512]=row;
    std::atomic_thread_fence(std::memory_order_release);
    g_dnm_response_ring.count=++written;
}
}
