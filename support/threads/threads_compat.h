#ifndef THREADS_COMPAT_H
#define THREADS_COMPAT_H

#ifndef NEED_C11_THREADS_WRAPPER

// we have C11 threads, so just use them...
#include <threads.h>

#else

#ifdef TARGET_MACOS

#include "threads_macos_compat_static.h"

#elif TARGET_WINDOWS

#include <c11/threads.h>

#else
#error "Missing C11 threads compatibility wrapper for target OS!"
#endif

#endif

#endif
