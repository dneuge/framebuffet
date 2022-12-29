#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <threads.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>

#include "../../../framework/src/framebuffet.h"
#include "../../../framework/src/fbutil.h"

typedef short bool;
#define FALSE 0
#define TRUE 1

typedef struct {
    mtx_t mutex;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;
  
    bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    Display *display;
    char *display_name;
    char *window_search_name_exact;
    
    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;

    unsigned char *buffers[2];
    int complete_buffer;

    // only to be handled by generator thread: (not protected by mutex)
    Window window;
    bool redirected_window;
} fbsource_capture_x11_t;

static const struct timespec wait_for_window = {
    .tv_sec = 2,
    .tv_nsec = 0
};

// TODO: check extensions; required: composite >= 0.2

static bool find_window(fbsource_capture_x11_t *instance, Window window, Window *matched_window, char *exact_name) {
    Window root;
    Window parent;
    Window *children;
    unsigned int num_children;
    Status status;
    bool found = FALSE;

    // check current window name
    char *X_window_name = NULL;
    status = XFetchName(instance->display, window, &X_window_name);
    if (status && X_window_name) {
        found = !strcmp(exact_name, X_window_name);
    }
    if (X_window_name) {
        XFree(X_window_name);
        X_window_name = NULL;
    }

    if (found) {
        *matched_window = window;
        return TRUE;
    }

    // recurse to check all child windows
    status = XQueryTree(instance->display, window, &root, &parent, &children, &num_children);
    if (!status) {
        FBLOG_WARN(instance->ext_instance_ref, "XQueryTree failed: %d", status);
        children = NULL;
    }

    if (children) {
        for (unsigned int i=0; i<num_children; i++) {
            if (find_window(instance, children[i], matched_window, exact_name)) {
                found = TRUE;
                break;
            }
        }
        
        XFree(children);
        children = NULL;
    }

    return found;
}

static bool generator_reset(fbsource_capture_x11_t *instance, char *reason) {
    FBLOG_DEBUG(instance->ext_instance_ref, "resetting for %s", reason);

    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers 0");
    framebuffet_publish_num_buffers(instance->ext_instance_ref, 0);
    FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: publish_num_buffers complete");

    if (instance->redirected_window) {
        FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: unredirect window");
        XCompositeUnredirectWindow(instance->display, instance->window, CompositeRedirectAutomatic);
        instance->redirected_window = FALSE;
        FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: unredirect window complete");
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to lock mutex before %s; resources will not be cleaned up", reason);
        return FALSE;
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

    return TRUE;
}

static inline double clock2ms(clock_t c) {
    return ((double) c) / CLOCKS_PER_SEC * 1000.0;
}

static int generate(void *data) {
    fbsource_capture_x11_t *instance = data;

    unsigned char *buffers[2];
    unsigned long buffer_size = 0;

    Status status;

    bool success;
    XWindowAttributes attrs;
    XImage *X_image = NULL;
    
    Window root_window = DefaultRootWindow(instance->display);

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");

    fbutil_set_current_thread_name("FBCapX/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));

    // outer loop is used to reinitialize on source reconfiguration
    while (!instance->stop_generator) {
        // window may not be immediately available, wait until it appears
        success = FALSE;
        while (!instance->stop_generator) {
            FBLOG_TRACE(instance->ext_instance_ref, "searching window");
            if (find_window(instance, root_window, &instance->window, instance->window_search_name_exact)) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window found: 0x%lx", instance->window);
                success = TRUE;

                status = XGetWindowAttributes(instance->display, instance->window, &attrs);
                if (!status) {
                    FBLOG_WARN(instance->ext_instance_ref, "XGetWindowAttributes failed for window 0x%lx", instance->window);
                    success = FALSE;
                } else {
                    // check window attributes
                    if (attrs.depth != 24) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported bit depth %d for window 0x%lx", attrs.depth, instance->window);
                        success = FALSE;
                    }

                    if (attrs.width < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported width, got %d for window 0x%lx", attrs.width, instance->window);
                        success = FALSE;
                    }

                    if (attrs.height < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported height, got %d for window 0x%lx", attrs.height, instance->window);
                        success = FALSE;
                    }
                }
            }
            
            if (success) {
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
        buffer_size = 3 * attrs.width * attrs.height;
        buffers[0] = malloc(buffer_size);
        buffers[1] = malloc(buffer_size);
        if (!buffers[0] || !buffers[1]) {
            // out of memory (or something like that); there's nothing we can do but terminate
            FBLOG_WARN(instance->ext_instance_ref, "failed to allocate buffers, terminating");
            break;
        }
        memset(buffers[0], 0, buffer_size);
        memset(buffers[1], 0, buffer_size);

        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to lock instance mutex, terminating");
            break;
        }

        instance->width = attrs.width;
        instance->height = attrs.height;
        instance->buffer_size = buffer_size;
        instance->buffers[0] = buffers[0];
        instance->buffers[1] = buffers[1];

        mtx_unlock(&instance->mutex);

        FBLOG_INFO(instance->ext_instance_ref, "window acquired, starting frame updates");
        framebuffet_publish_num_buffers(instance->ext_instance_ref, 1);

        // window needs to be redirected to off-screen buffer to be fully accessible if not visible
        XCompositeRedirectWindow(instance->display, instance->window, CompositeRedirectAutomatic);
        instance->redirected_window = TRUE;

        while (!instance->stop_generator) {
            clock_t benchStart = clock(); // DEBUG
            X_image = XGetImage(instance->display, instance->window, 0, 0, attrs.width, attrs.height, AllPlanes, ZPixmap);
            clock_t benchGotImage = clock(); // DEBUG
            if (!X_image) {
                FBLOG_WARN(instance->ext_instance_ref, "XGetImage failed");
                break;
            }

            int work_buffer = (instance->complete_buffer + 1) % 2;
            uint32_t *in = (uint32_t*) X_image->data;
            unsigned char *out = instance->buffers[work_buffer];
            
            int red_shift = 0;
            int green_shift = 8;
            int blue_shift = 16;
            if (X_image->byte_order == LSBFirst) {
                red_shift = 16;
                blue_shift = 0;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "unsupported XImage byte order");
                break;
            }

            for (int y=0; y<attrs.height; y++) {
                for (int x=0; x<attrs.width; x++) {
                    *(out++) = ((*in) & X_image->red_mask) >> red_shift;
                    *(out++) = ((*in) & X_image->green_mask) >> green_shift;
                    *(out++) = ((*in) & X_image->blue_mask) >> blue_shift;
                    in++;
                }
            }
            clock_t benchConverted = clock(); // DEBUG

            // mark buffer as complete
            if (mtx_lock(&instance->mutex) != thrd_success) {
                // just drop the frame if we cannot get hold of our mutex
                FBLOG_WARN(instance->ext_instance_ref, "dropping frame because mutex failed to lock");
                XDestroyImage(X_image);
                X_image = NULL;
                continue;
            }

            instance->complete_buffer = work_buffer;
            instance->frame_token++;

            mtx_unlock(&instance->mutex);
            clock_t benchUnlocked = clock(); // DEBUG

            framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
            clock_t benchPublished = clock(); // DEBUG
            
            XDestroyImage(X_image);
            X_image = NULL;
            clock_t benchDone = clock(); // DEBUG

            FBLOG_DEBUG(instance->ext_instance_ref, "benchmark: total %.2fms = %.1f FPS, XImage %.2f, convert %.2f, transfer %.2f, notify %.2f, destroy %.2f", clock2ms(benchDone-benchStart), 1000.0/clock2ms(benchDone-benchStart), clock2ms(benchGotImage-benchStart), clock2ms(benchConverted-benchGotImage), clock2ms(benchUnlocked-benchConverted), clock2ms(benchPublished-benchUnlocked), clock2ms(benchDone-benchPublished));
        }

        // if we got here we are reinitializing and need to reset
        if (!generator_reset(instance, "reinitialization")) {
            FBLOG_WARN(instance->ext_instance_ref, "worker reset failed, terminating");
            break;
        }
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "shutting down worker thread");

    if (!generator_reset(instance, "termination")) {
        FBLOG_WARN(instance->ext_instance_ref, "worker reset failed during termination");
        return FBERROR_UNSPECIFIC;
    }
    
    FBLOG_INFO(instance->ext_instance_ref, "worker thread terminated");

    return instance->stop_generator ? FBERROR_NONE : FBERROR_UNSPECIFIC;
}

static int capture_x11_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    fbsource_capture_x11_t *instance = malloc(sizeof(fbsource_capture_x11_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memset(instance, 0, sizeof(fbsource_capture_x11_t));

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->framebuffet = framebuffet;
    instance->ext_instance_ref = instance_ref;

    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 0);
    
    return FBERROR_NONE;
}

static int stop_generator_thread(fbsource_capture_x11_t *instance) {
    int out_error = FBERROR_NONE;
  
    if (!instance->has_generator_thread) {
        return FBERROR_NONE;
    }
  
    instance->stop_generator = TRUE;
    int thread_res;
    if (thrd_join(instance->generator_thread, &thread_res) != thrd_success) {
        // TODO: what else can be done if joining the thread fails?
        out_error = FBERROR_UNSPECIFIC;
    }
    instance->has_generator_thread = FALSE;

    return out_error;
}

static int capture_x11_destroy(void *data) {
    fbsource_capture_x11_t *instance = data;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    int out_error = stop_generator_thread(instance);

    fbcollections_map_free_map_and_payloads(instance->source_config);

    // buffers are already freed at end of generator thread
    
    // disconnect from X server if failed
    if (instance->display) {
        XCloseDisplay(instance->display);
        instance->display = NULL;
    }

    if (instance->display_name) {
        free(instance->display_name);
    }

    if (instance->window_search_name_exact) {
        free(instance->window_search_name_exact);
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

static void* get_config_pointer(fbsource_capture_x11_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_capture_x11_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static int capture_x11_configure_module(void *data, fbcollections_map_entry_t *config) {
    fbsource_capture_x11_t *instance = data;

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
    if (instance->display) {
        XCloseDisplay(instance->display);
        instance->display = NULL;
    }
    
    if (instance->display_name) {
        free(instance->display_name);
        instance->display_name = NULL;
    }
    
    if (instance->window_search_name_exact) {
        free(instance->window_search_name_exact);
        instance->window_search_name_exact = NULL;
    }
    
    bool success = TRUE;
    
    instance->window_search_name_exact = copy_string(get_config_pointer(instance, "window.search.name.exact"));
    if (!instance->window_search_name_exact || strlen(instance->window_search_name_exact) == 0) {
        FBLOG_WARN(instance->ext_instance_ref, "window.search.name.exact has to be set");
        success = FALSE;
    }

    // connect to X server
    if (success) {
        instance->display_name = copy_string(get_config_pointer(instance, "display")); // defaults to environment if null
        instance->display = XOpenDisplay(instance->display_name);
        if (!instance->display) {
            FBLOG_WARN(instance->ext_instance_ref, "connection to X server display failed (%s)", instance->display_name ? instance->display_name : "default");
            success = FALSE;
        }
    }

    /*
    // disconnect from X server if failed
    if (!success && instance->display) {
        XCloseDisplay(instance->display);
        instance->display = NULL;
    }
    */

    mtx_unlock(&instance->mutex);
    
    if (success) {
        instance->stop_generator = FALSE;

        if (thrd_create(&instance->generator_thread, generate, instance) == thrd_success) {
            instance->has_generator_thread = TRUE;
        } else {
            return FBERROR_UNSPECIFIC;
        }
    }

    return success ? FBERROR_NONE : FBERROR_UNSPECIFIC;
}

static int capture_x11_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static int capture_x11_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_capture_x11_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static int capture_x11_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_capture_x11_t *instance = data;

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

static int capture_x11_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_capture_x11_t *instance = data;
    int out_error = FBERROR_NONE;
  
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

static int capture_x11_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    fbsource_capture_x11_t *instance = data;
    
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        return FALSE;
    }
  
    return (last_token != instance->frame_token);
}

static const fbsource_module_t capture_x11_module = {
    ._create = capture_x11_create,
    ._destroy = capture_x11_destroy,
    .configure_module = capture_x11_configure_module,
    .get_num_buffers = capture_x11_get_num_buffers,
    .get_module_configuration = capture_x11_get_module_configuration,
    .get_buffer_configuration = capture_x11_get_buffer_configuration,
    .copy_latest_frame = capture_x11_copy_latest_frame,
    .has_update = capture_x11_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(capture_x11, "CaptureX11", FBMODULE_LICENSE_MIT, capture_x11_module)
