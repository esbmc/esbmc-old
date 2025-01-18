
#pragma once

#include "cstddef"

/* Support LLVM's libc++, which defines the float and long double overloads
 * in the top-level :: namespace in <math.h> when __cplusplus is set. These
 * clash with our definitions due to the exception declaration. */
#pragma push_macro("__cplusplus")
#undef __cplusplus
extern "C"
{
#include_next <math.h>
}
#pragma pop_macro("__cplusplus") // restore macro

#include <cmath>
