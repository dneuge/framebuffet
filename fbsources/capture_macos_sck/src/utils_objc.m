#include "utils.h"

#include "threads_compat.h"

#include "utils_objc.h"

@implementation Utils

+ (bool)waitWithTimeout:(uint32_t)timeout_millis completionCheck:(IntSupplier)checkComplete {
    mach_timespec_t deadline = get_system_timestamp_from_now(
        millis_as_mach_timespec(timeout_millis)
    );

    bool complete = false;
    while (!(complete = (checkComplete() != 0))) {
        struct timespec remaining = get_remaining_duration_until_system_timestamp(deadline);
        if (!is_strictly_positive_valid_timestamp(remaining)) {
            break;
        }



        // FIXME: sleep for check interval or remaining time
        thrd_yield();
    }

    return complete;
}

+ (char*)createCString:(NSString*)s {
    return [self createCString:s usingEncoding:NSASCIIStringEncoding];
}

+ (char*)createCString:(NSString*)s usingEncoding:(NSStringEncoding)encoding {
    if (!s) {
        return NULL;
    }

    NSData *data = [s dataUsingEncoding:encoding allowLossyConversion:TRUE];
    size_t data_length = [data length];
    if (!data_length) {
        return NULL;
    }

    size_t out_size = data_length+1;
    char *out = zmalloc(out_size);
    if (!out) {
        return false;
    }

    [data getBytes:out length:out_size];
    out[out_size-1] = 0;

    return out;
}

@end