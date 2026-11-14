#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbutil.h"

#define FBUTIL_MAX_THREAD_NAME_SIZE 16 /* limited by pthread */

__attribute__((__format__ (__printf__, 2, 0)))
static char* allocate_format_string(size_t size, const char *format, va_list args) {
    char *out = malloc(size);
    if (!out) {
        return NULL;
    }
    
    memset(out, 0, size);
    vsnprintf(out, size, format, args);
    out[size-1] = 0;

    return out;
}

static void set_current_thread_name(char *name);

#ifdef HAVE_PTHREAD
#include <pthread.h>

static void set_current_thread_name(char *name) {
    pthread_setname_np(pthread_self(), name);
}

#else
static void set_current_thread_name(char *name) {
    // do nothing on unsupported platforms
}
#endif

__attribute__((__format__ (__printf__, 1, 2)))
void fbutil_set_current_thread_name(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fbutil_set_current_thread_name_va(format, args);
    va_end(args);
}

__attribute__((__format__ (__printf__, 1, 0)))
void fbutil_set_current_thread_name_va(const char *format, va_list args) {
    char *name = allocate_format_string(FBUTIL_MAX_THREAD_NAME_SIZE, format, args);
    if (!name) {
        return;
    }

    set_current_thread_name(name);

    free(name);
}

