#ifndef FBSOURCE_CAPTURE_SCK_UTILS_H
#define FBSOURCE_CAPTURE_SCK_UTILS_H

#include <stdio.h>

#include <time.h>
#include <mach/clock.h>

#define IS_APPROXIMATELY(value, expected, abs_tolerance) ( (value >= expected-abs_tolerance) && (value <= expected+abs_tolerance) )
#define OSTYPE_STRING(value) ((char[5]){(char)((value & 0xFF000000) >> 24),(char)((value & 0x00FF0000) >> 16),(char)((value & 0x0000FF00) >> 8),(char)(value & 0x000000FF),0})

/**
 * Allocates memory, zeroed.
 * @param size number of bytes to allocate
 * @return allocated memory, NULL on error
 */
void* zmalloc(size_t size);

/**
 * Returns the smaller of both values.
 *
 * Use only if either value is not directly available and requires computation.
 *
 * @param a first value
 * @param b second value
 * @return smaller value
 */
long min_long(long a, long b);

/**
 * Retrieves the current monotonic system clock timestamp.
 * @return current monotonic system clock timestamp
 */
mach_timespec_t get_system_timestamp();

/**
 * Retrieves a monotonic system clock timestamp offset
 * from current time by given offset.
 * @param offset offset to add to current time
 * @return offset monotonic system clock timestamp
 */
mach_timespec_t get_system_timestamp_from_now(mach_timespec_t offset);

/**
 * Calculates the remaining time until the given system timestamp target.
 *
 * Result must be checked with is_strictly_positive_valid_timestamp.
 * An "invalid", zero or negative timestamp will be returned if the given time
 * has been reached, either exactly or in the past.
 *
 * @param target timestamp to calculate remaining duration to
 * @return remaining time, "invalid" if target time has been reached
 */
struct timespec get_remaining_duration_until_system_timestamp(mach_timespec_t target);

/**
 * Checks if the given timestamp is valid and strictly positive (not zero).
 * @param timestamp timestamp to check
 * @return true if strictly positive; false if zero, negative or invalid
 */
bool is_strictly_positive_valid_timestamp(struct timespec timestamp);

/**
 * Converts the given milliseconds to a mach_timespec_t.
 * @param millis milliseconds
 * @return mach timespec representing given milliseconds
 */
mach_timespec_t millis_as_mach_timespec(uint32_t millis);

#endif //FBSOURCE_CAPTURE_SCK_UTILS_H
