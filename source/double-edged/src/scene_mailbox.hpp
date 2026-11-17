#pragma once
#include <array>
#include <atomic>
#include <cassert>

// One producer,one consumer; at most one pending immutable snapshot. Publishing
// replaces only an unclaimed snapshot. Claimed GPU data remains owned by the
// consumer until its existing synchronous submission has finished.
template<unsigned Slots>
class SceneMailbox {
    static_assert(Slots>=3);
    enum State {Free,Writing,Ready,Reading};
    std::array<std::atomic<unsigned>,Slots> states_{};
    std::atomic<int> pending_{-1};
public:
    std::atomic<unsigned> published{0},replaced{0};
    SceneMailbox(){for(auto& state:states_)state.store(Free,std::memory_order_relaxed);}
    int acquire(){
        for(unsigned i=0;i<Slots;++i){unsigned expected=Free;if(states_[i].compare_exchange_strong(expected,Writing,std::memory_order_acquire,std::memory_order_relaxed))return int(i);}
        return -1;
    }
    void publish(unsigned slot){
        assert(slot<Slots&&states_[slot].load(std::memory_order_relaxed)==Writing);
        states_[slot].store(Ready,std::memory_order_relaxed);
        int old=pending_.exchange(int(slot),std::memory_order_acq_rel);
        if(old>=0){states_[unsigned(old)].store(Free,std::memory_order_release);replaced.fetch_add(1,std::memory_order_relaxed);}
        published.fetch_add(1,std::memory_order_relaxed);
    }
    void cancel(unsigned slot){assert(slot<Slots&&states_[slot].load(std::memory_order_relaxed)==Writing);states_[slot].store(Free,std::memory_order_release);}
    int take(){
        int slot=pending_.exchange(-1,std::memory_order_acq_rel);
        if(slot>=0){assert(states_[unsigned(slot)].load(std::memory_order_relaxed)==Ready);states_[unsigned(slot)].store(Reading,std::memory_order_relaxed);}
        return slot;
    }
    void release(unsigned slot){
        assert(slot<Slots&&states_[slot].load(std::memory_order_relaxed)==Reading);
        states_[slot].store(Free,std::memory_order_release);
    }
    bool pending()const{return pending_.load(std::memory_order_acquire)>=0;}
};
