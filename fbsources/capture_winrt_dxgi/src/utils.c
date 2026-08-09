#include "utils.h"

#include <stdlib.h>
#include <string.h>

void* zmalloc(size_t size) {
    void *out = malloc(size);
    if (!out) {
        return NULL;
    }

    memset(out, 0, size);

    return out;
}

char* copy_string(char *src, size_t max_size) {
    if (!src || (max_size < 1)) {
        return NULL;
    }

    size_t len = strnlen(src, max_size-1);
    char *out = zmalloc(len+1);
    if (!out) {
        return NULL;
    }

    memcpy(out, src, len);

    return out;
}