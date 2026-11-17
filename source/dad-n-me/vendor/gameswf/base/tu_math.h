// tu_math.h	-- Willem Kokke

// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

// platform abstract math.h include file

#ifndef TU_MATH_H
#define TU_MATH_H

#include "base/tu_config.h"
#include <math.h>
#include <float.h>

// Modern Darwin and PSP libc provide the C99 single-precision functions.


// isfinite() comes with C99; fake version here in case compiler lacks it.
#ifndef isfinite
#define isfinitef(x) ((x) >= -FLT_MAX && (x) <= FLT_MAX)	// NAN should fail this, yes?
#define isfinited(x) ((x) >= -DBL_MAX && (x) <= DBL_MAX)
#endif // not isfinite


#endif // TU_MATH_H
