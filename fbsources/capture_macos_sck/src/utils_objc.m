#include "utils.h"

#include "threads_compat.h"

#include "utils_objc.h"

#define DEFAULT_WAIT_CHECK_INTERVAL_MILLIS (10)
#define NANOS_PER_MILLI (1000000)

@implementation Utils

+ (bool)waitWithTimeout:(uint32_t)timeout_millis completionCheck:(IntSupplier)checkComplete {
    return [self waitWithTimeout:timeout_millis everyMillis:DEFAULT_WAIT_CHECK_INTERVAL_MILLIS completionCheck:checkComplete];
}

+ (bool)waitWithTimeout:(uint32_t)timeout_millis everyMillis:(uint16_t)check_interval_millis completionCheck:(IntSupplier)checkComplete {
    mach_timespec_t deadline = get_system_timestamp_from_now(
        millis_as_mach_timespec(timeout_millis)
    );

    struct timespec check_interval = {
        .tv_sec = check_interval_millis / 1000,
        .tv_nsec = (check_interval_millis % 1000) * NANOS_PER_MILLI,
    };

    bool complete = false;
    while (!(complete = (checkComplete() != 0))) {
        struct timespec remaining = get_remaining_duration_until_system_timestamp(deadline);
        if (!is_strictly_positive_valid_timestamp(remaining)) {
            break;
        }

        if (check_interval_millis == 0) {
            thrd_yield();
        } else if (timespec_is_greater_than(&remaining, &check_interval)) {
            thrd_sleep(&check_interval, NULL);
        } else {
            thrd_sleep(&remaining, NULL);
        }
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