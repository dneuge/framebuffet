#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "../../../framework/src/fbutil.h"

#include "capture_winrt_dxgi.h"

#include "utils.h"
#include "window_utils.h"

/* Useful resources:
 * https://github.com/robmikh/Win32CaptureSample (licensed MIT)
 * https://stackoverflow.com/questions/7436144/using-winrt-from-c
 * https://stackoverflow.com/questions/75171099/how-to-get-pixel-data-out-of-an-idxgisurface-created-with-gpu-access-only#75173154 (answer by Simon Mourier, 2023, licensed CC-BY-SA, negative check for plagiarism 2026)
 */

#define MAX_WINDOW_LENGTH (16383) /* arbitrarily chosen restriction for width/height to prevent excessive size allocation and do a sanity-check on API calls */

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

static bool find_window(fbsource_capture_winrt_dxgi_t *instance, HWND *handle, window_dimensions_t *dimensions, char *exact_name) {
    window_dimensions_t found_dimensions = {0,};

    if (!(instance && handle && dimensions && exact_name)) {
        FBLOG_WARN(instance->ext_instance_ref, "find_window: missing mandatory parameters, instance=%p, handle=%p, dimensions=%p, exact_name=%p", instance, handle, dimensions, exact_name);
        return false;
    }

    window_entry_t *first_window = list_capturable_windows();
    if (!first_window) {
        FBLOG_DEBUG(instance->ext_instance_ref, "find_window: no capturable windows");
        return false;
    }

    bool found = false;
    for (window_entry_t *window = first_window; !found && window; window = window->next) {
        if (!window->title) {
            continue;
        }

        if (strcmp(exact_name, window->title) != 0) {
            continue;
        }

        if (!get_window_dimensions(window->handle, &found_dimensions)) {
            FBLOG_WARN(instance->ext_instance_ref, "find_window: failed to retrieve dimensions for matching window");
            continue;
        }

        if (found_dimensions.width <= 0 || found_dimensions.height <= 0) {
            FBLOG_WARN(instance->ext_instance_ref, "find_window: invalid window dimensions width=%d, height=%d", found_dimensions.width, found_dimensions.height);
            continue;
        } else if (found_dimensions.width > MAX_WINDOW_LENGTH || found_dimensions.height > MAX_WINDOW_LENGTH) {
            FBLOG_WARN(instance->ext_instance_ref, "find_window: excessive window dimensions width=%d, height=%d (is the window really that large?)", found_dimensions.width, found_dimensions.height);
            continue;
        }

        int cropped_width = found_dimensions.width - instance->crop_left - instance->crop_right;
        int cropped_height = found_dimensions.height - instance->crop_top - instance->crop_bottom;
        if (cropped_width <= 0 || cropped_height <= 0) {
            FBLOG_WARN(instance->ext_instance_ref, "find_window: cropped window dimensions are invalid cropped_width=%d, cropped_height=%d (window is too small)", cropped_width, cropped_height);
            continue;
        }

        *handle = window->handle;
        *dimensions = found_dimensions;

        found = true;
    }

    destroy_window_entries(first_window);

    return found;
}

static bool generator_reset(fbsource_capture_winrt_dxgi_t *instance, char *reason) {
    FBLOG_DEBUG(instance->ext_instance_ref, "resetting for %s", reason);

    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers 0");
    framebuffet_publish_num_buffers(instance->ext_instance_ref, 0);
    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers complete");

    if (mtx_lock(&instance->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex before %s; resources will not be cleaned up", reason);
        return false;
    }
    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: locked mutex");
        
    if (instance->has_capture_session) {
        FBLOG_DEBUG(instance->ext_instance_ref, "stopping capture session");
        mtx_unlock(&instance->mutex);
        stop_capture_session(instance->ext_instance_ref, instance->capture_session);
        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to relock mutex before %s after attempt to stop capture session; resources will not be cleaned up", reason);
            return false;
        }
        instance->has_capture_session = false;
        FBLOG_DEBUG(instance->ext_instance_ref, "capture session stopped");
    }

    // release all resources we still hold and reset data
    instance->width = 0;
    instance->height = 0;
    instance->buffer_size = 0;
    instance->internal_buffer_size = 0;

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

    if (instance->internal_buffer) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing internal buffer for %s", reason);
        free(instance->internal_buffer);
        instance->internal_buffer = NULL;
    }

    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: unlocking mutex");
    mtx_unlock(&instance->mutex);

    FBLOG_DEBUG(instance->ext_instance_ref, "reset for %s complete", reason);

    return true;
}

/*
static bool shm_reset(fbsource_capture_winrt_dxgi_t *instance, char *reason) {
    int res;
    bool success = true;
    xcb_void_cookie_t void_cookie = {0};
    
    FBLOG_DEBUG(instance->ext_instance_ref, "resetting SHM for %s, shmaddr=0x%lx, shmid=%d", reason, instance->shm_info.shmaddr, instance->shm_info.shmid);

    if (success && instance->shm_attached_xserver) {
        FBLOG_DEBUG(instance->ext_instance_ref, "detaching X server from SHM");
        void_cookie = xcb_shm_detach_checked(instance->xcb_conn, instance->shm_info.shmseg);
        xcb_generic_error_t *xcb_err = xcb_request_check(instance->xcb_conn, void_cookie);
        if (!xcb_err) {
            instance->shm_attached_xserver = false;
        } else {
            FBLOG_WARN(instance->ext_instance_ref, "XShmDetach failed, X11 error code %hu", xcb_err->error_code);
            success = false;
            free(xcb_err);
            xcb_err = NULL;
        }
    }
    
    if (success && instance->shm_attached_self) {
        FBLOG_DEBUG(instance->ext_instance_ref, "detaching from SHM segment");
        res = shmdt(instance->shm_info.shmaddr);
        if (res == 0) {
            instance->shm_attached_self = false;
            instance->shm_info.shmaddr = NULL;
        } else if (res == -1) {
            FBLOG_WARN(instance->ext_instance_ref, "detach from SHM segment failed (errno %d)", errno);
            success = false;
        } else {
            FBLOG_WARN(instance->ext_instance_ref, "detach from SHM segment returned undefined result (%d), assuming failed", res);
            success = false;
        }
    }

    if (success) {
        if (!instance->shm_assigned) {
            FBLOG_DEBUG(instance->ext_instance_ref, "SHM segment has already been released");
        } else {
            FBLOG_DEBUG(instance->ext_instance_ref, "releasing SHM segment (late)");
            res = shmctl((int) instance->shm_info.shmid, IPC_RMID, NULL);
            if (res == 0) {
                instance->shm_assigned = false;
            } else if (res == -1) {
                FBLOG_WARN(instance->ext_instance_ref, "release of SHM segment failed (errno %d)", errno);
                success = false;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "release of SHM segment returned undefined result (%d), assuming failed", res);
                success = false;
            }
        }
    }

    return success;
}
*/

static inline double clock2ms(clock_t c) {
    return ((double) c) / ((double) CLOCKS_PER_SEC) * 1000.0;
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

static inline void raise_reinit_flag(fbsource_capture_winrt_dxgi_t *instance) {
    instance->want_reinit = true;
    cnd_broadcast(&instance->capture_status_cnd);
}

static inline void raise_abort_flag(fbsource_capture_winrt_dxgi_t *instance) {
    instance->want_abort = true;
    cnd_broadcast(&instance->capture_status_cnd);
}

static inline void raise_captured_frame_flag(fbsource_capture_winrt_dxgi_t *instance) {
    instance->captured_frame = true;
}

static void capture_winrt_dxgi_on_frame_arrived(void *ref, win_capture_frame_pool_t frame_pool) {
    if (!ref) {
        return;
    }

    fbsource_capture_winrt_dxgi_t *instance = ref;
    if (instance->stop_generator) {
        return;
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "frame available");

    clock_t benchStart = clock(); // DEBUG

    winrt_dxgi_frame_info_t frame_info = {0,};
    if (!copy_dxgi_frame(instance->ext_instance_ref, frame_pool, instance->internal_buffer, instance->internal_buffer_size, &frame_info)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to copy frame from DirectX capture session to internal buffer");
        raise_reinit_flag(instance);
        return;
    }

    window_dimensions_t window_dimensions = {0,};
    if (!get_window_dimensions(instance->window_handle, &window_dimensions)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to retrieve window size");
        raise_reinit_flag(instance);
        return;
    }

    if ((instance->expected_window_dimensions.width != window_dimensions.width) || (instance->expected_window_dimensions.height != window_dimensions.height)) {
        FBLOG_INFO(
            instance->ext_instance_ref, "window size changed, width %u => %u, height %u => %u",
            instance->expected_window_dimensions.width, window_dimensions.width,
            instance->expected_window_dimensions.height, window_dimensions.height
        );
        raise_reinit_flag(instance);
        return;
    }

    int minimum_frame_width = (instance->offset_x + instance->width);
    int minimum_frame_height = (instance->offset_y + instance->height);
    if (frame_info.width < minimum_frame_width || frame_info.height < minimum_frame_height) {
        FBLOG_WARN(
            instance->ext_instance_ref, "insufficient size of received frame: frame_info.width=%d, frame_info.height=%d, minimum_frame_width=%d, minimum_frame_height=%d",
            frame_info.width, frame_info.height, minimum_frame_width, minimum_frame_height
        );
        raise_reinit_flag(instance);
        return;
    }

    if (frame_info.color_bitdepth != 8) {
        FBLOG_WARN(instance->ext_instance_ref, "unsupported color bit depth, need 8 bit, got %u", frame_info.color_bitdepth);
        raise_abort_flag(instance);
        return;
    }
    if (frame_info.bytes_per_pixel != 4) {
        FBLOG_WARN(instance->ext_instance_ref, "unsupported number of bytes per pixel, need 4 bytes (32 bit), got %u (%u bit)", frame_info.bytes_per_pixel, frame_info.bytes_per_pixel*8);
        raise_abort_flag(instance);
        return;
    }
    clock_t benchGotImage = clock(); // DEBUG

    if (frame_info.num_bytes_per_row % frame_info.bytes_per_pixel != 0) {
        FBLOG_WARN(instance->ext_instance_ref, "number of bytes per row is not aligned with bytes per pixel: num_bytes_per_row=%u, bytes_per_pixel=%u", frame_info.num_bytes_per_row, frame_info.bytes_per_pixel);
        raise_reinit_flag(instance);
        return;
    }
    uint32_t frame_pixels_per_row = frame_info.num_bytes_per_row / frame_info.bytes_per_pixel;

    int work_buffer = (instance->complete_buffer + 1) % 2;
    uint32_t *in = (uint32_t*) instance->internal_buffer;
    in += instance->offset_x + (instance->offset_y * frame_pixels_per_row);
    unsigned char *out = instance->buffers[work_buffer];

    const uint32_t in_skip_pixels = (frame_pixels_per_row - instance->width);
    const uint32_t wanted_width = instance->width;
    const uint32_t *in_end = in + (instance->height * frame_pixels_per_row);
    const uint32_t red_mask = frame_info.red_mask;
    const uint32_t green_mask = frame_info.green_mask;
    const uint32_t blue_mask = frame_info.blue_mask;
    const uint8_t red_shift = frame_info.red_shift;
    const uint8_t green_shift = frame_info.green_shift;
    const uint8_t blue_shift = frame_info.blue_shift;
    uint32_t *in_end_of_wanted_row = in + wanted_width;
    while (in < in_end) {
        const uint32_t in_val = *in;
        *(out++) = (in_val & red_mask) >> red_shift;
        *(out++) = (in_val & green_mask) >> green_shift;
        *(out++) = (in_val & blue_mask) >> blue_shift;
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

    FBLOG_DEBUG(instance->ext_instance_ref, "benchmark: total %5.2fms = %5.1f FPS | grab %5.2f, convert %5.2f, transfer %.2f, publish %4.2f", clock2ms(benchDone-benchStart), 1000.0/clock2ms(benchDone-benchStart), clock2ms(benchGotImage-benchStart), clock2ms(benchConverted-benchGotImage), clock2ms(benchUnlocked-benchConverted), clock2ms(benchDone-benchUnlocked));
}

static int generate(void *data) {
    fbsource_capture_winrt_dxgi_t *instance = data;

    int res;

    bool success;

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");

    fbutil_set_current_thread_name("FBCapWinRTDXGI/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));

    window_dimensions_t window_dimensions = {0,};
    int i = 0;

    // outer loop is used to reinitialize on source reconfiguration
    HWND window_handle = {0,};
    while (!instance->stop_generator) {
        // window may not be immediately available, wait until it appears
        success = false;
        while (!instance->stop_generator) {
            FBLOG_TRACE(instance->ext_instance_ref, "searching window");
            success = find_window(instance, &window_handle, &window_dimensions, instance->window_search_name_exact);
            if (success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window found: width=%d, height=%d", window_dimensions.width, window_dimensions.height);
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

        instance->offset_x = instance->crop_left;
        instance->offset_y = instance->crop_top;

        int width = window_dimensions.width - instance->offset_x - instance->crop_right;
        int height = window_dimensions.height - instance->offset_y - instance->crop_bottom;
        if (width <= 0 || height <= 0) {
            FBLOG_WARN(instance->ext_instance_ref, "inconsistent window crop? calculated invalid width=%d, height=%d past window search", width, height);
            continue;
        }

        instance->width = width;
        instance->height = height;
        instance->expected_window_dimensions.width = window_dimensions.width;
        instance->expected_window_dimensions.height = window_dimensions.height;
        instance->window_handle = window_handle;
        instance->captured_frame = false;
        instance->want_abort = false;
        instance->want_reinit = false;

        // NOTE: buffers are only transferred to instance when locking the mutex, earlier aborts are not handled
        //       by aborting loop (generator_reset)

        FBLOG_DEBUG(instance->ext_instance_ref, "worker allocating buffers");
        unsigned long buffer_size = 3 * instance->width * instance->height;
        unsigned char *buffers[2] = {NULL, NULL};
        buffers[0] = zmalloc(buffer_size);
        buffers[1] = zmalloc(buffer_size);
        if (!buffers[0] || !buffers[1]) {
            // out of memory (or something like that); there's nothing we can do but terminate
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate instance buffers, terminating");
            if (buffers[0]) {
                free(buffers[0]);
            }
            if (buffers[1]) {
                free(buffers[1]);
            }
            break;
        }

        size_t internal_buffer_size = estimate_dxgi_buffer_size(window_dimensions);
        unsigned char *internal_buffer = zmalloc(internal_buffer_size);
        if (!internal_buffer) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate internal buffer, terminating");
            free(buffers[0]);
            free(buffers[1]);
            break;
        }

        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to lock instance mutex, terminating");
            free(buffers[0]);
            free(buffers[1]);
            free(internal_buffer);
            break;
        }

        instance->buffer_size = buffer_size;
        instance->buffers[0] = buffers[0];
        instance->buffers[1] = buffers[1];
        instance->internal_buffer = internal_buffer;
        instance->internal_buffer_size = internal_buffer_size;

        // NOTE: from here on buffers are freed by generator_rest when loop gets aborted

        win_graphics_capture_item_t *capture_item = NULL;
        FBLOG_TRACE(instance->ext_instance_ref, "creating window capture item");
        if (!create_window_capture_item(instance->ext_instance_ref, window_handle, &capture_item)) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to create window capture item");
            break;
        }
        FBLOG_TRACE(instance->ext_instance_ref, "starting capture session");
        if (!start_capture_session(instance->ext_instance_ref, &instance->capture_session, instance->d3d_device, capture_item, instance->capture_flags, capture_winrt_dxgi_on_frame_arrived, instance)) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to create capture session");
            destroy_capture_item(instance->ext_instance_ref, capture_item);
            break;
        }
        FBLOG_TRACE(instance->ext_instance_ref, "capture session started, unlocking mutex");
        instance->has_capture_session = true;

        mtx_unlock(&instance->mutex);

        FBLOG_INFO(instance->ext_instance_ref, "window capture session started, buffer available");
        framebuffet_publish_num_buffers(instance->ext_instance_ref, 1);

        /*
        // compute mask and shifts for color conversion (constant for optimization)
        if (!xcb_visualtype) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_visualtype is unknown");
            break;
        }
        const uint32_t red_mask = xcb_visualtype->red_mask;
        const uint32_t green_mask = xcb_visualtype->green_mask;
        const uint32_t blue_mask = xcb_visualtype->blue_mask;
        const uint8_t red_shift = calc_shift(red_mask);
        const uint8_t green_shift = calc_shift(green_mask);
        const uint8_t blue_shift = calc_shift(blue_mask);
        FBLOG_DEBUG(instance->ext_instance_ref, "visual=%u => mask/shift red 0x%08x>>%hu, green 0x%08x>>%hu, blue 0x%08x>>%hu", xcb_visualtype->visual_id, red_mask, red_shift, green_mask, green_shift, blue_mask, blue_shift);

        // run sanity check on masks
        uint32_t max_red = (red_mask >> red_shift);
        uint32_t max_green = (green_mask >> green_shift);
        uint32_t max_blue = (blue_mask >> blue_shift);
        if (max_red > 255 || max_green > 255 || max_blue > 255) {
            FBLOG_WARN(instance->ext_instance_ref, "found unexpected high number of colors per channel (%u/%u/%u) - either color mask is interleaved or HDR is active, neither is currently supported", max_red, max_green, max_blue);
        } else if (max_red != 255 || max_green != 255 || max_blue != 255) {
            FBLOG_WARN(instance->ext_instance_ref, "found unexpected number of colors per channel (%u/%u/%u) - display mode may not use full color range which can lead to issues", max_red, max_green, max_blue);
        }
        */

        bool mutex_locked = false;
        bool use_conditional_wait = true;
        FBLOG_DEBUG(instance->ext_instance_ref, "generator thread locking mutex");
        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex for conditional wait loop, falling back to active checks");
            use_conditional_wait = false;
        } else {
            FBLOG_DEBUG(instance->ext_instance_ref, "generator thread locked mutex");
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
                if (!set_now_plus_seconds(&wakeup_time, CONDITIONAL_WAKEUP_INTERVAL_SECONDS)) {
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

static fberror_t capture_winrt_dxgi_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    fbsource_capture_winrt_dxgi_t *instance = zmalloc(sizeof(fbsource_capture_winrt_dxgi_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    if (!create_d3d_device(instance_ref, &instance->d3d_device)) {
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        destroy_d3d_device(instance_ref, instance->d3d_device);
        instance->d3d_device = NULL;
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (cnd_init(&instance->capture_status_cnd) != thrd_success) {
        mtx_destroy(&instance->mutex);
        destroy_d3d_device(instance_ref, instance->d3d_device);
        instance->d3d_device = NULL;
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->framebuffet = framebuffet;
    instance->ext_instance_ref = instance_ref;

    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 0);

    return FBERROR_NONE;
}

static fberror_t stop_generator_thread(fbsource_capture_winrt_dxgi_t *instance) {
    fberror_t out_error = FBERROR_NONE;
  
    if (!instance->has_generator_thread) {
        FBLOG_DEBUG(instance->ext_instance_ref, "stop_generator_thread: generator thread is not running; skipping");
        return FBERROR_NONE;
    }
  
    FBLOG_DEBUG(instance->ext_instance_ref, "stop_generator_thread: setting stop flag");
    instance->stop_generator = true;
    int thread_res;
    FBLOG_DEBUG(instance->ext_instance_ref, "stop_generator_thread: joining thread");
    if (thrd_join(instance->generator_thread, &thread_res) != thrd_success) {
        // TODO: what else can be done if joining the thread fails?
        FBLOG_DEBUG(instance->ext_instance_ref, "stop_generator_thread: failed to join thread, ignoring");
        out_error = FBERROR_UNSPECIFIC;
    }
    instance->has_generator_thread = false;
    FBLOG_DEBUG(instance->ext_instance_ref, "stop_generator_thread: done");

    return out_error;
}

static fberror_t capture_winrt_dxgi_destroy(void *data) {
    fbsource_capture_winrt_dxgi_t *instance = data;
    if (!instance) {
        return FBERROR_NONE;
    }

    fberror_t out_error = stop_generator_thread(instance);

    fbcollections_map_free_map_and_payloads(instance->source_config);

    // buffers are already freed at end of generator thread

    if (instance->window_search_name_exact) {
        free(instance->window_search_name_exact);
    }
    
    // FIXME: make sure nothing is waiting for conditional or mutex before it gets destroyed
    cnd_broadcast(&instance->capture_status_cnd);
    cnd_destroy(&instance->capture_status_cnd);
    mtx_destroy(&instance->mutex);

    destroy_d3d_device(instance->ext_instance_ref, instance->d3d_device);
    instance->d3d_device = NULL;

    free(instance);
  
    return out_error;
}

static char* copy_complete_string(char* source) {
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

static void* get_config_pointer(fbsource_capture_winrt_dxgi_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_capture_winrt_dxgi_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static bool get_config_boolean(fbsource_capture_winrt_dxgi_t *instance, char *key, bool default_value) {
    char *value = get_config_pointer(instance, key);
    if (!value) {
        return default_value;
    }

    return !strcmp(value, "true");
}

static fberror_t capture_winrt_dxgi_configure_module(void *data, fbcollections_map_entry_t *config) {
    fbsource_capture_winrt_dxgi_t *instance = data;

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

    instance->capture_flags = 0;
    if (get_config_boolean(instance, "hide_cursor", false)) {
        instance->capture_flags |= WINRT_DXGI_CAPTURE_FLAG_HIDE_CURSOR;
    }
    if (get_config_boolean(instance, "hide_capture_border", false)) {
        instance->capture_flags |= WINRT_DXGI_CAPTURE_FLAG_HIDE_BORDER;
    }

    instance->crop_top = get_config_int(instance, "crop.top", 0);
    instance->crop_left = get_config_int(instance, "crop.left", 0);
    instance->crop_right = get_config_int(instance, "crop.right", 0);
    instance->crop_bottom = get_config_int(instance, "crop.bottom", 0);
    if (instance->crop_top < 0 || instance->crop_bottom < 0 || instance->crop_left < 0 || instance->crop_right < 0) {
        FBLOG_WARN(
            instance->ext_instance_ref, "crop must be positive or zero, got left=%d, right=%d, top=%d, bottom=%d",
            instance->crop_left, instance->crop_right, instance->crop_top, instance->crop_bottom
        );
        success = false;
    }

    instance->window_search_name_exact = copy_complete_string(get_config_pointer(instance, "window.search.name.exact"));
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

static int capture_winrt_dxgi_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static fberror_t capture_winrt_dxgi_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_capture_winrt_dxgi_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static fberror_t capture_winrt_dxgi_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_capture_winrt_dxgi_t *instance = data;

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

static fberror_t capture_winrt_dxgi_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_capture_winrt_dxgi_t *instance = data;
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

static bool capture_winrt_dxgi_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    fbsource_capture_winrt_dxgi_t *instance = data;
    
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        return false;
    }
  
    return (last_token != instance->frame_token);
}

static const fbsource_module_t capture_winrt_dxgi_module = {
    ._create = capture_winrt_dxgi_create,
    ._destroy = capture_winrt_dxgi_destroy,
    .configure_module = capture_winrt_dxgi_configure_module,
    .get_num_buffers = capture_winrt_dxgi_get_num_buffers,
    .get_module_configuration = capture_winrt_dxgi_get_module_configuration,
    .get_buffer_configuration = capture_winrt_dxgi_get_buffer_configuration,
    .copy_latest_frame = capture_winrt_dxgi_copy_latest_frame,
    .has_update = capture_winrt_dxgi_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(capture_winrt_dxgi, "CaptureWinRTDXGI", FBMODULE_LICENSE_MIT, capture_winrt_dxgi_module)
