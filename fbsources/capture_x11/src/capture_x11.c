#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <threads.h>

#include <errno.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <sys/shm.h>

#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_util.h>
#include <xcb/composite.h>
#include <xcb/damage.h>
#include <xcb/shm.h>

#include "../../../framework/src/framebuffet.h"
#include "../../../framework/src/fbutil.h"

#define FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR 1
#define FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR 1

#define WINDOW_NAME_MAX_LENGTH_IN_NUM_QUADS (1024 / 4) /* must be factor to 4, see xcb_get_property documentation */

/* Useful resources for de-mystifying XCB:
 * https://www.x.org/releases/X11R7.6/doc/libxcb/tutorial/index.html
 */

// TODO: make system errors readable using strerror
// TODO: make X11 errors readable using https://gitlab.freedesktop.org/xorg/lib/libxcb-errors

typedef struct {
    mtx_t mutex;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;
  
    bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    xcb_connection_t *xcb_conn;
    char *display_name;
    char *window_search_name_exact;
    bool allow_shm;
    
    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;

    unsigned char *buffers[2];
    int complete_buffer;

    // only to be handled by generator thread: (not protected by mutex)
    xcb_window_t window;
    bool redirected_window;
    bool shm_assigned;
    bool shm_attached_self;
    bool shm_attached_xserver;
    xcb_image_t *X_image;
    xcb_shm_segment_info_t shm_info;
    fd_set connection_fd_set;
    int connection_fd_set_count;
    int connection_fd_stop_excl;
    
} fbsource_capture_x11_t;

static const struct timespec wait_for_window = {
    .tv_sec = 2,
    .tv_nsec = 0
};

static const struct timeval wait_for_events_timeout = {
    .tv_sec = 2,
    .tv_usec = 0
};

static const struct timespec wait_after_error = {
    .tv_sec = 1,
    .tv_nsec = 0
};

// TODO: check extensions; required: composite >= 0.2, XShm for shm

static bool find_window(fbsource_capture_x11_t *instance, xcb_window_t window, xcb_window_t *matched_window, char *exact_name) {
    xcb_window_t root;
    xcb_window_t parent;
    bool found = false;
    xcb_generic_error_t *xcb_err = NULL;

    int exact_name_len = strlen(exact_name);

    // check current window name
    xcb_get_property_cookie_t get_property_cookie = xcb_get_property(instance->xcb_conn, 0, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, WINDOW_NAME_MAX_LENGTH_IN_NUM_QUADS);
    xcb_get_property_reply_t *get_property_reply = xcb_get_property_reply(instance->xcb_conn, get_property_cookie, &xcb_err);
    if (get_property_reply && !xcb_err) {
        int xcb_window_name_len = xcb_get_property_value_length(get_property_reply);
        if (xcb_window_name_len == exact_name_len) {
            char *xcb_window_name = xcb_get_property_value(get_property_reply);
            if (xcb_window_name && !strncmp(exact_name, xcb_window_name, xcb_window_name_len)) {
                found = true;
            }
        }
    }
    bool got_get_property_reply = false;
    if (get_property_reply) {
        got_get_property_reply = true;
        free(get_property_reply);
        get_property_reply = NULL;
    }
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_property failed; X11 error code %hu", xcb_err->error_code);
        free(xcb_err);
        xcb_err = NULL;
    } else if (!got_get_property_reply) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_property failed without X11 error code");
    }

    if (found) {
        *matched_window = window;
        return true;
    }

    // recurse to check all child windows
    xcb_query_tree_cookie_t query_tree_cookie = xcb_query_tree(instance->xcb_conn, window);
    xcb_query_tree_reply_t *query_tree_reply = xcb_query_tree_reply(instance->xcb_conn, query_tree_cookie, &xcb_err);
    if (query_tree_reply && !xcb_err) {
        int num_children = xcb_query_tree_children_length(query_tree_reply);
        xcb_window_t *children = xcb_query_tree_children(query_tree_reply);
        if (children) {
            for (unsigned int i=0; i<num_children; i++) {
                if (find_window(instance, children[i], matched_window, exact_name)) {
                    found = true;
                    break;
                }
            }
        }
        children = NULL;
    }
    bool got_query_tree_reply = false;
    if (query_tree_reply) {
        got_query_tree_reply = true;
        free(query_tree_reply);
        query_tree_reply = NULL;
    }
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_query_tree failed; X11 error code %hu", xcb_err->error_code);
        free(xcb_err);
        xcb_err = NULL;
    } else if (!got_query_tree_reply) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_query_tree failed without X11 error code");
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
        xcb_void_cookie_t cookie = xcb_composite_unredirect_window_checked(instance->xcb_conn, instance->window, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
        xcb_generic_error_t *xcb_err = xcb_request_check(instance->xcb_conn, cookie);
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "generator_reset: failed to unredirect window, X11 error code %hu", xcb_err->error_code);
            free(xcb_err);
            xcb_err = NULL;
        }
        instance->redirected_window = false;
        FBLOG_TRACE(instance->ext_instance_ref, "generator_reset: unredirect window complete");
    }

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

static bool shm_reset(fbsource_capture_x11_t *instance, char *reason) {
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
    
    if (success && instance->X_image) {
        FBLOG_DEBUG(instance->ext_instance_ref, "destroying XImage");

        // destroying the image assumes that it owns the attached buffer which is not the case for SHM, "detach" first
        instance->X_image->base = NULL;
        instance->X_image->data = NULL;
        instance->X_image->size = 0;

        xcb_image_destroy(instance->X_image);
        instance->X_image = NULL;
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
        if (instance->shm_assigned) {
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

static inline double clock2ms(clock_t c) {
    return ((double) c) / CLOCKS_PER_SEC * 1000.0;
}

static void clear_connection_fd_set(fbsource_capture_x11_t *instance) {
    instance->connection_fd_set_count = 0;
    instance->connection_fd_stop_excl = 0;
    FD_ZERO(&instance->connection_fd_set);
}

static void add_connection_fd_set(fbsource_capture_x11_t *instance, int fd) {
    FBLOG_DEBUG(instance->ext_instance_ref, "adding X11 connection file descriptor %d to wait FD set", fd);
    
    FD_SET(fd, &instance->connection_fd_set);
    if (fd >= instance->connection_fd_stop_excl) {
        instance->connection_fd_stop_excl = fd + 1;
    }
    instance->connection_fd_set_count++;
}

static void init_connection_fd_set(fbsource_capture_x11_t *instance) {
    // FIXME: this should hopefully no longer be necessary with xcb? (was needed for event handling)
    /*
    clear_connection_fd_set(instance);
    add_connection_fd_set(instance, XConnectionNumber(instance->xcb_conn));
    */
}

static void wait_for_events(fbsource_capture_x11_t *instance) {
    bool success = instance->connection_fd_set_count > 0;
    if (success) {
        struct timeval timeout = wait_for_events_timeout; // timeout must be copied as it will be modified by select
        int res = select(instance->connection_fd_stop_excl, &instance->connection_fd_set, NULL, NULL, &timeout);
        
        // select returns -1 on error, 0 on timeout, positive integer = total count of descriptors
        if (res < 0) {
            success = false;
            
            // expected errors in errno according to documentation are:
            //
            //   EBADF  in case of bad file descriptors (closed or erroneous)
            //   EINVAL in case of invalid parameters
            //   ENOMEM in case of memory allocation error
            //   EINTR  if interrupted by a signal
            //
            // except for EINTR all these errors sound like they would be final, so we can stop trying
            if (errno != EINTR) {
                FBLOG_WARN(instance->ext_instance_ref, "waiting for X11 events failed with %d, disabling", errno);
                clear_connection_fd_set(instance);
            }
        }
    }

    // yield instead if we were unable to wait this time
    if (!success) {
        thrd_yield();
    }
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

static int generate(void *data) {
    const xcb_image_format_t format = XCB_IMAGE_FORMAT_Z_PIXMAP;

    fbsource_capture_x11_t *instance = data;

    unsigned char *buffers[2];
    unsigned long buffer_size = 0;

    int res;

    bool success;
    bool processed_image = false;
    xcb_get_geometry_reply_t *attrs = NULL;
    xcb_generic_error_t *xcb_err = NULL;
    xcb_damage_damage_t damage = {0}; // FIXME: correct type to subsitute Xlib Damage?
    xcb_visualtype_t *xcb_visualtype = NULL;
    bool use_damage = true;

    bool use_shm = instance->allow_shm;

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");

    fbutil_set_current_thread_name("FBCapX/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));

    init_connection_fd_set(instance);

    const xcb_setup_t *xcb_setup = xcb_get_setup(instance->xcb_conn);
    if (!xcb_setup) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_setup failed; unable to run");
        return 1;
    }

    // TODO: should we handle different screens here?
    int screen_number = 0;
    xcb_window_t root_window = {0};
    bool root_window_found = false;
    int i = 0;
    for (xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_setup); it.rem>0; xcb_screen_next(&it)) {
        i++;
        if (i < screen_number) {
            continue;
        }
        xcb_screen_t *screen = it.data;
        if (!screen) {
            break;
        }
        root_window = screen->root;
        root_window_found = true;
        break;
    }
    if (!root_window_found) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to find root window; unable to run");
        return 1;
    }

    if (use_shm) {
        xcb_shm_query_version_cookie_t shm_query_version_cookie = xcb_shm_query_version(instance->xcb_conn);
        xcb_shm_query_version_reply_t *shm_query_version_reply = xcb_shm_query_version_reply(instance->xcb_conn, shm_query_version_cookie, &xcb_err);
        bool got_shm_query_version_reply = false;
        if (shm_query_version_reply) {
            // FIXME: reply contains information about shared pixmaps and pixmap formats, do we need that?
            got_shm_query_version_reply = true;
            FBLOG_DEBUG(instance->ext_instance_ref, "SHM extension available, version %u.%u", shm_query_version_reply->major_version, shm_query_version_reply->minor_version);
            free(shm_query_version_reply);
            shm_query_version_reply = NULL;
        }
        if (xcb_err) {
            FBLOG_DEBUG(instance->ext_instance_ref, "xcb_shm_query_version failed, disabling SHM; X11 error code %hu", xcb_err->error_code);
            use_shm = false;
            free(xcb_err);
            xcb_err = NULL;
        } else if (!got_shm_query_version_reply) {
            FBLOG_DEBUG(instance->ext_instance_ref, "xcb_shm_query_version returned without reply, disabling SHM");
            use_shm = false;
        }
    }

    xcb_damage_query_version_cookie_t damage_query_version_cookie = xcb_damage_query_version(instance->xcb_conn, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
    xcb_damage_query_version_reply_t *damage_query_version_reply = xcb_damage_query_version_reply(instance->xcb_conn, damage_query_version_cookie, &xcb_err);
    bool got_damage_query_version_reply = false;
    if (damage_query_version_reply) {
        got_damage_query_version_reply = true;
        if ((damage_query_version_reply->major_version < FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) || ((damage_query_version_reply->major_version == FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) && (damage_query_version_reply->minor_version < FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR))) {
            FBLOG_WARN(instance->ext_instance_ref, "XDamage version is too old, reported %d.%d, expected at least %d.%d, disabling", damage_query_version_reply->major_version, damage_query_version_reply->minor_version, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
            use_damage = false;
        } else if ((damage_query_version_reply->major_version != FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) || (damage_query_version_reply->minor_version != FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR)) {
            FBLOG_INFO(instance->ext_instance_ref, "XDamage version is more recent than tested with this plugin, reported %d.%d, expected %d.%d", damage_query_version_reply->major_version, damage_query_version_reply->minor_version, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
        }
        free(damage_query_version_reply);
        damage_query_version_reply = NULL;
    }
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_query_version failed, disabling; X11 error code %hu", xcb_err->error_code);
        use_damage = false;
        free(xcb_err);
        xcb_err = NULL;
    }
    if (!got_damage_query_version_reply) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_query_version returned no reply, disabling");
        use_damage = false;
    }

    use_damage = false; // FIXME: disable Damage until event handling has been implemented to test function

    if (!use_damage) {
        FBLOG_WARN(instance->ext_instance_ref, "XDamage is not available, frames will be updated periodically instead of event-based, performance may be reduced");
    } else {
        FBLOG_INFO(instance->ext_instance_ref, "XDamage is available");
    }
    
    // outer loop is used to reinitialize on source reconfiguration
    while (!instance->stop_generator) {
        // window may not be immediately available, wait until it appears
        success = false;
        processed_image = false;
        while (!instance->stop_generator) {
            FBLOG_TRACE(instance->ext_instance_ref, "searching window");
            if (find_window(instance, root_window, &instance->window, instance->window_search_name_exact)) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window found: 0x%lx", instance->window);
                success = true;

                if (attrs) {
                    free(attrs);
                    attrs = NULL;
                }

                xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry(instance->xcb_conn, instance->window);
                attrs = xcb_get_geometry_reply(instance->xcb_conn, geometry_cookie, &xcb_err);
                if (xcb_err || !attrs) {
                    if (xcb_err) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_geometry failed for window 0x%lx, X11 error code %hu, attrs=%p", instance->window, xcb_err->error_code, attrs);
                        free(xcb_err);
                        xcb_err = NULL;
                    } else {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_geometry failed for window 0x%lx, no X11 error code, attrs=%p", instance->window, attrs);
                    }
                    success = false;
                } else {
                    // check window attributes
                    if (attrs->depth != 24) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported bit depth %d for window 0x%lx", attrs->depth, instance->window);
                        success = false;
                    }

                    if (attrs->width < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported width, got %d for window 0x%lx", attrs->width, instance->window);
                        success = false;
                    }

                    if (attrs->height < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported height, got %d for window 0x%lx", attrs->height, instance->window);
                        success = false;
                    }
                }
            }
            
            if (success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window attributes: width=%d, height=%d", attrs->width, attrs->height);
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
        buffer_size = 3 * attrs->width * attrs->height;
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

        instance->width = attrs->width;
        instance->height = attrs->height;
        instance->buffer_size = buffer_size;
        instance->buffers[0] = buffers[0];
        instance->buffers[1] = buffers[1];

        mtx_unlock(&instance->mutex);

        FBLOG_INFO(instance->ext_instance_ref, "window acquired, starting frame updates");
        framebuffet_publish_num_buffers(instance->ext_instance_ref, 1);

        // window needs to be redirected to off-screen buffer to be fully accessible if not visible
        xcb_void_cookie_t xcb_void_cookie = xcb_composite_redirect_window_checked(instance->xcb_conn, instance->window, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
        xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
        if (!xcb_err) {
            instance->redirected_window = true;
        } else {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_composite_redirect_window_checked failed, window may not be accessible while in background; X11 error code %hu", xcb_err->error_code);
            free(xcb_err);
            xcb_err = NULL;
        }

        // create SHM XImage structure
        if (use_shm) {
            FBLOG_DEBUG(instance->ext_instance_ref, "initializing XShm");
            instance->X_image = xcb_image_create(attrs->width, attrs->height, format, 0, attrs->depth, 24, 0, XCB_IMAGE_ORDER_LSB_FIRST, XCB_IMAGE_ORDER_MSB_FIRST, NULL, ~0, NULL); // FIXME: do we need xpad? is bpp correct? what is unit?
            if (!instance->X_image) {
                FBLOG_WARN(instance->ext_instance_ref, "xcb_image_create failed, disabling SHM support");
                use_shm = false;
            }
        }

        if (use_shm) {
            instance->shm_info.shmseg = xcb_generate_id(instance->xcb_conn);
            if (instance->shm_info.shmseg == -1) {
                FBLOG_WARN(instance->ext_instance_ref, "xcb_generate_id failed to generate ID for xcb_shm_seg_t, disabling SHM support");
                use_shm = false;
            }
        }

        // retrieve a SHM memory segment according to XImage specifications
        int bytes_per_pixel = 4; // FIXME: get from server ahead of time (visualtype is not yet available from image as we have none - maybe capture once before initializing SHM?)
        ssize_t shm_buffer_size = 0;
        if (use_shm) {
            shm_buffer_size = bytes_per_pixel * instance->X_image->width * instance->X_image->height;
            FBLOG_DEBUG(instance->ext_instance_ref, "requesting %lu bytes for SHM buffer", shm_buffer_size);
            instance->shm_info.shmid = shmget(IPC_PRIVATE, shm_buffer_size, IPC_CREAT|0777); // FIXME: verify arguments
            if (instance->shm_info.shmid != -1) {
                instance->shm_assigned = true;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "shmget failed (%d %s), disabling SHM support", errno, strerror(errno));
                use_shm = false;
                if (!shm_reset(instance, "failed initialization")) {
                    break;
                }
            }
        }

        // attach this process to the memory segment and set active on XImage if successful
        if (use_shm) {
            void *shm_addr = shmat((int) instance->shm_info.shmid, NULL, 0);
            if (shm_addr && (shm_addr != (void*)-1)) {
                instance->shm_attached_self = true;
                instance->X_image->data = shm_addr;
                instance->X_image->base = shm_addr;
                instance->X_image->size = shm_buffer_size;
                instance->shm_info.shmaddr = shm_addr;
            } else {
                // NOTE: only -1 is specified as an error indication but a null pointer probably does not make either
                FBLOG_WARN(instance->ext_instance_ref, "shmat failed (errno %d), disabling SHM support", errno);
                use_shm = false;
                if (!shm_reset(instance, "failed initialization")) {
                    break;
                }
            }

            // Releasing the SHM segment immediately after attaching to it allows the system to clean up the segment
            // in case this process crashes, otherwise it would be left over until manually removed or the system gets
            // rebooted.
            FBLOG_DEBUG(instance->ext_instance_ref, "releasing SHM segment (early)");
            res = shmctl((int) instance->shm_info.shmid, IPC_RMID, NULL);
            if (res == 0) {
                instance->shm_assigned = false;
            } else if (res == -1) {
                FBLOG_WARN(instance->ext_instance_ref, "early release of SHM segment failed, segment may remain reserved if this process crashes (errno %d)", errno);
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "early release of SHM segment returned undefined result (%d), segment may remain reserved if this process crashes", res);
            }
        }

        // attach the memory segment to X11
        if (use_shm) {
            xcb_void_cookie = xcb_shm_attach_checked(instance->xcb_conn, instance->shm_info.shmseg, instance->shm_info.shmid, 0); // FIXME: is read_only correct? is there any constant?
            xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
            if (!xcb_err) {
                FBLOG_DEBUG(instance->ext_instance_ref, "xcb_shm_attach_checked succeeded");
                instance->shm_attached_xserver = true;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "xcb_shm_attach_checked failed, disabling SHM support; X11 error code %hu", xcb_err->error_code);
                use_shm = false;
                free(xcb_err);
                xcb_err = NULL;
                if (!shm_reset(instance, "failed initialization")) {
                    break;
                }
            }
        }

        if (use_shm) {
            FBLOG_DEBUG(instance->ext_instance_ref, "XSHM is enabled");
        } else {
            FBLOG_DEBUG(instance->ext_instance_ref, "XSHM is disabled");
        }

        // listen for events
        // FIXME: equivalent of XSelectInput for xcb?
        //XSelectInput(instance->xcb_conn, instance->window, StructureNotifyMask);
        // FIXME: equivalent of XDamageCreate for xcb?
        /*
        if (use_damage) {
            damage = XDamageCreate(instance->xcb_conn, instance->window, XDamageReportNonEmpty);
        }
        */

        bool update_available = true;
        while (!instance->stop_generator) {
            if (!use_damage || update_available) {
                update_available = false;
                
                clock_t benchStart = clock(); // DEBUG
                if (use_shm) {
                    // FIXME: xcb_shm_get_image fails with BadMatch (code 8) if the window is not 100% on screen - detect and disable SHM if not usable, may work when reading back-buffer pixmap instead
                    xcb_shm_get_image_cookie_t shm_get_image_cookie = xcb_shm_get_image(instance->xcb_conn, instance->window, attrs->x, attrs->y, attrs->width, attrs->height, 0xFFFFFFFF, format, instance->shm_info.shmseg, 0);
                    xcb_shm_get_image_reply_t *shm_get_image_reply = xcb_shm_get_image_reply(instance->xcb_conn, shm_get_image_cookie, &xcb_err);
                    bool got_reply = false;
                    if (shm_get_image_reply) {
                        got_reply = true;
                        xcb_visualtype = xcb_aux_get_visualtype(instance->xcb_conn, screen_number, shm_get_image_reply->visual);
                        free(shm_get_image_reply);
                        shm_get_image_reply = NULL;
                    }
                    if (xcb_err) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_shm_get_image failed; X11 error code %hu", xcb_err->error_code);
                        free(xcb_err);
                        xcb_err = NULL;
                        break;
                    }
                    if (!got_reply) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_shm_get_image failed without X11 error code");
                        break;
                    }
                } else {
                    xcb_get_image_cookie_t get_image_cookie = xcb_get_image(instance->xcb_conn, format, instance->window, attrs->x, attrs->y, attrs->width, attrs->height, 0xFFFFFFFF);
                    xcb_get_image_reply_t *get_image_reply = xcb_get_image_reply(instance->xcb_conn, get_image_cookie, &xcb_err);
                    bool got_reply = false;
                    if (get_image_reply)
                    {
                        got_reply = true;
                        xcb_visualtype = xcb_aux_get_visualtype(instance->xcb_conn, screen_number, get_image_reply->visual);
                        if (instance->X_image) {
                            xcb_image_destroy(instance->X_image);
                            instance->X_image = NULL;
                        }
                        uint8_t *received = xcb_get_image_data(get_image_reply);
                        int received_len = xcb_get_image_data_length(get_image_reply);
                        if (!received) {
                            FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed: xcb_get_image_data returned NULL");
                        } else {
                            void *buffer = malloc(received_len); // TODO: retain and reallocate only when larger buffer is needed
                            if (!buffer) {
                                FBLOG_WARN(instance->ext_instance_ref, "failed to allocate %d bytes to copy received image buffer", received_len);
                            } else {
                                // FIXME: we may not actually need a separate buffer
                                memcpy(buffer, received, received_len);
                                // FIXME: we don't actually need an image instance, attributes and format are known and we have the buffer already - remove if not needed for sharing implementation with SHM
                                instance->X_image = xcb_image_create(attrs->width, attrs->height, format, 0, attrs->depth, 24, 0, XCB_IMAGE_ORDER_LSB_FIRST, XCB_IMAGE_ORDER_MSB_FIRST, buffer, received_len, buffer); // FIXME: do we need xpad? is bpp correct? what is unit? do we need base, size, data?
                            }
                        }
                        // FIXME: do we need to also free the reply's image data?
                        free(get_image_reply);
                        get_image_reply = NULL;
                    }
                    if (xcb_err) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed; X11 error code %hu", xcb_err->error_code);
                        free(xcb_err);
                        xcb_err = NULL;
                        break;
                    }
                    if (!got_reply) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed without X11 error code");
                        break;
                    }
                    if (!instance->X_image) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed: no image received");
                        break;
                    }
                }
                clock_t benchGotImage = clock(); // DEBUG

                int work_buffer = (instance->complete_buffer + 1) % 2;
                uint32_t *in = (uint32_t*) instance->X_image->data;
                unsigned char *out = instance->buffers[work_buffer];

                // TODO: cache shifts, recompute only when xcb_visualtype changes
                int red_shift = calc_shift(xcb_visualtype->red_mask);
                int green_shift = calc_shift(xcb_visualtype->green_mask);
                int blue_shift = calc_shift(xcb_visualtype->blue_mask);

                if (!xcb_visualtype) {
                    FBLOG_WARN(instance->ext_instance_ref, "xcb_visualtype is unknown");
                    break;
                }

                const uint32_t red_mask = xcb_visualtype->red_mask;
                const uint32_t green_mask = xcb_visualtype->green_mask;
                const uint32_t blue_mask = xcb_visualtype->blue_mask;

                const uint32_t *end = in + (attrs->height * attrs->width);
                while (in < end) {
                    const uint32_t in_val = *in;
                    *(out++) = (in_val & red_mask) >> red_shift;
                    *(out++) = (in_val & green_mask) >> green_shift;
                    *(out++) = (in_val & blue_mask) >> blue_shift;
                    in++;
                }
                clock_t benchConverted = clock(); // DEBUG

                // mark buffer as complete
                if (mtx_lock(&instance->mutex) != thrd_success) {
                    // just drop the frame if we cannot get hold of our mutex
                    FBLOG_WARN(instance->ext_instance_ref, "dropping frame because mutex failed to lock");
                    xcb_image_destroy(instance->X_image);
                    instance->X_image = NULL;
                    continue;
                }

                instance->complete_buffer = work_buffer;
                instance->frame_token++;

                mtx_unlock(&instance->mutex);
                clock_t benchUnlocked = clock(); // DEBUG

                framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
                clock_t benchPublished = clock(); // DEBUG

                if (!use_shm) {
                    xcb_image_destroy(instance->X_image);
                    instance->X_image = NULL;
                }
            
                clock_t benchDone = clock(); // DEBUG

                FBLOG_DEBUG(instance->ext_instance_ref, "benchmark: total %.2fms = %.1f FPS, XImage %.2f, convert %.2f, transfer %.2f, notify %.2f, destroy %.2f, use_shm=%d", clock2ms(benchDone-benchStart), 1000.0/clock2ms(benchDone-benchStart), clock2ms(benchGotImage-benchStart), clock2ms(benchConverted-benchGotImage), clock2ms(benchUnlocked-benchConverted), clock2ms(benchPublished-benchUnlocked), clock2ms(benchDone-benchPublished), use_shm);

                processed_image = true;
            }
            
            // process events
            bool size_changed = false;
            // FIXME: reimplement with xcb
            /*
            while (XPending(instance->xcb_conn) > 0) {
                XNextEvent(instance->xcb_conn, &event);

                if (use_damage && (event.type == damage_event_base)) {
                    XDamageNotifyEvent *e = (XDamageNotifyEvent*) &event;
                    if (e->drawable == instance->window) {
                        FBLOG_TRACE(instance->ext_instance_ref, "XDamage indicates update");
                        XDamageSubtract(instance->xcb_conn, e->damage, None, None);
                        update_available = true;
                        continue;
                    }
                } else if (event.type == ConfigureNotify) {
                    XConfigureEvent *e = (XConfigureEvent*) &event;
                    if ((e->window == instance->window) && ((e->width != attrs->width) || (e->height != attrs->height))) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "XConfigureEvent indicates size has changed from %dx%d to %dx%d", attrs->width, attrs->height, e->width, e->height);
                        size_changed = true;
                        continue;
                    }
                }
            }
            */
            if (size_changed) {
                FBLOG_INFO(instance->ext_instance_ref, "window size changed, reinitializing");
                break;
            }
            if (use_damage && !update_available) {
                wait_for_events(instance);
            }
        }

        // if we got here we are reinitializing or stopping and need to reset
        // FIXME: equivalent for XSelectInput needed with xcb?
        //XSelectInput(instance->xcb_conn, instance->window, 0); // TODO: check if removing the listener really works this way (I suspect it does but it is not mentioned in docs and yields no search results)
        if (use_damage) {
            xcb_void_cookie = xcb_damage_destroy_checked(instance->xcb_conn, damage);
            xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
            if (xcb_err) {
                FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_destroy_checked failed; X11 error code %hu", xcb_err->error_code);
                free(xcb_err);
                xcb_err = NULL;
            }
        }

        // further resets will be handled for termination, no need to do it twice
        if (instance->stop_generator) {
            break;
        }
        
        if (use_shm && !shm_reset(instance, "reinitialization")) {
            FBLOG_WARN(instance->ext_instance_ref, "SHM reset failed, terminating");
            break;
        }
        
        if (!generator_reset(instance, "reinitialization")) {
            FBLOG_WARN(instance->ext_instance_ref, "worker reset failed, terminating");
            break;
        }

        // prevent rapid reinitialization after errors; only reinitialize without delay if we processed at least one image before
        if (!processed_image) {
            thrd_sleep(&wait_after_error, NULL);
        }
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "shutting down worker thread");
    
    fberror_t out_err = instance->stop_generator ? FBERROR_NONE : FBERROR_UNSPECIFIC;
    
    if (use_shm && !shm_reset(instance, "termination")) {
        FBLOG_WARN(instance->ext_instance_ref, "SHM reset failed, terminating");
        out_err = FBERROR_UNSPECIFIC;
    }
    
    if (!generator_reset(instance, "termination")) {
        FBLOG_WARN(instance->ext_instance_ref, "worker reset failed during termination");
        out_err = FBERROR_UNSPECIFIC;
    }

    clear_connection_fd_set(instance);
    
    if (attrs) {
        free(attrs);
        attrs = NULL;
    }

    FBLOG_INFO(instance->ext_instance_ref, "worker thread terminated");

    return out_err;
}

static fberror_t capture_x11_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
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

static fberror_t stop_generator_thread(fbsource_capture_x11_t *instance) {
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

static fberror_t capture_x11_destroy(void *data) {
    fbsource_capture_x11_t *instance = data;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    fberror_t out_error = stop_generator_thread(instance);

    fbcollections_map_free_map_and_payloads(instance->source_config);

    // buffers are already freed at end of generator thread
    
    // disconnect from X server if failed
    if (instance->xcb_conn) {
        xcb_disconnect(instance->xcb_conn);
        instance->xcb_conn = NULL;
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

static bool get_config_boolean(fbsource_capture_x11_t *instance, char *key, bool default_value) {
    char *value = get_config_pointer(instance, key);
    if (!value) {
        return default_value;
    }

    return !strcmp(value, "true");
}

static fberror_t capture_x11_configure_module(void *data, fbcollections_map_entry_t *config) {
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
    if (instance->xcb_conn) {
        xcb_disconnect(instance->xcb_conn);
        instance->xcb_conn = NULL;
    }
    
    if (instance->display_name) {
        free(instance->display_name);
        instance->display_name = NULL;
    }
    
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

    instance->allow_shm = get_config_boolean(instance, "allowSHM", true);

    // connect to X server
    if (success) {
        instance->display_name = copy_string(get_config_pointer(instance, "display")); // defaults to environment if null
        instance->xcb_conn = xcb_connect(instance->display_name, NULL);
        if (!instance->xcb_conn) {
            FBLOG_WARN(instance->ext_instance_ref, "connection to X server display failed (%s)", instance->display_name ? instance->display_name : "default");
            success = false;
        }
    }

    /*
    // disconnect from X server if failed
    if (!success && instance->display) {
        xcb_disconnect(instance->display);
        instance->display = NULL;
    }
    */

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

static int capture_x11_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static fberror_t capture_x11_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_capture_x11_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static fberror_t capture_x11_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
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

static fberror_t capture_x11_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_capture_x11_t *instance = data;
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

static bool capture_x11_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    fbsource_capture_x11_t *instance = data;
    
    if (buffer_id != 0 || instance->buffer_size <= 0) {
        return false;
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
