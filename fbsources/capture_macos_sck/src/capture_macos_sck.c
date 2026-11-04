#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include "threads_compat.h"

#include "../../../framework/src/framebuffet.h"
#include "../../../framework/src/fbutil.h"

#include "utils.h"

#include "sck_capture.h"

#define FBSOURCE_SCK_SEARCH_TIMEOUT_MILLIS (30000)
#define FBSOURCE_SCK_SETUP_TIMEOUT_MILLIS  (30000)
#define FBSOURCE_SCK_START_TIMEOUT_MILLIS  (30000)
#define FBSOURCE_SCK_STOP_TIMEOUT_MILLIS   (30000)

#define X_BYTES_PER_PIXEL (4) /* API always uses 32 bits per pixel; this does not seem to be flexible */

#define ARGB32_RED_MASK    (0x00FF0000)
#define ARGB32_RED_SHIFT   (16)
#define ARGB32_GREEN_MASK  (0x0000FF00)
#define ARGB32_GREEN_SHIFT (8)
#define ARGB32_BLUE_MASK   (0x000000FF)
#define ARGB32_BLUE_SHIFT  (0)

#define ABS_CONTENT_CHANGE_TOLERANCE (0.1)

typedef struct {
    mtx_t mutex;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;
  
    bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    char *window_search_name_exact;

    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;

    unsigned char *buffers[2];
    int complete_buffer;

    cnd_t capture_status_cnd;
    bool captured_frame;
    bool want_reinit;
    bool want_abort;

    // only to be handled by generator thread: (not protected by mutex)
    fbsource_sck_capture_handler_t handler;
    double expected_width;
    double expected_height;
} fbsource_capture_macos_sck_t;

#define NANOS_PER_MILLISECOND (1000000)
#define CONDITIONAL_WAKEUP_INTERVAL_SECONDS (2)

static const struct timespec active_check_sleep = {
    .tv_sec = 0,
    .tv_nsec = 200 * NANOS_PER_MILLISECOND
};

static const struct timespec wait_for_window = {
    .tv_sec = 2,
    .tv_nsec = 0
};

static const struct timespec wait_after_error = {
    .tv_sec = 1,
    .tv_nsec = 0
};

static bool set_now_plus_seconds(struct timespec *out, uint16_t seconds) {
    if (!out) {
        return false;
    }

    struct timespec now = {0,};
    int res = timespec_get(&now, TIME_UTC);
    if (res == 0 || res != TIME_UTC) {
        return false;
    }

    out->tv_sec = now.tv_sec + seconds;
    out->tv_nsec = now.tv_nsec;

    return true;
}

static bool generator_reset(fbsource_capture_macos_sck_t *instance, char *reason) {
    FBLOG_DEBUG(instance->ext_instance_ref, "resetting for %s", reason);

    if (!stop_capture(&instance->handler, FBSOURCE_SCK_STOP_TIMEOUT_MILLIS)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to stop capture on reset for %s", reason);
        // FIXME: abort module instance properly
        // FIXME: try stopping again?
        return false;
    }

    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers 0");
    framebuffet_publish_num_buffers(instance->ext_instance_ref, 0);
    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers complete");

    if (mtx_lock(&instance->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex before %s; resources will not be cleaned up", reason);
        return false;
    }
    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: locked mutex");
        
    // release all resources we still hold and reset data
    instance->width = 0;
    instance->height = 0;
    instance->buffer_size = 0;
    
    if (instance->buffers[0]) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing first buffer for %s", reason);
        free(instance->buffers[0]);
        instance->buffers[0] = NULL;
    }
    
    if (instance->buffers[1]) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing second buffer for %s", reason);
        free(instance->buffers[1]);
        instance->buffers[1] = NULL;
    }

    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: unlocking mutex");
    mtx_unlock(&instance->mutex);

    FBLOG_DEBUG(instance->ext_instance_ref, "reset for %s complete", reason);

    return true;
}

static inline double clock2ms(clock_t c) {
    return ((double) c) / CLOCKS_PER_SEC * 1000.0;
}

static uint8_t calc_shift(uint32_t mask) {
    if (!mask) {
        return 0;
    }

    uint8_t shift = 0;
    while (!(mask & 0x1)) {
        mask = mask >> 1;
        shift++;
    }

    return shift;
}

static inline void raise_reinit_flag(fbsource_capture_macos_sck_t *instance) {
    instance->want_reinit = true;
    cnd_broadcast(&instance->capture_status_cnd);
}

static inline void raise_abort_flag(fbsource_capture_macos_sck_t *instance) {
    instance->want_abort = true;
    cnd_broadcast(&instance->capture_status_cnd);
}

static inline void raise_captured_frame_flag(fbsource_capture_macos_sck_t *instance) {
    instance->captured_frame = true;
}

static void on_frame_received(void *ref, fbsource_sck_frame_t *frame) {
    if (!ref) {
        return;
    }

    fbsource_capture_macos_sck_t *instance = ref;
    if (instance->stop_generator) {
        return;
    }

    clock_t benchStart = clock(); // DEBUG

    if (frame->pixel_format != FBSOURCE_SCK_PIXEL_FORMAT_32BGRA) {
        FBLOG_WARN(
            instance->ext_instance_ref, "unsupported pixel format; module constant %" PRIu8,
            frame->pixel_format
        );
        raise_reinit_flag(instance);
        return;
    }

    if (frame->bytes_per_pixel != 4) {
        FBLOG_WARN(instance->ext_instance_ref, "unsupported number of bytes per pixel, need 4 bytes (32 bit), got %zu (%zu bit)", frame->bytes_per_pixel, frame->bytes_per_pixel*8);
        raise_abort_flag(instance);
        return;
    }

    if (!IS_APPROXIMATELY(frame->content_width, instance->expected_width, ABS_CONTENT_CHANGE_TOLERANCE) || !IS_APPROXIMATELY(frame->content_height, instance->expected_height, ABS_CONTENT_CHANGE_TOLERANCE)) {
        FBLOG_INFO(
            instance->ext_instance_ref, "window size changed, width %.1f => %.1f, height %.1f => %.1f",
            instance->expected_width, frame->content_width,
            instance->expected_height, frame->content_height
        );
        raise_reinit_flag(instance);
        return;
    }

    long content_offset_x = lround(frame->content_offset_x);
    long content_offset_y = lround(frame->content_offset_y);
    long content_width = min_long(lround(frame->content_width), instance->width);
    long content_height = min_long(lround(frame->content_height), instance->height);
    if (content_offset_x < 0 || content_offset_y < 0 || content_width < 1 || content_height < 1) {
        FBLOG_WARN(
            instance->ext_instance_ref, "invalid content box: offset_x %.1f => %ld, offset_y %.1f => %ld, width %.1f => %ld, height %.1f => %ld",
            frame->content_offset_x, content_offset_x, frame->content_offset_y, content_offset_y,
            frame->content_width, content_width, frame->content_height, content_height
        );
        raise_reinit_flag(instance);
    }

    long minimum_frame_width = (content_offset_x + content_width);
    long minimum_frame_height = (content_offset_y + content_height);
    size_t minimum_bytes_per_row = minimum_frame_width * frame->bytes_per_pixel;
    size_t minimum_bytes_total = minimum_frame_height * frame->bytes_per_row;
    if (frame->bytes_per_row < minimum_bytes_per_row || frame->buffer_size < minimum_bytes_total) {
        FBLOG_WARN(
            instance->ext_instance_ref, "frame has insufficient buffer size: "
            "buffer_size=%lu, bytes_per_pixel=%lu, bytes_per_row=%lu, minimum_frame_width=%ld, "
            "minimum_frame_height=%ld, minimum_bytes_per_row=%lu, minimum_total_bytes=%lu",
            frame->buffer_size, frame->bytes_per_pixel, frame->bytes_per_row,
            minimum_frame_width, minimum_frame_height, minimum_bytes_per_row, minimum_bytes_total
        );
        raise_reinit_flag(instance);
        return;
    }

    clock_t benchGotImage = clock(); // DEBUG

    if (frame->bytes_per_row % frame->bytes_per_pixel != 0) {
        FBLOG_WARN(
            instance->ext_instance_ref,
            "number of bytes per row is not aligned with bytes per pixel: bytes_per_row=%zu, bytes_per_pixel=%zu",
            frame->bytes_per_row, frame->bytes_per_pixel
        );
        raise_reinit_flag(instance);
        return;
    }
    uint32_t frame_pixels_per_row = frame->bytes_per_row / frame->bytes_per_pixel;

    int work_buffer = (instance->complete_buffer + 1) % 2;
    uint32_t *in = (uint32_t*) frame->buffer;
    in += content_offset_x + (content_offset_y * frame_pixels_per_row);
    unsigned char *out = instance->buffers[work_buffer];

    const uint32_t in_skip_pixels = (frame_pixels_per_row - instance->width);
    const uint32_t wanted_width = instance->width;
    const uint32_t *in_end = in + (instance->height * frame_pixels_per_row);
    uint32_t *in_end_of_wanted_row = in + wanted_width;
    while (in < in_end) {
        const uint32_t in_val = *in;
        *(out++) = (in_val & ARGB32_RED_MASK) >> ARGB32_RED_SHIFT;
        *(out++) = (in_val & ARGB32_GREEN_MASK) >> ARGB32_GREEN_SHIFT;
        *(out++) = (in_val & ARGB32_BLUE_MASK) >> ARGB32_BLUE_SHIFT;
        in++;

        if (in >= in_end_of_wanted_row) { // TODO: annotate "unlikely"?
            in += in_skip_pixels;
            in_end_of_wanted_row = in + wanted_width;
        }
    }
    clock_t benchConverted = clock(); // DEBUG

    // mark buffer as complete
    if (mtx_lock(&instance->mutex) != thrd_success) {
        // just drop the frame if we cannot get hold of our mutex
        FBLOG_WARN(instance->ext_instance_ref, "dropping frame because mutex failed to lock");
        return;
    }

    instance->complete_buffer = work_buffer;
    instance->frame_token++;

    mtx_unlock(&instance->mutex);
    clock_t benchUnlocked = clock(); // DEBUG

    raise_captured_frame_flag(instance);

    framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);

    clock_t benchDone = clock(); // DEBUG

    if (FBLOG_IS_TRACE_ENABLED(instance->ext_instance_ref)) {
        FBLOG_TRACE(instance->ext_instance_ref, "benchmark: total %5.2fms = %5.1f FPS | grab %5.2f, convert %5.2f, transfer %.2f, publish %4.2f", clock2ms(benchDone-benchStart), 1000.0/clock2ms(benchDone-benchStart), clock2ms(benchGotImage-benchStart), clock2ms(benchConverted-benchGotImage), clock2ms(benchUnlocked-benchConverted), clock2ms(benchDone-benchUnlocked));
    }
}

static void on_error(void *ref, sck_error_recommendation_t recommendation) {
    if (!ref) {
        return;
    }

    fbsource_capture_macos_sck_t *instance = ref;
    if (instance->stop_generator) {
        return;
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "error notification: recommendation=%" PRIu8, recommendation);
    if (recommendation == FBSOURCE_SCK_ERROR_RECOMMEND_REINIT) {
        raise_reinit_flag(instance);
    } else if (recommendation == FBSOURCE_SCK_ERROR_RECOMMEND_ABORT) {
        raise_abort_flag(instance);
    } else {
        FBLOG_WARN(instance->ext_instance_ref, "unhandled error recommendation %" PRIu8 ", aborting", recommendation);
        raise_abort_flag(instance);
    }
}

static int generate(void *data) {
    fbsource_capture_macos_sck_t *instance = data;

    window_parameters_t window_params = INVALID_WINDOW_PARAMETERS;
    unsigned int width = 0;
    unsigned int height = 0;

    unsigned char *buffers[2];
    unsigned long buffer_size = 0;

    int res;

    bool success;

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");

    fbutil_set_current_thread_name("FBCapSCK/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));

    // outer loop is used to reinitialize on source reconfiguration
    while (!instance->stop_generator) {
        // window may not be immediately available, wait until it appears
        success = false;
        while (!instance->stop_generator) {
            FBLOG_TRACE(instance->ext_instance_ref, "searching window");

            window_list_item_t *window_list = list_shareable_windows(FBSOURCE_SCK_SEARCH_TIMEOUT_MILLIS);
            if (!window_list) {
                FBLOG_WARN(instance->ext_instance_ref, "system returned no shareable windows");
            }
            window_list_item_t *found = NULL;
            for (window_list_item_t *item = window_list; item; item = item->next) {
                FBLOG_TRACE(instance->ext_instance_ref, "window %" PRIu32 " %.1fx%.1f \"%s\"", item->params.id, item->params.width, item->params.height, item->title);
                if (!item->title || item->params.width < 0.9 || item->params.height < 0.9) {
                    continue;
                }

                if (strcmp(item->title, instance->window_search_name_exact) == 0) {
                    found = item;
                    break;
                }
            }
            if (found) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window found: %" PRIu32 " %.1fx%.1f \"%s\"", found->params.id, found->params.width, found->params.height, found->title);
                window_params = found->params;
                width = lround(window_params.width);
                height = lround(window_params.height);
                success = (width > 0) && (height > 0);
                if (!success) {
                    FBLOG_WARN(instance->ext_instance_ref, "bad window geometry: %" PRIu32 " %.1fx%.1f => %ux%u", found->params.id, found->params.width, found->params.height, width, height);
                }
            }
            free_window_list(window_list);
            window_list = NULL;

            if (success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window geometry: width=%d, height=%d", width, height);
                break;
            }

            FBLOG_TRACE(instance->ext_instance_ref, "window has not been found yet, waiting...");
            thrd_sleep(&wait_for_window, NULL);
        }

        if (!success) {
            // configuration loop aborted without completing
            // this should only happen when the thread should terminate, so abort as well
            FBLOG_DEBUG(instance->ext_instance_ref, "search loop aborted, terminating");
            break;
        }

        FBLOG_DEBUG(instance->ext_instance_ref, "worker allocating buffers");
        buffer_size = 3 * width * height;
        buffers[0] = zmalloc(buffer_size);
        buffers[1] = zmalloc(buffer_size);
        if (!buffers[0] || !buffers[1]) {
            // out of memory (or something like that); there's nothing we can do but terminate
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate buffers, terminating");
            break;
        }

        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to lock instance mutex, terminating");
            break;
        }

        instance->width = width;
        instance->height = height;
        instance->expected_width = window_params.width;
        instance->expected_height = window_params.height;
        instance->buffer_size = buffer_size;
        instance->buffers[0] = buffers[0];
        instance->buffers[1] = buffers[1];
        instance->captured_frame = false;
        instance->want_abort = false;
        instance->want_reinit = false;

        mtx_unlock(&instance->mutex);

        FBLOG_INFO(instance->ext_instance_ref, "window acquired, setting up capture");
        instance->handler = setup_capture(
            instance->ext_instance_ref, window_params, FBSOURCE_SCK_SETUP_TIMEOUT_MILLIS,
            on_frame_received, instance,
            on_error, instance
        );
        if (!instance->handler) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to setup capture handler");

            if (mtx_lock(&instance->mutex) != thrd_success) {
                FBLOG_WARN(instance->ext_instance_ref, "failed to lock instance mutex, terminating");
                break;
            }

            instance->width = 0;
            instance->height = 0;
            instance->buffer_size = 0;

            free(instance->buffers[0]);
            instance->buffers[0] = NULL;

            free(instance->buffers[1]);
            instance->buffers[1] = NULL;

            mtx_unlock(&instance->mutex);

            continue;
        }

        FBLOG_INFO(instance->ext_instance_ref, "starting capture");
        if (!start_capture(instance->handler, FBSOURCE_SCK_START_TIMEOUT_MILLIS)) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to start capture, stopping...");
            break;
        }
        framebuffet_publish_num_buffers(instance->ext_instance_ref, 1);

        bool mutex_locked = false;
        bool use_conditional_wait = true;
        FBLOG_TRACE(instance->ext_instance_ref, "generator thread locking mutex");
        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex for conditional wait loop, falling back to active checks");
            use_conditional_wait = false;
        } else {
            FBLOG_TRACE(instance->ext_instance_ref, "generator thread locked mutex");
            mutex_locked = true;
        }
        while (!instance->stop_generator) {
            if (!use_conditional_wait || !mutex_locked) {
                if (mutex_locked) {
                    FBLOG_DEBUG(instance->ext_instance_ref, "generator thread unlocking mutex because conditional wait is not usable");
                    mtx_unlock(&instance->mutex);
                    FBLOG_DEBUG(instance->ext_instance_ref, "generator thread unlocked mutex");
                    mutex_locked = false;
                }

                thrd_sleep(&active_check_sleep, NULL);
                FBLOG_TRACE(instance->ext_instance_ref, "generator thread waking up for active check (conditional wait is disabled)");
            } else {
                const struct timespec wakeup_time = {0,};
                if (!set_now_plus_seconds((struct timespec*) &wakeup_time, CONDITIONAL_WAKEUP_INTERVAL_SECONDS)) {
                    FBLOG_WARN(instance->ext_instance_ref, "failed to compute wakeup timestamp for conditional wait loop, falling back to active checks");
                    use_conditional_wait = false;
                } else {
                    res = cnd_timedwait(&instance->capture_status_cnd, &instance->mutex, &wakeup_time);
                    if (res == thrd_success || res == thrd_timedout) {
                        FBLOG_TRACE(instance->ext_instance_ref, "generator thread woken up by conditional");
                    } else {
                        FBLOG_WARN(instance->ext_instance_ref, "failed conditional wait, falling back to active checks");
                        use_conditional_wait = false;
                    }
                }
            }

            if (instance->want_reinit || instance->want_abort) {
                FBLOG_DEBUG(instance->ext_instance_ref, "capture session thread status: want_reinit=%d, want_abort=%d", instance->want_reinit, instance->want_abort);
                break;
            }
        }
        if (mutex_locked) {
            FBLOG_DEBUG(instance->ext_instance_ref, "generator thread unlocking mutex after loop");
            mtx_unlock(&instance->mutex);
            FBLOG_DEBUG(instance->ext_instance_ref, "generator thread unlocked mutex");
            mutex_locked = false;
        }

        if (instance->want_abort) {
            FBLOG_WARN(instance->ext_instance_ref, "capture session requested abort");
            break;
        }

        // if we got here we are reinitializing or stopping and need to reset

        // further resets will be handled for termination, no need to do it twice
        if (instance->stop_generator) {
            break;
        }

        if (!generator_reset(instance, "reinitialization")) {
            FBLOG_WARN(instance->ext_instance_ref, "worker reset failed, terminating");
            break;
        }

        // prevent rapid reinitialization after errors; only reinitialize without delay if we processed at least one image before
        if (!instance->captured_frame) {
            thrd_sleep(&wait_after_error, NULL);
        }
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "shutting down worker thread");

    fberror_t out_err = instance->stop_generator ? FBERROR_NONE : FBERROR_UNSPECIFIC;

    if (!generator_reset(instance, "termination")) {
        FBLOG_WARN(instance->ext_instance_ref, "worker reset failed during termination");
        out_err = FBERROR_UNSPECIFIC;
    }

    FBLOG_INFO(instance->ext_instance_ref, "worker thread terminated");

    return out_err;
}

static fberror_t capture_macos_sck_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    fbsource_capture_macos_sck_t *instance = malloc(sizeof(fbsource_capture_macos_sck_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memset(instance, 0, sizeof(fbsource_capture_macos_sck_t));

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (cnd_init(&instance->capture_status_cnd) != thrd_success) {
        mtx_destroy(&instance->mutex);
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->framebuffet = framebuffet;
    instance->ext_instance_ref = instance_ref;

    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 0);
    
    return FBERROR_NONE;
}

static fberror_t stop_generator_thread(fbsource_capture_macos_sck_t *instance) {
    fberror_t out_error = FBERROR_NONE;
  
    if (!instance->has_generator_thread) {
        return FBERROR_NONE;
    }
  
    instance->stop_generator = true;
    int thread_res;
    if (thrd_join(instance->generator_thread, &thread_res) != thrd_success) {
        // TODO: what else can be done if joining the thread fails?
        out_error = FBERROR_UNSPECIFIC;
    }
    instance->has_generator_thread = false;

    return out_error;
}

static fberror_t capture_macos_sck_destroy(void *data) {
    fbsource_capture_macos_sck_t *instance = data;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    fberror_t out_error = stop_generator_thread(instance);

    fbcollections_map_free_map_and_payloads(instance->source_config);

    // buffers are already freed at end of generator thread

    if (instance->window_search_name_exact) {
        free(instance->window_search_name_exact);
    }
    
    // FIXME: make sure nothing is waiting for the mutex before it gets destroyed
    cnd_broadcast(&instance->capture_status_cnd);
    cnd_destroy(&instance->capture_status_cnd);
    mtx_destroy(&instance->mutex);
    free(instance);
  
    return out_error;
}

static char* copy_string(char* source) {
    if (!source) {
        return NULL;
    }
    
    int length = strlen(source);
    char *copy = malloc(length + 1);
    if (copy) {
        memcpy(copy, source, length);
        copy[length] = 0;
    }
    
    return copy;
}

static void* get_config_pointer(fbsource_capture_macos_sck_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_capture_macos_sck_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static bool get_config_boolean(fbsource_capture_macos_sck_t *instance, char *key, bool default_value) {
    char *value = get_config_pointer(instance, key);
    if (!value) {
        return default_value;
    }

    return !strcmp(value, "true");
}

static fberror_t capture_macos_sck_configure_module(void *data, fbcollections_map_entry_t *config) {
    fbsource_capture_macos_sck_t *instance = data;

    FBLOG_DEBUG(instance->ext_instance_ref, "module is being (re)configured");

    stop_generator_thread(instance);
  
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_free_map_and_payloads(instance->source_config);
    instance->source_config = fbcollections_map_copy_with_payloads(config);
    if (instance->source_config == NULL) {
        mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error
        return FBERROR_UNSPECIFIC;
    }

    // reset
    if (instance->window_search_name_exact) {
        free(instance->window_search_name_exact);
        instance->window_search_name_exact = NULL;
    }
    
    bool success = true;
    
    instance->window_search_name_exact = copy_string(get_config_pointer(instance, "window.search.name.exact"));
    if (!instance->window_search_name_exact || strlen(instance->window_search_name_exact) == 0) {
        FBLOG_WARN(instance->ext_instance_ref, "window.search.name.exact has to be set");
        success = false;
    }

    mtx_unlock(&instance->mutex);
    
    if (success) {
        instance->stop_generator = false;

        if (thrd_create(&instance->generator_thread, generate, instance) == thrd_success) {
            instance->has_generator_thread = true;
        } else {
            return FBERROR_UNSPECIFIC;
        }
    }

    return success ? FBERROR_NONE : FBERROR_UNSPECIFIC;
}

static int capture_macos_sck_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static fberror_t capture_macos_sck_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_capture_macos_sck_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static fberror_t capture_macos_sck_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_capture_macos_sck_t *instance = data;

    if (!buffer_config) {
        FBLOG_WARN(instance->ext_instance_ref, "get_buffer_configuration was called with null as buffer_config");
        return FBERROR_UNSPECIFIC;
    }
    
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        // this module only generates a single buffer while configured window is available
        return FBERROR_NO_SUCH_BUFFER;
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex to get buffer config");
        return FBERROR_UNSPECIFIC;
    }

    buffer_config->width = instance->width;
    buffer_config->height = instance->height;
    buffer_config->size = instance->buffer_size;

    mtx_unlock(&instance->mutex); // TODO: handle error?

    return buffer_config->size > 0 ? FBERROR_NONE : FBERROR_NO_SUCH_BUFFER;
}

static fberror_t capture_macos_sck_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_capture_macos_sck_t *instance = data;
    fberror_t out_error = FBERROR_NONE;
  
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        return FBERROR_NO_SUCH_BUFFER;
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }
  
    if (instance->buffer_size <= 0) {
        out_error = FBERROR_NO_SUCH_BUFFER;
    } else if (buffer_size < instance->buffer_size) {
        out_error = FBERROR_BUFFER_TOO_SMALL;
    } else {
        memcpy(destination, instance->buffers[instance->complete_buffer], instance->buffer_size);
        *token = instance->frame_token;
    }

    mtx_unlock(&instance->mutex); // TODO: handle error
  
    return out_error;
}

static bool capture_macos_sck_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    fbsource_capture_macos_sck_t *instance = data;
    
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        return false;
    }
  
    return (last_token != instance->frame_token);
}

static const fbsource_module_t capture_macos_sck_module = {
    ._create = capture_macos_sck_create,
    ._destroy = capture_macos_sck_destroy,
    .configure_module = capture_macos_sck_configure_module,
    .get_num_buffers = capture_macos_sck_get_num_buffers,
    .get_module_configuration = capture_macos_sck_get_module_configuration,
    .get_buffer_configuration = capture_macos_sck_get_buffer_configuration,
    .copy_latest_frame = capture_macos_sck_copy_latest_frame,
    .has_update = capture_macos_sck_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(capture_macos_sck, "CaptureMacOSSCK", FBMODULE_LICENSE_MIT, capture_macos_sck_module)
