#ifndef FBSOURCE_CAPTURE_WINRT_DXGI_UTILS_H
#define FBSOURCE_CAPTURE_WINRT_DXGI_UTILS_H

#include <stdio.h>

/**
 * Allocates memory, zeroed.
 * @param size number of bytes to allocate
 * @return allocated memory, NULL on error
 */
void* zmalloc(size_t size);

/**
 * Copies at most max_size - 1 characters from src, up until the first null termination.
 * @param src source string to copy from; may be NULL
 * @param max_size maximum size, incl. null termination
 * @return copied string; NULL on error
 */
char* copy_string(char *src, size_t max_size);

#endif //FBSOURCE_CAPTURE_WINRT_DXGI_UTILS_H
