#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

// Reuse the existing, otherwise dormant detailed-profile buffer. The normal
// game only commits fixed-size RAM records; an emulator observer can drain them
// without debugger requests or memory-stick writes. Detailed PERF mode owns
// the original buffer exclusively and disables this ring.
struct DnmPresentationRecord {
    uint32_t flags,vcount;
    uint64_t time_us;
};
static_assert(sizeof(DnmPresentationRecord)==16);
struct DnmPresentationRing {
    uint32_t magic,count,capacity,address;
};
extern "C" {
__attribute__((used,externally_visible)) volatile DnmPresentationRing g_dnm_present_ring{};
__attribute__((used,externally_visible)) volatile uint32_t g_dnm_loading_address{};
}
namespace presentation_ring {
inline bool enabled=false;
inline uint32_t written=0;
inline constexpr uint32_t capacity=sizeof(perf::records)/sizeof(DnmPresentationRecord);
inline void init() {
    if(perf::enabled)return;
    g_dnm_present_ring.capacity=capacity;
    g_dnm_present_ring.address=uint32_t(reinterpret_cast<uintptr_t>(perf::records));
    g_dnm_present_ring.count=0;
    std::atomic_thread_fence(std::memory_order_release);
    g_dnm_present_ring.magic=0x31474e52; // RNG1
    enabled=true;
}
inline void loading_address(const void* loading) {
    g_dnm_loading_address=uint32_t(reinterpret_cast<uintptr_t>(loading));
}
inline void append(unsigned tick,unsigned subframe,bool fresh,unsigned moving,
                   unsigned vcount,uint64_t time_us) {
    if(!enabled)return;
    DnmPresentationRecord record{(tick<<3)|(unsigned(moving>0)<<2)|(subframe<<1)|unsigned(fresh),vcount,time_us};
    // Unsigned-character access changes object representation without typed
    // aliasing or extra storage. PERF mode never reads/writes this buffer here.
    auto* bytes=reinterpret_cast<unsigned char*>(perf::records);
    std::memcpy(bytes+(written%capacity)*sizeof(record),&record,sizeof(record));
    std::atomic_thread_fence(std::memory_order_release);
    g_dnm_present_ring.count=++written;
}
}
