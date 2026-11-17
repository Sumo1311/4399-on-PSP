#include "base/tu_types.h"
#include "base/tu_loadlib.h"
#include "base/tu_timer.h"
#include <cstdlib>
#include <new>
#include <sys/time.h>
#include <unistd.h>

// Use the platform heap so engine allocations stay within PSP's heap limit.
extern "C" void* dnm_engine_malloc(size_t n) {
    for(;;) {
        void* p=std::malloc(n);if(p || !n)return p;
        if(auto handler=std::get_new_handler())handler();else throw std::bad_alloc();
    }
}
extern "C" void* dnm_engine_realloc(void* p, size_t n) {
    for(;;) {
        void* resized=std::realloc(p,n);if(resized || !n)return resized;
        if(auto handler=std::get_new_handler())handler();else throw std::bad_alloc();
    }
}
extern "C" void dlfree(void* p) { std::free(p); }

// The packaged game has no external modules.
tu_loadlib::tu_loadlib(const char*) : m_hlib(nullptr) {}
tu_loadlib::~tu_loadlib() {}
void* tu_loadlib::get_function(const char*) { return nullptr; }

static uint64 start_us;
static uint64 micros() {
    timeval tv{}; gettimeofday(&tv,nullptr);
    return uint64(tv.tv_sec)*1000000+tv.tv_usec;
}
void tu_timer::init_timer() { start_us=micros(); }
Uint32 tu_timer::get_ticks() { return Uint32((micros()-start_us)/1000); }
Uint64 tu_timer::get_systime() { return micros()/1000000; }
void tu_timer::sleep(int ms) { usleep(ms*1000); }
uint64 tu_timer::get_profile_ticks() { return micros(); }
double tu_timer::profile_ticks_to_seconds(uint64 t) { return double(t)/1000000; }
double tu_timer::profile_ticks_to_milliseconds(uint64 t) { return double(t)/1000; }

tu_datetime::tu_datetime() : m_time(time(nullptr)) {}
double tu_datetime::get_time() const { return double(m_time)*1000; }
void tu_datetime::set_time(double t) { m_time=time_t(t/1000); }
int tu_datetime::get(part p) {
    tm v=*localtime(&m_time);
    switch(p) {
    case FULLYEAR: return v.tm_year+1900; case YEAR: return v.tm_year;
    case MON: return v.tm_mon; case MDAY: return v.tm_mday; case WDAY: return v.tm_wday;
    case HOUR: return v.tm_hour; case MIN: return v.tm_min; case SEC: return v.tm_sec;
    default: return -1;
    }
}
void tu_datetime::set(part p,int value) {
    tm v=*localtime(&m_time);
    switch(p) {
    case FULLYEAR: v.tm_year=value-1900; break; case YEAR: v.tm_year=value; break;
    case MON: v.tm_mon=value; break; case MDAY: v.tm_mday=value; break;
    case HOUR: v.tm_hour=value; break; case MIN: v.tm_min=value; break;
    case SEC: v.tm_sec=value; break; default: return;
    }
    m_time=mktime(&v);
}

// Optional native frontend hook; host/reference runtimes leave it unset.
namespace { void (*game_loading_callback)()=nullptr; }
extern "C" void dnm_set_game_loading_callback(void (*callback)()){game_loading_callback=callback;}
extern "C" void dnm_enter_game_loading(){if(game_loading_callback)game_loading_callback();}
