#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <time.h>
#include <threads.h>
#else
#include <c11/time.h>
#include <c11/threads.h>
#endif

#include "../../../framework/src/framebuffet.h"
#include "../../../framework/src/fbutil.h"

/* Use fixed buffers as we need to keep a copy of the latest state around anyways,
   so we don't actually need to alternate between buffers. */
#define FBSOURCE_MERGE_COMPLETE_BUFFER_ID 0
#define FBSOURCE_MERGE_WORK_BUFFER_ID 1

#define FBSOURCE_MERGE_AUTO (-1)

#define FBSOURCE_MERGE_SUBCONFIG_KEY_BUFFER_SIZE 65

typedef struct {
    void *instance;
    
    mtx_t mutex;
    cnd_t wait;
    
    thrd_t thread;
    bool has_thread;
    
    bool reconfigure;
    bool wait_for_update;
    
    bool update_available;
    
    char *name;
    int buffer_id;

    fbsource_module_instance_entry_t *source_instance;
    fbsource_token_t token;
    bool got_token;
    
    unsigned long buffer_size;
    unsigned char *buffer;
    unsigned int width;
    unsigned int height;
} fbsource_merge_source_t;

typedef struct {
    fbsource_merge_source_t *source;
    
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int dst_x;
    int dst_y;
} fbsource_merge_area_t;

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} fbsource_merge_color_t;

typedef struct {
    mtx_t mutex;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;
  
    bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;
    
    bool source_update_available;
    mtx_t source_update_wait_mutex;
    cnd_t source_update_wait;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    int num_areas;
    fbsource_merge_area_t *areas;

    int width;
    int height;
    bool flip_y;
    fbsource_merge_color_t base_color;

    unsigned char *buffers[2];
    unsigned long buffer_size;
} fbsource_merge_t;

static const struct timespec sleep_for_source = {
    .tv_sec = 0,
    .tv_nsec = 500000000 // 500ms
};

#define FBSOURCE_MERGE_WAIT_FOR_UPDATE_MICROS 200000 /* 200 ms */

static const struct timespec sleep_for_update_fallback = {
    .tv_sec = FBSOURCE_MERGE_WAIT_FOR_UPDATE_MICROS / 1000000,
    .tv_nsec = (FBSOURCE_MERGE_WAIT_FOR_UPDATE_MICROS % 1000000) * 1000
};

#define FBSOURCE_MERGE_WAIT_FOR_STATE_CHANGE_MICROS 2000000 /* 2 seconds */

static const struct timespec sleep_for_state_change_fallback = {
    .tv_sec = FBSOURCE_MERGE_WAIT_FOR_STATE_CHANGE_MICROS / 1000000,
    .tv_nsec = (FBSOURCE_MERGE_WAIT_FOR_STATE_CHANGE_MICROS % 1000000) * 1000
};

/**
 * Periodic wakeup while waiting for source updates.
 * Influences possible shutdown/reconfiguration delay due to shutdown check depending
 * on source update thread to be unlocked for shutdown check.
 */
static const long wait_for_update_micros = 2000000; // 2s

static const struct timespec zero_timespec = {
    .tv_sec = 0,
    .tv_nsec = 0
};

static inline struct timespec get_timespec_from_now(uint32_t microseconds) {
    // FIXME: not fully tested/verified
    // TODO: copied from framework; extract to utility module?
    
    struct timespec end;
    if (!timespec_get(&end, TIME_UTC)) {
        return zero_timespec;
    }

    time_t seconds_to_add = microseconds / 1000000;
    long nanoseconds_to_add = (microseconds % 1000000) * 1000;

    // add to timestamp
    end.tv_sec += seconds_to_add;
    end.tv_nsec += nanoseconds_to_add;

    // overflow to seconds
    end.tv_sec += end.tv_nsec / 1000000000;
    end.tv_nsec = end.tv_nsec % 1000000000;

    return end;
}

static inline fbsource_merge_area_t* get_area_pointer(fbsource_merge_t *instance, int area_id) {
    if (!instance->areas || (instance->num_areas <= 0)) {
        FBLOG_WARN(instance->ext_instance_ref, "get_area_pointer was called but no areas have been recorded");
        return NULL;
    }
    
    if ((area_id < 0) || (area_id >= instance->num_areas)) {
        FBLOG_WARN(instance->ext_instance_ref, "out of bounds access to area pointer: requested area %d but got only %d records", area_id, instance->num_areas);
        return NULL;
    }

    return &(instance->areas[area_id]);
}

static bool source_reset(fbsource_merge_t *instance, fbsource_merge_source_t *source) {
    bool success = true;
    
    if (mtx_lock(&source->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to lock mutex before clearing state", source->name, source->buffer_id);
        return false;
    }
        
    source->reconfigure = false;
    source->update_available = false;
    source->got_token = false;
    source->buffer_size = 0;
    source->width = 0;
    source->height = 0;

    if (source->buffer) {
        free(source->buffer);
        source->buffer = NULL;
    }

    if (source->source_instance) {
        int res = framebuffet_release_source_instance(source->source_instance);
        if (res == FBERROR_NONE) {
            source->source_instance = NULL;
        } else if (res == FBERROR_DESTRUCTION_PENDING) {
            // we can still continue to forget the instance if destruction is pending but should warn and abort
            // because it means we somehow ended up with a mismatch in usage counts
            // (destruction should have been prevented while source was still in use)
            FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to release source instance (error %d) because it is pending destruction already, continuing", source->name, source->buffer_id, res);
            source->source_instance = NULL;
            success = false;
        } else {
            // something else went wrong, we should keep the instance assigned so we can potentially
            // retry to release it
            FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to release source instance (error %d)", source->name, source->buffer_id, res);
            success = false;
        }
    }

    mtx_unlock(&source->mutex);

    return success;
}

static void source_wait_for_update(fbsource_merge_t *instance, fbsource_merge_source_t *source) {
    // clear request
    source->wait_for_update = false;

    if (!source->got_token) {
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] got instructed to wait for an update but there is no token from previous retrieval, assuming there will be one", source->name, source->buffer_id);
        source->update_available = true;
        instance->source_update_available = true;
        cnd_broadcast(&instance->source_update_wait);
        return;
    }

    //FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] waiting", source->name, source->buffer_id);

    int res = framebuffet_wait_for_update_micros(source->source_instance, source->buffer_id, source->token, wait_for_update_micros);
    if (res == FBERROR_NO_FRAME) {
        // got no frame yet; return to main loop to check for state changes but return to waiting state if nothing more important comes up
        source->wait_for_update = true;
    } else if (res == FBERROR_NONE) {
        // got an update, flag source and notify main thread
        source->update_available = true;
        instance->source_update_available = true;
        cnd_broadcast(&instance->source_update_wait);
    } else {
        // something went wrong that requires reinitialization
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] waiting for an update failed with error %d, will reconfigure", source->name, source->buffer_id, res);
        source->reconfigure = true;
    }
}

static bool source_reconfigure(fbsource_merge_t *instance, fbsource_merge_source_t *source) {
    // kept local for search; only stored on struct if everything is ready for processing
    fbsource_module_instance_entry_t *source_instance = NULL;
    fbsource_buffer_config_t source_buffer_config;
    unsigned char *buffer = NULL;
    
    int res;

    FBLOG_INFO(instance->ext_instance_ref, "[source %s #%d] (re)configuring", source->name, source->buffer_id);

    // clear reconfiguration request
    source->reconfigure = false;

    // source may not be immediately available, wait until it has been created
    while (!instance->stop_generator) {
        // clear any previous state
        if (!source_reset(instance, source)) {
            FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to clear state before reconfiguration; terminating...", source->name, source->buffer_id);
            break;
        }
        
        FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] searching source instance", source->name, source->buffer_id);
        source_instance = framebuffet_get_source_instance(instance->framebuffet, source->name);
        if (source_instance) {
            int source_num_buffers = framebuffet_get_num_buffers(source_instance);
            FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] source instance found, reports %d buffers", source->name, source->buffer_id, source_num_buffers);
            if (source_num_buffers >= source->buffer_id) {
                res = framebuffet_get_buffer_configuration(source_instance, source->buffer_id, &source_buffer_config);
                if (res != FBERROR_NONE) {
                    FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] source failed to report buffer config (error %d)", source->name, source->buffer_id, res);
                } else {
                    FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] source reports width=%u, height=%u, buffer_size=%lu", source->name, source->buffer_id, source_buffer_config.width, source_buffer_config.height, source_buffer_config.size);

                    bool has_required_dimensions = (source_buffer_config.width >= 0 && source_buffer_config.height >= 0);
                    if (has_required_dimensions) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] found source buffer with required dimensions", source->name, source->buffer_id);
                        break;
                    }

                    FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] source buffer is too small", source->name, source->buffer_id);
                }
            }
        }
            
        FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] source instance has not been found yet, waiting...", source->name, source->buffer_id);
        if (source_instance) {
            int res = framebuffet_release_source_instance(source_instance);
            source_instance = NULL;
            if (res != FBERROR_NONE) {
                if (res == FBERROR_DESTRUCTION_PENDING) {
                    FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to release source during search (error %d) because destruction is pending, aborting", source->name, source->buffer_id, res);
                } else {
                    FBLOG_ERROR(instance->ext_instance_ref, "[source %s #%d] Failed to release source during search (error %d), aborting. The source instance will probably not be able to be removed from Frame Buffet, causing further errors.", source->name, source->buffer_id, res);
                }
                break;
            }
        }
        thrd_sleep(&sleep_for_source, NULL);
    }

    if (!source_instance) {
        // configuration loop aborted without completing
        // this should only happen when the thread should terminate, so abort as well
        FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] search loop aborted, terminating", source->name, source->buffer_id);
        return false;
    }

    buffer = malloc(source_buffer_config.size);
    if (!buffer) {
        // out of memory (or something like that); there's nothing we can do but terminate
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to allocate source buffer (%lu bytes), terminating", source->name, source->buffer_id, source_buffer_config.size);
        return false;
    }
    memset(buffer, 0, source_buffer_config.size);

    FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] source acquired, activating", source->name, source->buffer_id);

    // transfer to struct
    if (mtx_lock(&source->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to lock mutex before activating source", source->name, source->buffer_id);
    }
        
    source->got_token = false;
    source->source_instance = source_instance;
    source->width = source_buffer_config.width;
    source->height = source_buffer_config.height;
    source->buffer_size = source_buffer_config.size;
    source->buffer = buffer;

    source->update_available = true; // has to come last as it will be checked without locking the mutex first
        
    mtx_unlock(&source->mutex);

    // notify main thread about the available update
    instance->source_update_available = true;
    cnd_broadcast(&instance->source_update_wait);

    FBLOG_INFO(instance->ext_instance_ref, "[source %s #%d] source activated", source->name, source->buffer_id);

    return true;
}

static int source_run(void *data) {
    fbsource_merge_source_t *source = data;
    fbsource_merge_t *instance = source->instance;
    
    int res;

    FBLOG_INFO(instance->ext_instance_ref, "[source %s #%d] worker thread started", source->name, source->buffer_id);
    
    fbutil_set_current_thread_name("FBMrgS/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));
 
    source->reconfigure = true;
    source->wait_for_update = false;

    bool reconfigure;
    bool wait_for_update;
    
    while (!instance->stop_generator) {
        FBLOG_TRACE(instance->ext_instance_ref, "[source %s #%d] loop iteration", source->name, source->buffer_id);
        
        // wait until asked either to wait for a frame update or to reconfigure
        reconfigure = source->reconfigure;
        wait_for_update = source->wait_for_update;
    
        if (!(wait_for_update || reconfigure)) {
            struct timespec cnd_timeout = get_timespec_from_now(FBSOURCE_MERGE_WAIT_FOR_STATE_CHANGE_MICROS);
            if (mtx_lock(&source->mutex) != thrd_success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] failed to lock mutex to wait for state change, will sleep instead", source->name, source->buffer_id);
                if (thrd_sleep(&sleep_for_state_change_fallback, NULL) < -1) {
                    FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] error while sleeping (non-mutex fallback) to wait for state change, terminating...", source->name, source->buffer_id);
                    break;
                }
            } else {
                res = thrd_success; // don't fail below

                // check again to confirm while holding the mutex
                if (!(source->wait_for_update || source->reconfigure)) {
                    res = cnd_timedwait(&source->wait, &source->mutex, &cnd_timeout);
                }

                // refresh requested state change while still holding the mutex
                reconfigure = source->reconfigure;
                wait_for_update = source->wait_for_update;
                
                mtx_unlock(&source->mutex);
                
                if ((res != thrd_success) && (res != thrd_timedout)) {
                    FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] error while waiting for state change with mutex condition, terminating...", source->name, source->buffer_id);
                    break;
                }
            }
        }
        
        if (wait_for_update) {
            source_wait_for_update(instance, source);
        } else if (reconfigure) {
            if (!source_reconfigure(instance, source)) {
                FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to configure, terminating...", source->name, source->buffer_id);
            }
        }
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "[source %s #%d] shutting down", source->name, source->buffer_id);

    if (!source_reset(instance, source)) {
        FBLOG_WARN(instance->ext_instance_ref, "[source %s #%d] failed to clear state on thread shutdown", source->name, source->buffer_id);
        if (source->source_instance) {
            FBLOG_ERROR(instance->ext_instance_ref, "[source %s #%d] Source instance could not be released (final attempt). The source instance will probably not be able to be removed from Frame Buffet, causing further errors.", source->name, source->buffer_id);
        }
    }
    
    FBLOG_INFO(instance->ext_instance_ref, "[source %s #%d] worker thread terminated", source->name, source->buffer_id);
    
    return FBERROR_NONE;
}

static int generate(void *data) {
    fbsource_merge_t *instance = data;
    int res;

    FBLOG_INFO(instance->ext_instance_ref, "[main] worker thread started");

    fbutil_set_current_thread_name("FBMrgM/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));
    
    unsigned int instance_line_bytes = instance->width * 3;
    int directional_instance_line_bytes = instance->flip_y ? -instance_line_bytes : instance_line_bytes;
    
    while (!instance->stop_generator) {
        FBLOG_TRACE(instance->ext_instance_ref, "[main] loop iteration");
        
        // wait until any source has been updated
        if (!instance->source_update_available) {
            struct timespec cnd_timeout = get_timespec_from_now(FBSOURCE_MERGE_WAIT_FOR_UPDATE_MICROS);
            if (mtx_lock(&instance->source_update_wait_mutex) != thrd_success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "[main] failed to lock mutex to wait for update, will sleep instead");
                if (thrd_sleep(&sleep_for_update_fallback, NULL) < -1) {
                    FBLOG_WARN(instance->ext_instance_ref, "[main] error while sleeping (non-mutex fallback) to wait for an update, terminating...");
                    break;
                }
            } else {
                res = thrd_success; // don't fail below if an update appeared while waiting for the mutex
                if (!instance->source_update_available) {
                    // state did not change while we were waiting to lock the mutex; wait on the condition
                    res = cnd_timedwait(&instance->source_update_wait, &instance->source_update_wait_mutex, &cnd_timeout);
                }
                mtx_unlock(&instance->source_update_wait_mutex);
                
                if ((res != thrd_success) && (res != thrd_timedout)) {
                    FBLOG_WARN(instance->ext_instance_ref, "[main] error while waiting for update with mutex condition, terminating...");
                    break;
                }
            }
        }

        if (!instance->source_update_available) {
            continue;
        }

        instance->source_update_available = false;

        FBLOG_TRACE(instance->ext_instance_ref, "[main] source update has been indicated");

        bool success = true;
        bool updated = false;
        for (int i=0; i<instance->num_areas; i++) {
            // source_area is only used to process the first encounter of a source
            // individual areas for that source are processed below
            fbsource_merge_area_t *source_area = get_area_pointer(instance, i);
            if (!source_area) {
                FBLOG_WARN(instance->ext_instance_ref, "[main] failed to get pointer for area %d, terminating...", i);
                break;
            }

            fbsource_merge_source_t *source = source_area->source;
            if (!source) {
                FBLOG_WARN(instance->ext_instance_ref, "[main] missing source for area %d, terminating...", i);
                break;
            }

            // skip sources that have no update or haven't been configured yet (quick check before locking mutex)
            if (!source->update_available || !source->buffer || !source->source_instance || !source->buffer_size || !source->width || !source->height) {
                continue;
            }
            
            if (mtx_lock(&source->mutex) != thrd_success) {
                FBLOG_WARN(instance->ext_instance_ref, "[main] failed to lock mutex for source %s #%d, terminating...", source->name, source->buffer_id);
                success = false;
                break;
            }
            
            // check source availability again and skip if unusable (with mutex; might have changed)
            if (!source->update_available || !source->buffer || !source->source_instance || !source->buffer_size || !source->width || !source->height) {
                mtx_unlock(&source->mutex);
                continue;
            }

            FBLOG_TRACE(instance->ext_instance_ref, "[main] processing update for source %s #%d", source->name, source->buffer_id);

            res = framebuffet_copy_latest_frame(source->source_instance, source->buffer, source->buffer_size, source->buffer_id, &source->token);
            source->got_token = true;
            
            if (res != FBERROR_NONE) {
                FBLOG_WARN(instance->ext_instance_ref, "[main] update for source %s #%d failed (error %d); requesting reconfiguration", source->name, source->buffer_id, res);
                source->reconfigure = true;
            } else {
                unsigned int source_line_bytes = source->width * 3;
                
                for (int j=0; j<instance->num_areas; j++) {
                    fbsource_merge_area_t *area = get_area_pointer(instance, j);
                    if (area->source != source) {
                        // will be revisited later if the other source has an update
                        continue;
                    }
                    
                    int wanted_width = (area->src_w == FBSOURCE_MERGE_AUTO) ? source->width : area->src_w;
                    int wanted_height = (area->src_h == FBSOURCE_MERGE_AUTO) ? source->height : area->src_h;

                    // reduce source area as needed to fit in source frame bounds
                    // TODO: calculate on source reconfiguration
                    if (area->src_x + wanted_width >= (int) source->width) {
                        wanted_width = source->width - area->src_x;
                    }
                    
                    if (area->src_y + wanted_height >= (int) source->height) {
                        wanted_height = source->height - area->src_y;
                    }

                    // reduce as needed to fit in destination frame bounds
                    if (area->dst_x + wanted_width >= instance->width) {
                        wanted_width = instance->width - area->dst_x;
                    }
                    
                    if (area->dst_y + wanted_height >= instance->height) {
                        wanted_height = instance->height - area->dst_y;
                    }

                    // skip area if there is nothing left to copy after area reduction
                    if ((wanted_width < 1) || (wanted_height < 1)) {
                        continue;
                    }
                    
                    unsigned char *source_start = source->buffer + (area->src_y * source->width * 3) + (area->src_x * 3);
                    unsigned int wanted_line_bytes = wanted_width * 3;
                    
                    // merge from source to target buffer
                    unsigned char *source = source_start;
                    unsigned char *destination;
                    if (!instance->flip_y) {
                        // normal orientation: y complete lines down from start of buffer, then x pixels to the right
                        destination = instance->buffers[FBSOURCE_MERGE_WORK_BUFFER_ID] + (area->dst_y * instance_line_bytes) + (area->dst_x * 3);
                    } else {
                        // flipped Y axis: y+1 complete lines up from end of buffer, then x pixels to the right
                        destination = instance->buffers[FBSOURCE_MERGE_WORK_BUFFER_ID] + instance->buffer_size - ((area->dst_y + 1) * instance_line_bytes)  + (area->dst_x * 3);
                    }
                    
                    for (int line=0; line<wanted_height; line++) {
                        memcpy(destination, source, wanted_line_bytes);
                        source += source_line_bytes;
                        destination += directional_instance_line_bytes;
                    }

                    updated = true;
                }
            }

            source->update_available = false;
            source->wait_for_update = true;
            cnd_broadcast(&source->wait);

            mtx_unlock(&source->mutex);
        }

        // copy result to complete buffer for output, update token and notify framework
        if (updated) {
            if (mtx_lock(&instance->mutex) != thrd_success) {
                FBLOG_WARN(instance->ext_instance_ref, "[main] failed to lock instance mutex for copy; dropping frame");
                continue;
            }
  
            memcpy(instance->buffers[FBSOURCE_MERGE_COMPLETE_BUFFER_ID], instance->buffers[FBSOURCE_MERGE_WORK_BUFFER_ID], instance->buffer_size);

            instance->frame_token++;

            mtx_unlock(&instance->mutex);

            framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
        }

        if (!success) {
            break;
        }
    }

    // in case of an error, stop all source threads as well because we don't need them any more
    instance->stop_generator = true;

    FBLOG_INFO(instance->ext_instance_ref, "[main] worker thread terminated");

    return FBERROR_NONE;
}

/*
static int generate_crop(void *data) {
    fbsource_merge_t *instance = data;
    fbsource_module_instance_entry_t *source_instance = NULL;
    fbsource_buffer_config_t source_buffer_config;

    unsigned int min_source_width = instance->x + instance->width;
    unsigned int min_source_height = instance->y + instance->height;
    bool got_source_token = false;
    fbsource_token_t source_token = 0;

    unsigned char *source_buffer = NULL;
    unsigned int source_width = 0;
    unsigned int source_height = 0;
    unsigned long source_buffer_size = 0;

    int res;

    FBLOG_INFO(instance->ext_instance_ref, "main worker thread started");
    
    // outer loop is used to reinitialize on source reconfiguration
    while (!instance->stop_generator) {
        // source may not be immediately available, wait until it has been created
        while (!instance->stop_generator) {
            FBLOG_TRACE(instance->ext_instance_ref, "searching source instance \"%s\"", instance->source_name);
            source_instance = framebuffet_get_source_instance(instance->framebuffet, instance->source_name);
            if (source_instance) {
                int source_num_buffers = framebuffet_get_num_buffers(source_instance);
                FBLOG_TRACE(instance->ext_instance_ref, "source instance found, reports %d buffers", source_num_buffers);
                if (source_num_buffers >= instance->source_buffer_id) {
                    res = framebuffet_get_buffer_configuration(source_instance, instance->source_buffer_id, &source_buffer_config);
                    if (res != FBERROR_NONE) {
                        FBLOG_TRACE(instance->ext_instance_ref, "source failed to report buffer config (error %d)", res);
                    } else {
                        source_width = source_buffer_config.width;
                        source_height = source_buffer_config.height;
                        source_buffer_size = source_buffer_config.size;

                        FBLOG_TRACE(instance->ext_instance_ref, "source reports width=%u, height=%u, buffer_size=%lu", source_width, source_height, source_buffer_size);

                        bool has_required_dimensions = (source_width >= min_source_width && source_height >= min_source_height);
                        if (has_required_dimensions) {
                            FBLOG_DEBUG(instance->ext_instance_ref, "found source buffer with required dimensions");
                            break;
                        }

                        FBLOG_DEBUG(instance->ext_instance_ref, "source buffer is too small");
                    }
                }
            }
            
            FBLOG_TRACE(instance->ext_instance_ref, "source instance has not been found yet, waiting...");
            source_instance = NULL;
            thrd_sleep(&wait_for_source, NULL);
        }

        if (!source_instance) {
            // configuration loop aborted without completing
            // this should only happen when the thread should terminate, so abort as well
            FBLOG_DEBUG(instance->ext_instance_ref, "search loop aborted, terminating");
            break;
        }

        source_buffer = malloc(source_buffer_size);
        if (!source_buffer) {
            // out of memory (or something like that); there's nothing we can do but terminate
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate source buffer, terminating");
            break;
        }

        FBLOG_INFO(instance->ext_instance_ref, "source acquired, starting frame updates");

        memset(source_buffer, 0, source_buffer_size);
        got_source_token = false;

        // calculate where we need to start copying from and how many bytes to skip between lines
        unsigned char *source_start = source_buffer + (instance->y * source_width * 3) + (instance->x * 3);
        unsigned int source_line_bytes = source_width * 3;
        unsigned int line_copy_count = instance->width * 3;

        FBLOG_DEBUG(instance->ext_instance_ref, "wanted(width=%d, height=%d, x=%d, y=%d) source(width=%d, height=%d) => source_line_bytes=%d, line_copy_count=%d", instance->width, instance->height, instance->x, instance->y, source_width, source_height, source_line_bytes, line_copy_count);
        
        while (!instance->stop_generator) {
            // get our target buffer to work on
            int work_buffer = (instance->complete_buffer + 1) % 2;
            unsigned char *buffer = instance->buffers[work_buffer];
            if (!buffer) {
                break;
            }

            // wait for the next update on source
            if (got_source_token) {
                res = framebuffet_wait_for_update_micros(source_instance, instance->source_buffer_id, source_token, wait_for_update_micros);
                if (res != FBERROR_NONE && res != FBERROR_NO_FRAME) {
                    // something went wrong that requires reinitialization
                    FBLOG_WARN(instance->ext_instance_ref, "waiting for an update failed with error %d, reinitializing", res);
                    break;
                }
            }

            // update the source buffer
            res = framebuffet_copy_latest_frame(source_instance, source_buffer, source_buffer_size, instance->source_buffer_id, &source_token);
            if (res != FBERROR_NONE) {
                // something went wrong that requires reinitialization
                FBLOG_WARN(instance->ext_instance_ref, "copy from source failed with error %d, reinitializing", res);
                break;
            }
            
            got_source_token = true;

            // merge from source to target buffer
            unsigned char *destination = buffer;
            unsigned char *source = source_start;
            for (int y=0; y<instance->height; y++) {
                memcpy(destination, source, line_copy_count);
                source += source_line_bytes;
                destination += line_copy_count;
            }

            // mark buffer as complete
            if (mtx_lock(&instance->mutex) != thrd_success) {
                // just drop the frame if we cannot get hold of our mutex
                FBLOG_WARN(instance->ext_instance_ref, "dropping frame because mutex failed to lock");
                continue;
            }

            instance->complete_buffer = work_buffer;
            instance->frame_token++;

            mtx_unlock(&instance->mutex); // TODO: how to handle an error?

            framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
        }

        // if we got here we are shutting down or reinitializing and need to release the source buffer
        if (source_buffer) {
            FBLOG_DEBUG(instance->ext_instance_ref, "releasing source buffer for reinitialization");
            free(source_buffer);
            source_buffer = NULL;
        }
    }

    // release any remaining memory we hold
    if (source_buffer) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing source buffer for termination");
        free(source_buffer);
    }

    FBLOG_INFO(instance->ext_instance_ref, "source worker thread terminated");
}
*/

static fberror_t merge_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    fbsource_merge_t *instance = malloc(sizeof(fbsource_merge_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memset(instance, 0, sizeof(fbsource_merge_t));

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        FBLOG_WARN(instance_ref, "failed to create instance mutex");
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (mtx_init(&instance->source_update_wait_mutex, mtx_plain) != thrd_success) {
        FBLOG_WARN(instance_ref, "failed to create source wait mutex");
        mtx_destroy(&instance->mutex);
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (cnd_init(&instance->source_update_wait) != thrd_success) {
        FBLOG_WARN(instance_ref, "failed to create source wait condition");
        mtx_destroy(&instance->source_update_wait_mutex);
        mtx_destroy(&instance->mutex);
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->framebuffet = framebuffet;
    instance->ext_instance_ref = instance_ref;

    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 1);
    
    return FBERROR_NONE;
}

static fberror_t destroy_source_thread(fbsource_merge_t *instance, fbsource_merge_source_t *source) {
    // should not fail on repeated calls, multiple areas can share a single source
    
    fberror_t err = FBERROR_NONE;

    if (!source->has_thread) {
        FBLOG_DEBUG(instance->ext_instance_ref, "source thread %s #%d is indicated as non-existent, skipping join", source->name, source->buffer_id);
    } else {
        FBLOG_DEBUG(instance->ext_instance_ref, "joining source thread %s #%d", source->name, source->buffer_id);

        cnd_broadcast(&source->wait);
        
        int thread_res;
        if (thrd_join(source->thread, &thread_res) == thrd_success) {
            FBLOG_DEBUG(instance->ext_instance_ref, "joined source thread %s #%d, freeing resources", source->name, source->buffer_id);
            source->has_thread = false;
        } else {
            // TODO: what else can be done if joining the thread fails?
            FBLOG_WARN(instance->ext_instance_ref, "failed to join source thread %s #%d", source->name, source->buffer_id);
            err = FBERROR_UNSPECIFIC;

            // resources must not be released as the thread will continue to run and would crash the application
            goto end;
        }
    }

    if (source->name) {
        free(source->name);
        source->name = NULL;
    }

    if (source->buffer) {
        free(source->buffer);
        source->buffer = NULL;
    }

    cnd_destroy(&source->wait);
    mtx_destroy(&source->mutex);

    FBLOG_TRACE(instance->ext_instance_ref, "freed resources");
    
 end:
    return err;
}

static fberror_t destroy_source_threads(fbsource_merge_t *instance) {
    fberror_t out_error = FBERROR_NONE;
    
    for (int i=0; i<instance->num_areas; i++) {
        fbsource_merge_area_t *area = get_area_pointer(instance, i);
        if (!area) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to get area pointer for area %d during thread destruction", i);
            continue;
        }

        if (!area->source) {
            FBLOG_DEBUG(instance->ext_instance_ref, "source for area %d has already been destroyed", i);
        } else {
            FBLOG_DEBUG(instance->ext_instance_ref, "destroying source at %p for area %d", area->source, i);
            fberror_t err = destroy_source_thread(instance, area->source);
            if (err == FBERROR_NONE) {
                area->source = NULL;
            } else {
                out_error = err;
            }
        }
    }

    return out_error;
}

static fberror_t destroy_threads(fbsource_merge_t *instance) {
    fberror_t err = FBERROR_NONE;
  
    if (!instance->has_generator_thread) {
        return FBERROR_NONE;
    }

    // generic stop variable is also monitored by all source threads
    instance->stop_generator = true;

    int thread_res;
    if (thrd_join(instance->generator_thread, &thread_res) != thrd_success) {
        // TODO: what else can be done if joining the thread fails?
        FBLOG_WARN(instance->ext_instance_ref, "failed to join generator thread; resources will not be freed and source threads will not be destroyed either");
        return FBERROR_UNSPECIFIC;
    }
    instance->has_generator_thread = false;

    err = destroy_source_threads(instance);
    if (err != FBERROR_NONE) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to destroy source threads (error %d)", err);
        return err;
    }

    return FBERROR_NONE;
}

static fberror_t merge_destroy(void *data) {
    fbsource_merge_t *instance = data;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    fberror_t out_error = destroy_threads(instance);
    if (out_error != FBERROR_NONE) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to destroy threads; resources cannot be freed, processing may not have terminated");
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_free_map_and_payloads(instance->source_config);

    if (instance->buffers[0]) {
        free(instance->buffers[0]);
    }
  
    if (instance->buffers[1]) {
        free(instance->buffers[1]);
    }

    if (instance->areas) {
        free(instance->areas);
    }
    
    // FIXME: make sure nothing is waiting for mutexes before they are destroyed
  
    cnd_destroy(&instance->source_update_wait);
    mtx_destroy(&instance->source_update_wait_mutex);
    
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

static void* get_config_pointer(fbsource_merge_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_merge_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static int num_uint_digits(unsigned int x) {
    int digits = 1;
    while ((x /= 10) != 0) {
        digits++;
    }
    return digits;
}

static bool build_area_config_key(char *buffer, size_t buffer_size, unsigned int area_id, char *sub_key) {
    size_t expected_length = 8 + num_uint_digits(area_id) + strlen(sub_key);
    if ((size_t) snprintf(buffer, buffer_size, "areas[%d].%s", area_id, sub_key) != expected_length) {
        buffer[0] = 0;
        return false;
    }
    buffer[buffer_size - 1] = 0;
    return true;
}

static char* copy_config_string(fbsource_merge_t *instance, char *key, char *default_value) {
    char *value = get_config_pointer(instance, key);
    return copy_string(value ? value : default_value);
}

static char* copy_subconfig_string(fbsource_merge_t *instance, int area_id, char *sub_key, char *default_value) {
    char key[FBSOURCE_MERGE_SUBCONFIG_KEY_BUFFER_SIZE] = {0};
    if (!build_area_config_key(key, FBSOURCE_MERGE_SUBCONFIG_KEY_BUFFER_SIZE, area_id, sub_key)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to build subconfig key \"%s\" for area %d, using default value \"%s\"", sub_key, area_id, default_value);
        return copy_string(default_value);
    }
    
    return copy_config_string(instance, key, default_value);
}

static int get_subconfig_int(fbsource_merge_t *instance, int area_id, char *sub_key, int default_value) {
    char key[FBSOURCE_MERGE_SUBCONFIG_KEY_BUFFER_SIZE] = {0};
    if (!build_area_config_key(key, FBSOURCE_MERGE_SUBCONFIG_KEY_BUFFER_SIZE, area_id, sub_key)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to build subconfig key \"%s\" for area %d, using default value %d", sub_key, area_id, default_value);
        return default_value;
    }

    return get_config_int(instance, key, default_value);
}

static bool parse_color(char *s, fbsource_merge_color_t *out) {
    // TODO: implement web color lookup
    // TODO: implement HTML color definition parsing (hex triple)
    
    if (!s) {
        return false;
    }
    
    if (!strcmp(s, "black")) {
        out->red = 0;
        out->green = 0;
        out->blue = 0;
        return true;
    }

    if (!strcmp(s, "white")) {
        out->red = 255;
        out->green = 255;
        out->blue = 255;
        return true;
    }

    if (!strcmp(s, "magenta")) {
        out->red = 255;
        out->green = 0;
        out->blue = 255;
        return true;
    }

    return false;
}

static void fill_with_color(unsigned char *buffer, fbsource_merge_color_t color, long size) {
    // subtract the last half-open pixel in case the buffer size does not fit all data
    size -= (size % 3);
    
    int i = 0;
    while (i < size) {
        buffer[i++] = color.red;
        buffer[i++] = color.green;
        buffer[i++] = color.blue;
    }
}

static fberror_t merge_configure_module(void *data, fbcollections_map_entry_t *config) {
    // NOTE: this is not thread-safe because destruction of threads may not have finished by a simultaneous call
    
    fbsource_merge_t *instance = data;
    fberror_t out_error = FBERROR_NONE;

    FBLOG_DEBUG(instance->ext_instance_ref, "module is being (re)configured");

    fberror_t err = destroy_threads(instance);
    if (err != FBERROR_NONE) {
        FBLOG_WARN(instance->ext_instance_ref, "unable to reconfigure, previous threads could not be destroyed (error %d)", err);
        return err;
    }
  
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_free_map_and_payloads(instance->source_config);
    instance->source_config = fbcollections_map_copy_with_payloads(config);
    if (instance->source_config == NULL) {
        mtx_unlock(&instance->mutex);
        FBLOG_WARN(instance->ext_instance_ref, "failed to copy source module configuration to instance");
        return FBERROR_UNSPECIFIC;
    }
  
    // release old buffers
    if (instance->buffers[0]) {
        free(instance->buffers[0]);
    }

    if (instance->buffers[1]) {
        free(instance->buffers[1]);
    }

    instance->buffers[0] = NULL;
    instance->buffers[1] = NULL;

    // release areas
    if (instance->areas) {
        free(instance->areas);
        instance->areas = NULL;
    }
    
    instance->num_areas = 0;
    
    // apply general generator configuration (individual areas follow below if successful)
    instance->num_areas = get_config_int(instance, "numAreas", 0);
    if (instance->num_areas < 1) {
        mtx_unlock(&instance->mutex);
        FBLOG_WARN(instance->ext_instance_ref, "invalid number of areas: %d", instance->num_areas);
        return FBERROR_UNSPECIFIC;
    }
    instance->width = get_config_int(instance, "width", 0);
    instance->height = get_config_int(instance, "height", 0);
    instance->buffer_size = instance->width * instance->height * 3;

    char *config_flip_y = get_config_pointer(instance, "flipY");
    instance->flip_y = config_flip_y && !strcmp("true", config_flip_y);

    char *config_color = get_config_pointer(instance, "baseColor");
    if (!config_color) {
        config_color = "black";
    }
    if (!parse_color(config_color, &instance->base_color)) {
        FBLOG_WARN(instance->ext_instance_ref, "base color \"%s\" could not be parsed", config_color);
        out_error = FBERROR_UNSPECIFIC;
    }

    // allocate buffers if generator parameters were acceptable
    if (out_error == FBERROR_NONE) {
        bool config_ok = (instance->buffer_size > 0) && (instance->width > 0) && (instance->height > 0);
        if (!config_ok) {
            FBLOG_WARN(instance->ext_instance_ref, "invalid base configuration provided (width=%d, height=%d, baseColor=%s)", instance->width, instance->height, config_color);
        } else {
            instance->buffers[0] = (unsigned char*) malloc(instance->buffer_size);
            instance->buffers[1] = (unsigned char*) malloc(instance->buffer_size);

            if (!(instance->buffers[0] && instance->buffers[1])) {
                FBLOG_WARN(instance->ext_instance_ref, "failed to allocate 2x %ld bytes for buffers", instance->buffer_size);
                out_error = FBERROR_MEMORY_ALLOCATION;
            } else {
                // initialize both buffers with base color
                fill_with_color(instance->buffers[0], instance->base_color, instance->buffer_size);
                memcpy(instance->buffers[1], instance->buffers[0], instance->buffer_size);
            }
        }
    }
    
    // create area records from config
    size_t areas_size = instance->num_areas * sizeof(fbsource_merge_area_t);
    if (out_error == FBERROR_NONE) {
        instance->areas = malloc(areas_size);
        if (!instance->areas) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate %ld bytes for areas array", areas_size);
            out_error = FBERROR_MEMORY_ALLOCATION;
        }
    }
    
    int highest_allocated_area_id = -1;
    if (out_error == FBERROR_NONE) {
        memset(instance->areas, 0, areas_size);

        for (int i=0; i<instance->num_areas; i++) {
            fbsource_merge_area_t *area = get_area_pointer(instance, i);
            if (!area) {
                FBLOG_ERROR(instance->ext_instance_ref, "implementation error: area pointer for %d is not available during construction; will keep all resources half-allocated", i);
                mtx_unlock(&instance->mutex);
                return FBERROR_UNSPECIFIC;
            }

            area->src_x = get_subconfig_int(instance, i, "source.x", 0);
            area->src_y = get_subconfig_int(instance, i, "source.y", 0);
            area->src_w = get_subconfig_int(instance, i, "source.width", FBSOURCE_MERGE_AUTO);
            area->src_h = get_subconfig_int(instance, i, "source.height", FBSOURCE_MERGE_AUTO);
            area->dst_x = get_subconfig_int(instance, i, "destination.x", -1);
            area->dst_y = get_subconfig_int(instance, i, "destination.y", -1);
            
            char *wanted_source_name = copy_subconfig_string(instance, i, "source.name", "");
            int wanted_buffer_id = get_subconfig_int(instance, i, "source.bufferId", 0);

            bool valid_source = wanted_source_name && (strlen(wanted_source_name) > 0) && (wanted_buffer_id >= 0);
            bool valid_src_x = (area->src_x >= 0);
            bool valid_src_y = (area->src_y >= 0);
            bool valid_src_w = (area->src_w > 0) || (area->src_w == FBSOURCE_MERGE_AUTO);
            bool valid_src_h = (area->src_h > 0) || (area->src_h == FBSOURCE_MERGE_AUTO);
            bool valid_dst_x = (area->dst_x >= 0) && (area->dst_x < instance->width) && ((area->src_w == FBSOURCE_MERGE_AUTO) || (area->dst_x + area->src_w < instance->width));
            bool valid_dst_y = (area->dst_y >= 0) && (area->dst_y < instance->height) && ((area->src_h == FBSOURCE_MERGE_AUTO) || (area->dst_y + area->src_h < instance->height));
            bool valid = valid_source && valid_src_x && valid_src_y && valid_src_w && valid_src_h && valid_dst_x && valid_dst_y;
            
            if (!valid) {
                FBLOG_WARN(instance->ext_instance_ref, "invalid configuration of area %d provided: source(name=\"%s\", bufferId=%d, x=%d, y=%d, width=%d, height=%d), destination(x=%d, y=%d); validation flags: %d%d%d%d%d%d%d", i, wanted_source_name, wanted_buffer_id, area->src_x, area->src_y, area->src_w, area->src_h, area->dst_x, area->dst_y, valid_source, valid_src_x, valid_src_y, valid_src_w, valid_src_h, valid_dst_x, valid_dst_y);
                out_error = FBERROR_UNSPECIFIC;
            } else {
                FBLOG_DEBUG(instance->ext_instance_ref, "defining area %d as: source(name=\"%s\", bufferId=%d, x=%d, y=%d, width=%d, height=%d), destination(x=%d, y=%d)", i, wanted_source_name, wanted_buffer_id, area->src_x, area->src_y, area->src_w, area->src_h, area->dst_x, area->dst_y);
            }
            
            // search previously created source of same name and buffer ID
            fbsource_merge_source_t *source = NULL;
            bool reuses_source = false;
            if (out_error == FBERROR_NONE) {
                for (int j=0; j<i; j++) {
                    fbsource_merge_area_t *previous_area = get_area_pointer(instance, j);
                    if (!previous_area) {
                        FBLOG_ERROR(instance->ext_instance_ref, "implementation error: area pointer for %d is not available during construction of area %d (but it was previously?!); will keep all resources half-allocated", j, i);
                        mtx_unlock(&instance->mutex);
                        return FBERROR_UNSPECIFIC;
                    }

                    if ((wanted_buffer_id == previous_area->source->buffer_id) && !strcmp(wanted_source_name, previous_area->source->name)) {
                        source = previous_area->source;
                        reuses_source = true;
                        break;
                    }
                }
            }

            // create source record if it doesn't exist yet
            if ((out_error == FBERROR_NONE) && !source) {
                source = malloc(sizeof(fbsource_merge_source_t));
                if (!source) {
                    FBLOG_WARN(instance->ext_instance_ref, "failed to allocate memory for source record %s #%d", wanted_source_name, wanted_buffer_id);
                    out_error = FBERROR_MEMORY_ALLOCATION;
                } else {
                    memset(source, 0, sizeof(fbsource_merge_source_t));

                    source->instance = instance;
                    source->buffer_id = wanted_buffer_id;
                    
                    source->name = copy_string(wanted_source_name);
                    if (!source->name) {
                        FBLOG_WARN(instance->ext_instance_ref, "failed to copy source name for area %d", i);
                        out_error = FBERROR_MEMORY_ALLOCATION;
                    }
                }

                if ((out_error == FBERROR_NONE) && (mtx_init(&source->mutex, mtx_plain) != thrd_success)) {
                    FBLOG_WARN(instance->ext_instance_ref, "mutex for source %s #%d could not be created", source->name, source->buffer_id);
                    out_error = FBERROR_UNSPECIFIC;
                }

                if ((out_error == FBERROR_NONE) && (cnd_init(&source->wait) != thrd_success)) {
                    FBLOG_WARN(instance->ext_instance_ref, "wait condition for source %s #%d could not be created", source->name, source->buffer_id);
                    mtx_destroy(&source->mutex);
                    out_error = FBERROR_UNSPECIFIC;
                }
            }
            
            if (out_error == FBERROR_NONE) {
                area->source = source;
            }
            
            free(wanted_source_name);
            wanted_source_name = NULL;
            
            // if an error occured, undo this record and the newly created source
            if (out_error != FBERROR_NONE) {
                if (!reuses_source && source) {
                    if (source->name) {
                        free(source->name);
                    }

                    free(source);
                    area->source = NULL;
                }

                // earlier created records will be undone in error handling
                
                break;
            }

            highest_allocated_area_id = i;
        }
    }
    
    // destroy all source records on error
    if (out_error != FBERROR_NONE) {
        for (int i=0; i<=highest_allocated_area_id; i++) {
            FBLOG_DEBUG(instance->ext_instance_ref, "destroying area record %d due to construction/configuration error", i);
            fbsource_merge_area_t *prev_area = get_area_pointer(instance, i);
            if (!prev_area) {
                FBLOG_ERROR(instance->ext_instance_ref, "implementation error: area pointer for %d is not available while cleaning up after construction error (but it was before?!); will keep all resources half-allocated", i);
                mtx_unlock(&instance->mutex);
                return FBERROR_UNSPECIFIC;
            }

            fbsource_merge_source_t *prev_source = prev_area->source;
            if (prev_source) {
                cnd_destroy(&prev_source->wait);
                mtx_destroy(&prev_source->mutex);
                
                if (prev_source->name) {
                    free(prev_source->name);
                }

                free(prev_source);
                prev_area->source = NULL;

                // since we just freed the source we need to unlink it from all other areas
                for (int j=i+1; j<=highest_allocated_area_id; j++) {
                    fbsource_merge_area_t *other_area = get_area_pointer(instance, j);
                    if (!prev_area) {
                        FBLOG_ERROR(instance->ext_instance_ref, "implementation error: area pointer for %d is not available while cleaning up area %d after construction error (but it was before?!); will keep all resources half-allocated", j, i);
                        mtx_unlock(&instance->mutex);
                        return FBERROR_UNSPECIFIC;
                    }

                    if (other_area->source == prev_area->source) {
                        other_area->source = NULL;
                    }
                }
            }
        }

        // undo areas allocation
        free(instance->areas);
        instance->areas = NULL;
        instance->num_areas = 0;
    }

    // reset generic part on error
    if (out_error != FBERROR_NONE) {
        if (instance->buffers[0]) {
            free(instance->buffers[0]);
            instance->buffers[0] = NULL;
        }
    
        if (instance->buffers[1]) {
            free(instance->buffers[1]);
            instance->buffers[1] = NULL;
        }

        instance->width = 0;
        instance->height = 0;
        instance->buffer_size = 0;
    }
    
    mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error
    
    // start main thread
    if (out_error == FBERROR_NONE) {
        instance->stop_generator = false;

        if (thrd_create(&instance->generator_thread, generate, instance) == thrd_success) {
            instance->has_generator_thread = true;
        } else {
            FBLOG_WARN(instance->ext_instance_ref, "failed to start generator thread");
            out_error = FBERROR_UNSPECIFIC;
        }
    }

    // start source threads
    if (out_error == FBERROR_NONE) {
        for (int i=0; i<instance->num_areas; i++) {
            fbsource_merge_area_t *area = get_area_pointer(instance, i);
            if (!area) {
                FBLOG_ERROR(instance->ext_instance_ref, "implementation error: area pointer for %d is not available during thread creation (but it was before?!); will leave everything in unfinished state", i);
                return FBERROR_UNSPECIFIC;
            }

            fbsource_merge_source_t *source = area->source;
            if (source->has_thread) {
                FBLOG_DEBUG(instance->ext_instance_ref, "source thread for %s #%d has already been spawned", source->name, source->buffer_id);
            } else if (thrd_create(&source->thread, source_run, source) == thrd_success) {
                source->has_thread = true;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "failed to start source thread for %s #%d", source->name, source->buffer_id);
                out_error = FBERROR_UNSPECIFIC;
            }
        }
    }
    
    FBLOG_DEBUG(instance->ext_instance_ref, "(re)configuration finished with %d", out_error);

    return out_error;
}

static int merge_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static fberror_t merge_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_merge_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static fberror_t merge_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_merge_t *instance = data;

    FBLOG_TRACE(instance->ext_instance_ref, "get_buffer_config buffer_id=%d, buffer_config=%p", buffer_id, buffer_config);
    
    if (!buffer_config) {
        FBLOG_WARN(instance->ext_instance_ref, "get_buffer_configuration was called with null as buffer_config");
        return FBERROR_UNSPECIFIC;
    }
    
    if (buffer_id != 0) {
        // this module only generates a single buffer
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

    FBLOG_TRACE(instance->ext_instance_ref, "get_buffer_config finished");
    
    return FBERROR_NONE;
}

static fberror_t merge_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_merge_t *instance = data;
    fberror_t out_error = FBERROR_NONE;

    if (buffer_id != 0) {
        return FBERROR_NO_SUCH_BUFFER;
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }
  
    if (instance->buffer_size <= 0) {
        out_error = FBERROR_UNSPECIFIC;
    } else if (buffer_size < instance->buffer_size) {
        out_error = FBERROR_BUFFER_TOO_SMALL;
    } else {
        memcpy(destination, instance->buffers[FBSOURCE_MERGE_COMPLETE_BUFFER_ID], instance->buffer_size);
        *token = instance->frame_token;
    }

    mtx_unlock(&instance->mutex); // TODO: handle error
  
    return out_error;
}

static bool merge_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    if (buffer_id != 0) {
        return false;
    }
  
    fbsource_merge_t *instance = data;
    return (last_token != instance->frame_token);
}

static const fbsource_module_t merge_module = {
    ._create = merge_create,
    ._destroy = merge_destroy,
    .configure_module = merge_configure_module,
    .get_num_buffers = merge_get_num_buffers,
    .get_module_configuration = merge_get_module_configuration,
    .get_buffer_configuration = merge_get_buffer_configuration,
    .copy_latest_frame = merge_copy_latest_frame,
    .has_update = merge_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(merge, "Merge", FBMODULE_LICENSE_MIT, merge_module)
