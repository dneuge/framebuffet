#include <stdlib.h>
#include <string.h>

#include <mach/mach.h>

#include "utils.h"

#define NANOS_PER_MILLI (1000000)

void* zmalloc(size_t size) {
    void *out = malloc(size);
    if (!out) {
        return NULL;
    }

    memset(out, 0, size);

    return out;
}

long min_long(long a, long b) {
    return (a<b) ? a : b;
}

mach_timespec_t get_system_timestamp() {
    // NOTE: cargo cult from https://stackoverflow.com/questions/11680461/monotonic-clock-on-osx
    //       only signatures/headers are available from Apple
    //       there does not appear to be any documentation for these functions?

    mach_timespec_t out = {0,};
    clock_serv_t system_clock = {0,};

    // TODO: there are undocumented return values, probably success indicators?
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &system_clock);
    clock_get_time(system_clock, &out);
    mach_port_deallocate(mach_task_self(), system_clock);

    return out;
}

mach_timespec_t get_system_timestamp_from_now(mach_timespec_t offset) {
    mach_timespec_t out = get_system_timestamp();
    ADD_MACH_TIMESPEC(&out, &offset);
    return out;
}

static bool is_valid_timestamp(struct timespec *timestamp) {
    if (timestamp->tv_nsec < 0 || timestamp->tv_nsec >= 1000000000) {
        return false;
    }

    return true;
}

bool timespec_is_greater_than(struct timespec *a, struct timespec *b) {
    if (a->tv_sec > b->tv_sec) {
        return true;
    }

    if (a->tv_sec < b->tv_sec) {
        return false;
    }

    return a->tv_nsec > b->tv_nsec;
}

bool is_strictly_positive_valid_timestamp(struct timespec timestamp) {
    if (!is_valid_timestamp(&timestamp)) {
        return false;
    }

    if (timestamp.tv_sec > 0) {
        return true;
    }

    if (timestamp.tv_sec < 0) {
        return false;
    }

    return (timestamp.tv_nsec > 0);
}

struct timespec get_remaining_duration_until_system_timestamp(mach_timespec_t target) {
    mach_timespec_t remaining = get_system_timestamp();
    SUB_MACH_TIMESPEC(&remaining, &target);
    return (struct timespec) {
        .tv_sec = remaining.tv_sec,
        .tv_nsec = remaining.tv_nsec,
    };
}

mach_timespec_t millis_as_mach_timespec(uint32_t millis) {
    return (mach_timespec_t){
        .tv_sec = millis / 1000,
        .tv_nsec = (clock_res_t) ((millis % 1000) * NANOS_PER_MILLI),
    };
}
