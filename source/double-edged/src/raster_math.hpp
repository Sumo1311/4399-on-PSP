#pragma once
#include <cmath>
namespace glorton {
inline float raster_floor(float value) {
#if defined(PSP) && defined(__mips__)
    if(std::isfinite(value) && std::fabs(value)<8388608.f) {
        float result;
        asm volatile("floor.w.s %0,%1\n\tcvt.s.w %0,%0":"=&f"(result):"f"(value));
        return std::copysign(result,value);
    }
#endif
    return std::floor(value);
}
inline float raster_ceil(float value) {
#if defined(PSP) && defined(__mips__)
    if(std::isfinite(value) && std::fabs(value)<8388608.f) {
        float result;
        asm volatile("ceil.w.s %0,%1\n\tcvt.s.w %0,%0":"=&f"(result):"f"(value));
        return std::copysign(result,value);
    }
#endif
    return std::ceil(value);
}
}
