#ifndef FRAMEBUFFET_UTIL_H
#define FRAMEBUFFET_UTIL_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void fbutil_set_current_thread_name(char *format, ...);
void fbutil_set_current_thread_name_va(char *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif
