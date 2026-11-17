#pragma once
#include <cmath>
namespace gameswf {
inline float script_round_twip(float value) {
    if(!std::isfinite(value))return 0.f;
#if defined(PSP) && defined(__mips__)
    // Above 2^23 every finite float already represents an integer. Guard the
    // integer conversion's range, and keep the input sign when it rounds to 0.
    if(std::fabs(value)>=8388608.f)return value;
    float rounded;
    asm volatile("cvt.w.s %0,%1\n\tcvt.s.w %0,%0" : "=&f"(rounded) : "f"(value));
    return std::copysign(rounded,value);
#else
    return std::nearbyintf(value);
#endif
}
}
