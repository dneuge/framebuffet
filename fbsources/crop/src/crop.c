#include <stdbool.h>
#include <stdlib.h>
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

typedef struct {
    mtx_t mutex;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;
  
    bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    char *source_name;
    int source_buffer_id;
    unsigned int width;
    unsigned int height;
    unsigned int x;
    unsigned int y;
    unsigned long buffer_size;

    unsigned char *buffers[2];
    int complete_buffer;
} fbsource_crop_t;

static const struct timespec wait_for_source = {
    .tv_sec = 0,
    .tv_nsec = 500000000 // 500ms
};

static const long wait_for_update_micros = 2000000; // 2s

static int generate(void *data) {
    fbsource_crop_t *instance = data;
    fbsource_module_instance_entry_t *source_instance = NULL;
    fbsource_buffer_config_t source_buffer_config;
    bool found_instance = false;

    unsigned int min_source_width = instance->x + instance->width;
    unsigned int min_source_height = instance->y + instance->height;
    bool got_source_token = false;
    fbsource_token_t source_token = 0;

    unsigned char *source_buffer = NULL;
    unsigned int source_width = 0;
    unsigned int source_height = 0;
    unsigned long source_buffer_size = 0;

    int res;

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");
    
    fbutil_set_current_thread_name("FBCrop/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));
    
    // outer loop is used to reinitialize on source reconfiguration
    while (!instance->stop_generator) {
        // source may not be immediately available, wait until it has been created
        found_instance = false;
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
                            found_instance = true;
                            break;
                        }

                        FBLOG_DEBUG(instance->ext_instance_ref, "source buffer is too small");
                    }
                }

                res = framebuffet_release_source_instance(source_instance);
                if (res != FBERROR_NONE) {
                    FBLOG_WARN(instance->ext_instance_ref, "failed to release source instance \"%s\" while searching (error %d), aborting", instance->source_name, res);
                    break;
                }
                source_instance = NULL;
            }
            
            FBLOG_TRACE(instance->ext_instance_ref, "source instance has not been found yet, waiting...");
            thrd_sleep(&wait_for_source, NULL);
        }

        if (!found_instance) {
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

            // crop from source to target buffer
            unsigned char *destination = buffer;
            unsigned char *source = source_start;
            for (unsigned int y=0; y<instance->height; y++) {
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

        // if we got here we are shutting down or reinitializing

        // no need to clean up twice in case we are terminating anyway
        if (instance->stop_generator) {
            break;
        }
        
        if (source_buffer) {
            FBLOG_DEBUG(instance->ext_instance_ref, "releasing source buffer for reinitialization");
            free(source_buffer);
            source_buffer = NULL;
        }

        if (source_instance) {
            res = framebuffet_release_source_instance(source_instance);
            if (res != FBERROR_NONE) {
                FBLOG_WARN(instance->ext_instance_ref, "failed to release source instance \"%s\" for reinitialization (error %d), aborting", instance->source_name, res);
                break;
            }
            source_instance = NULL;
        }
    }

    // release any remaining resources we hold
    if (source_buffer) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing source buffer for termination");
        free(source_buffer);
    }

    if (source_instance) {
        FBLOG_DEBUG(instance->ext_instance_ref, "releasing source instance for termination");
        res = framebuffet_release_source_instance(source_instance);
        source_instance = NULL;
        if (res == FBERROR_NONE) {
            FBLOG_DEBUG(instance->ext_instance_ref, "source instance released");
        } else if (res == FBERROR_DESTRUCTION_PENDING) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to release source instance \"%s\" for termination (error %d) because it is pending destruction already", instance->source_name, res);
        } else {
            FBLOG_ERROR(instance->ext_instance_ref, "Failed to release source instance \"%s\" for termination (error %d) but destruction is not pending. The source instance will probably not be able to be removed from Frame Buffet, causing further errors.", instance->source_name, res);
        }
    }
    
    FBLOG_INFO(instance->ext_instance_ref, "source worker thread terminated");

    return FBERROR_NONE;
}

static int crop_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    fbsource_crop_t *instance = malloc(sizeof(fbsource_crop_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memset(instance, 0, sizeof(fbsource_crop_t));

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->framebuffet = framebuffet;
    instance->ext_instance_ref = instance_ref;

    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 1);
    
    return FBERROR_NONE;
}

static int stop_generator_thread(fbsource_crop_t *instance) {
    int out_error = FBERROR_NONE;
  
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

static int crop_destroy(void *data) {
    fbsource_crop_t *instance = data;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    int out_error = stop_generator_thread(instance);

    fbcollections_map_free_map_and_payloads(instance->source_config);

    if (instance->source_name) {
        free(instance->source_name);
    }
    
    if (instance->buffers[0]) {
        free(instance->buffers[0]);
    }
  
    if (instance->buffers[1]) {
        free(instance->buffers[1]);
    }

    // FIXME: make sure nothing is waiting for the mutex before it gets destroyed
  
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

static void* get_config_pointer(fbsource_crop_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_crop_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static int crop_configure_module(void *data, fbcollections_map_entry_t *config) {
    fbsource_crop_t *instance = data;

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
  
    // release old buffers
    if (instance->buffer_size > 0) {
        free(instance->buffers[0]);
        free(instance->buffers[1]);
    }

    instance->buffers[0] = NULL;
    instance->buffers[1] = NULL;
    instance->complete_buffer = 0;

    // apply configuration
    if (instance->source_name) {
        free(instance->source_name);
    }
    instance->source_name = copy_string(get_config_pointer(instance, "source.name"));
    
    instance->source_buffer_id = get_config_int(instance, "source.bufferId", 0);    
    instance->width = get_config_int(instance, "width", 0);
    instance->height = get_config_int(instance, "height", 0);
    instance->x = get_config_int(instance, "x", 0);
    instance->y = get_config_int(instance, "y", 0);
    instance->buffer_size = instance->width * instance->height * 3;

    // allocate buffers if parameters were acceptable
    bool success = instance->source_name && (strlen(instance->source_name) > 0) && (instance->source_buffer_id >= 0) && (instance->width > 0) && (instance->height > 0) && (instance->x >= 0) && (instance->y >= 0);
    if (success) {
        instance->buffers[0] = (unsigned char*) malloc(instance->buffer_size);
        instance->buffers[1] = (unsigned char*) malloc(instance->buffer_size);
    }

    // reset on error (buffers remain unallocated if input is wrong)
    if (!instance->buffers[0] || !instance->buffers[1]) {
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

    success = (instance->buffer_size > 0);

    mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error

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

static int crop_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static int crop_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_crop_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static int crop_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_crop_t *instance = data;

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
  
    return FBERROR_NONE;
}

static int crop_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_crop_t *instance = data;
    int out_error = FBERROR_NONE;
  
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
        memcpy(destination, instance->buffers[instance->complete_buffer], instance->buffer_size);
        *token = instance->frame_token;
    }

    mtx_unlock(&instance->mutex); // TODO: handle error
  
    return out_error;
}

static bool crop_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    if (buffer_id != 0) {
        return false;
    }
  
    fbsource_crop_t *instance = data;
    return (last_token != instance->frame_token);
}

static const fbsource_module_t crop_module = {
    ._create = crop_create,
    ._destroy = crop_destroy,
    .configure_module = crop_configure_module,
    .get_num_buffers = crop_get_num_buffers,
    .get_module_configuration = crop_get_module_configuration,
    .get_buffer_configuration = crop_get_buffer_configuration,
    .copy_latest_frame = crop_copy_latest_frame,
    .has_update = crop_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(crop, "Crop", FBMODULE_LICENSE_MIT, crop_module)
