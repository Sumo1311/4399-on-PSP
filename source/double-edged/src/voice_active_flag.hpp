#pragma once
#include <atomic>

// The mixer and script worker still serialize all voice mutations through the
// existing audio semaphore. Only this playing flag is read outside that lock.
// The mixer uses a cheap atomic read; the script acquires its current state.
class VoiceActiveFlag {
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> value_{false};
public:
    VoiceActiveFlag()=default;
    operator bool()const noexcept{return value_.load(std::memory_order_relaxed);}
    VoiceActiveFlag& operator=(bool value)noexcept{value_.store(value,std::memory_order_release);return *this;}
    bool playing()const noexcept{return value_.load(std::memory_order_acquire);}
};
