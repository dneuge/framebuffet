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

#define X_BYTES_PER_PIXEL (4) /* API always uses 32 bits per pixel; this does not seem to be flexible */

/* Useful resources for de-mystifying XCB:
 * https://www.x.org/releases/X11R7.6/doc/libxcb/tutorial/index.html
 * https://xcb.freedesktop.org/tutorial/events/
 *
 * Description of what needs to be done to correctly access Composite-redirected windows: (written for Xlib)
 *   https://web.archive.org/web/20240806120127/http://www.talisman.org/~erlkonig/misc/x11-composite-tutorial/
 * which is now also unavailable and originally recovered
 *   https://web.archive.org/web/20101223183042/http://ktown.kde.org/~fredrik/composite_howto.html
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
    bool allow_back_buffer;
    bool allow_damage;

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

typedef struct _damage_list_item_t damage_list_item_t;
typedef struct _damage_list_item_t {
    xcb_damage_damage_t xcb_damage;
    damage_list_item_t *next;
} damage_list_item_t;

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

/**
 * Frees the entire list starting from given head.
 * @param head head of list to free all items of; allowed to be null
 */
static void free_damages_list(damage_list_item_t *head) {
    while (head) {
        damage_list_item_t *next = head->next;
        head->xcb_damage = -1;
        head->next = NULL;
        free(head);
        head = next;
    }
}

/**
 * Calls xcb_damage_destroy for all items of given list and removes them if successful.
 * Unsuccessfully destroyed instances are kept in the list. The given list head is moved so that points to the first
 * undestroyable instance or NULL if successful.
 * @param instance capture instance
 * @param head_item pointer to list head, list head will be moved to first failed item or set to NULL if all items were successfully destroyed; allowed to be NULL
 * @return true if successful, false if (partially) failed
 */
static bool destroy_damages(fbsource_capture_x11_t *instance, damage_list_item_t **head_item) {
    xcb_generic_error_t *xcb_err = NULL;
    xcb_void_cookie_t xcb_void_cookie = {0};

    if (!head_item || !*head_item) {
        return true;
    }

    damage_list_item_t *item = *head_item;
    while (item) {
        xcb_void_cookie = xcb_damage_destroy_checked(instance->xcb_conn, item->xcb_damage);
        xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "[destroy_damages] failed to destroy damage instance on X server");
            item = item->next;

            free(xcb_err);
            xcb_err = NULL;

            // keep item in list
            continue;
        }

        // remove item from list
        damage_list_item_t *next = item->next;
        item->xcb_damage = -1;
        item->next = NULL;
        free(item);

        // move head to new item
        if (*head_item == item) {
            *head_item = next;
        }

        item = next;
    }

    return !*head_item;
}

/**
 * Creates damage monitor instances via xcb_damage_create, recursively if possible.
 * @param instance capture instance
 * @param drawable drawable to monitor
 * @param drawable_is_window set to true if the drawable is a window (will attempt recursive monitoring), false if not
 * @return list of registered damage instances or NULL if failed
 */
static damage_list_item_t* create_damages(fbsource_capture_x11_t *instance, xcb_drawable_t drawable, bool drawable_is_window) {
    xcb_generic_error_t *xcb_err = NULL;
    xcb_void_cookie_t xcb_void_cookie = {0};

    damage_list_item_t *head_item = malloc(sizeof(damage_list_item_t));
    if (!head_item) {
        FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to allocate list item");
        return NULL;
    }
    head_item->xcb_damage = -1;
    head_item->next = NULL;

    head_item->xcb_damage = xcb_generate_id(instance->xcb_conn);
    if (head_item->xcb_damage == -1) {
        FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to create XDamage ID");
        free(head_item);
        return NULL;
    }

    FBLOG_TRACE(instance->ext_instance_ref, "[create_damages] registering damage for drawable 0x%08x", drawable);
    xcb_void_cookie = xcb_damage_create_checked(instance->xcb_conn, head_item->xcb_damage, drawable, XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to create XDamage monitor for drawable 0x%08x, X11 error code %hu", drawable, xcb_err->error_code);
        free(xcb_err);
        xcb_err = NULL;

        free(head_item);
        return NULL;
    }

    // we can only recurse into sub-windows, not other types of drawables
    if (!drawable_is_window) {
        return head_item;
    }

    bool success = true;
    damage_list_item_t *item = head_item;
    xcb_query_tree_cookie_t query_tree_cookie = xcb_query_tree(instance->xcb_conn, drawable);
    xcb_query_tree_reply_t *query_tree_reply = xcb_query_tree_reply(instance->xcb_conn, query_tree_cookie, &xcb_err);
    if (query_tree_reply && !xcb_err) {
        int num_child_windows = xcb_query_tree_children_length(query_tree_reply);
        FBLOG_TRACE(instance->ext_instance_ref, "[create_damages] registering damage for %d child windows", num_child_windows);
        xcb_window_t *windows = xcb_query_tree_children(query_tree_reply);
        for (int i=0; i<num_child_windows; i++) {
            xcb_window_t window = windows[i];

            xcb_generic_error_t *xcb_inner_err = NULL;
            xcb_get_window_attributes_cookie_t get_window_attributes_cookie = xcb_get_window_attributes(instance->xcb_conn, window);
            xcb_get_window_attributes_reply_t *get_window_attributes_reply = xcb_get_window_attributes_reply(instance->xcb_conn, get_window_attributes_cookie, &xcb_inner_err);
            bool window_has_no_output = false;
            if (get_window_attributes_reply && !xcb_inner_err) {
                // NOTE: the underscore on _class probably indicates a private field but there appears to be no other way to get that information
                window_has_no_output = (get_window_attributes_reply->_class == XCB_WINDOW_CLASS_INPUT_ONLY);
            }
            bool got_get_window_attributes_reply = false;
            if (get_window_attributes_reply) {
                got_get_window_attributes_reply = true;
                free(get_window_attributes_reply);
                get_window_attributes_reply = NULL;
            }
            if (xcb_inner_err) {
                FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to get window attributes for 0x%08x, X11 error code %hu; subscription to damage notifications may fail", window, xcb_inner_err->error_code);
                free(xcb_inner_err);
                xcb_inner_err = NULL;
            } else if (!got_get_window_attributes_reply) {
                FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to get window attributes for 0x%08x without X11 error code; subscription to damage notifications may fail", window);
            }

            // trying to subscribe to a window that is "input only" fails, prevent such requests
            if (window_has_no_output) {
                FBLOG_DEBUG(instance->ext_instance_ref, "[create_damages] sub-window 0x%08x produces no output, skipping", window);
                continue;
            }

            damage_list_item_t *sublist = create_damages(instance, window, true);
            if (!sublist) {
                success = false;
                break;
            }

            // join result to list
            item->next = sublist;

            // forward list to last item
            while (item->next) {
                item = item->next;
            }
        }
    }
    bool got_query_tree_reply = false;
    if (query_tree_reply) {
        got_query_tree_reply = true;
        free(query_tree_reply);
        query_tree_reply = NULL;
    }
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "[create_damages] xcb_query_tree failed; X11 error code %hu", xcb_err->error_code);
        success = false;
        free(xcb_err);
        xcb_err = NULL;
    } else if (!got_query_tree_reply) {
        FBLOG_WARN(instance->ext_instance_ref, "[create_damages] xcb_query_tree failed without reply");
        success = false;
    }

    if (success) {
        return head_item;
    }

    if (destroy_damages(instance, &head_item)) {
        return NULL;
    }

    FBLOG_WARN(instance->ext_instance_ref, "[create_damages] failed to destroy some instances; freeing list but damages may remain registered");
    free_damages_list(head_item);

    return NULL;
}

static int generate(void *data) {
    const xcb_image_format_t format = XCB_IMAGE_FORMAT_Z_PIXMAP;

    fbsource_capture_x11_t *instance = data;

    unsigned char *buffers[2];
    unsigned long buffer_size = 0;

    int res;

    bool success;
    bool processed_image = false;
    xcb_get_geometry_reply_t *geom = NULL; // NOTE: we retain the reply, freed on termination or replacement
    xcb_generic_error_t *xcb_err = NULL;
    damage_list_item_t *damages = NULL;
    uint8_t damage_event_base = 0;
    xcb_visualtype_t *xcb_visualtype = NULL;
    bool use_damage = instance->allow_damage;
    bool use_shm = instance->allow_shm;

    FBLOG_INFO(instance->ext_instance_ref, "worker thread started");

    fbutil_set_current_thread_name("FBCapX/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));

    const xcb_setup_t *xcb_setup = xcb_get_setup(instance->xcb_conn);
    if (!xcb_setup) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_setup failed; unable to run");
        return 1;
    }

    // TODO: should we handle different screens here?
    int screen_number = 0;
    xcb_window_t root_window = {0};
    bool root_window_found = false;
    uint16_t screen_width = 0;
    uint16_t screen_height = 0;
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

        screen_width = screen->width_in_pixels;
        screen_height = screen->height_in_pixels;

        break;
    }
    if (!root_window_found) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to find root window; unable to run");
        return 1;
    }

    // check if SHM is available and disable if not
    if (use_shm) {
        xcb_shm_query_version_cookie_t shm_query_version_cookie = xcb_shm_query_version(instance->xcb_conn);
        xcb_shm_query_version_reply_t *shm_query_version_reply = xcb_shm_query_version_reply(instance->xcb_conn, shm_query_version_cookie, &xcb_err);
        if (shm_query_version_reply && !xcb_err) {
            FBLOG_DEBUG(instance->ext_instance_ref, "SHM extension available, version %u.%u", shm_query_version_reply->major_version, shm_query_version_reply->minor_version);
        }
        bool got_shm_query_version_reply = false;
        if (shm_query_version_reply) {
            got_shm_query_version_reply = true;
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

    const char damage_extension_name[] = "DAMAGE";
    xcb_query_extension_cookie_t query_extension_cookie = xcb_query_extension(instance->xcb_conn, strlen(damage_extension_name), damage_extension_name);
    xcb_query_extension_reply_t *query_extension_reply = xcb_query_extension_reply(instance->xcb_conn, query_extension_cookie, &xcb_err);
    if (query_extension_reply && !xcb_err) {
        damage_event_base = query_extension_reply->first_event;
        if (!query_extension_reply->present) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_query_extension did not find \"%s\", disabling XDamage", damage_extension_name);
            use_damage = false;
        }
    }
    bool got_query_extension_reply = false;
    if (query_extension_reply) {
        got_query_extension_reply = true;
        free(query_extension_reply);
        query_extension_reply = NULL;
    }
    if (xcb_err) {
        FBLOG_WARN(instance->ext_instance_ref, "xcb_query_extension for %s failed, disabling XDamage; X11 error code %hu", damage_extension_name, xcb_err->error_code);
        use_damage = false;
        free(xcb_err);
        xcb_err = NULL;
    } else if (!got_query_extension_reply) {
        FBLOG_DEBUG(instance->ext_instance_ref, "xcb_query_extension for %s failed without X11 error code, disabling XDamage", damage_extension_name);
        use_damage = false;
    }

    if (use_damage) {
        xcb_damage_query_version_cookie_t damage_query_version_cookie = xcb_damage_query_version(instance->xcb_conn, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
        xcb_damage_query_version_reply_t *damage_query_version_reply = xcb_damage_query_version_reply(instance->xcb_conn, damage_query_version_cookie, &xcb_err);
        if (damage_query_version_reply && !xcb_err) {
            if ((damage_query_version_reply->major_version < FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) || ((damage_query_version_reply->major_version == FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) && (damage_query_version_reply->minor_version < FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR))) {
                FBLOG_WARN(instance->ext_instance_ref, "XDamage version is too old, reported %d.%d, expected at least %d.%d, disabling", damage_query_version_reply->major_version, damage_query_version_reply->minor_version, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
                use_damage = false;
            } else if ((damage_query_version_reply->major_version != FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR) || (damage_query_version_reply->minor_version != FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR)) {
                FBLOG_INFO(instance->ext_instance_ref, "XDamage version is more recent than tested with this plugin, reported %d.%d, expected %d.%d", damage_query_version_reply->major_version, damage_query_version_reply->minor_version, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MAJOR, FBSOURCE_CAPX11_XDAMAGE_EXPECTED_MINOR);
            }
        }
        bool got_damage_query_version_reply = false;
        if (damage_query_version_reply) {
            got_damage_query_version_reply = true;
            free(damage_query_version_reply);
            damage_query_version_reply = NULL;
        }
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_query_version failed, disabling; X11 error code %hu", xcb_err->error_code);
            use_damage = false;
            free(xcb_err);
            xcb_err = NULL;
        } else if (!got_damage_query_version_reply) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_query_version returned no reply, disabling");
            use_damage = false;
        }
    }

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

                if (geom) {
                    free(geom);
                    geom = NULL;
                }

                xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry(instance->xcb_conn, instance->window);
                geom = xcb_get_geometry_reply(instance->xcb_conn, geometry_cookie, &xcb_err);
                if (geom && !xcb_err) {
                    // check window attributes
                    if (geom->depth != 24) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported bit depth %d for window 0x%lx", geom->depth, instance->window);
                        success = false;
                    }

                    if (geom->width < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported width, got %d for window 0x%lx", geom->width, instance->window);
                        success = false;
                    }

                    if (geom->height < 1) {
                        FBLOG_WARN(instance->ext_instance_ref, "unsupported height, got %d for window 0x%lx", geom->height, instance->window);
                        success = false;
                    }
                }
                if (xcb_err) {
                    FBLOG_WARN(instance->ext_instance_ref, "xcb_get_geometry failed for window 0x%lx, X11 error code %hu, geom=%p", instance->window, xcb_err->error_code, geom);
                    success = false;
                    free(xcb_err);
                    xcb_err = NULL;

                    // prevent accidental use of invalid reply
                    if (geom) {
                        free(geom);
                        geom = NULL;
                    }
                } else if (!geom) {
                    FBLOG_WARN(instance->ext_instance_ref, "xcb_get_geometry failed for window 0x%lx, no X11 error code, geom=%p", instance->window, geom);
                    success = false;
                }
            }

            if (success) {
                FBLOG_DEBUG(instance->ext_instance_ref, "window geometry: width=%d, height=%d", geom->width, geom->height);
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
        buffer_size = 3 * geom->width * geom->height;
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

        instance->width = geom->width;
        instance->height = geom->height;
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

        // Composite redirection switches window rendering to a back-buffer pixmap which is what we should read from.
        // If we keep reading from the window, we loose most benefits of window redirection, such as the ability to
        // read oversize windows from shared memory.
        xcb_drawable_t source_drawable = instance->window;
        bool back_buffer_pixmap_available = false;
        xcb_pixmap_t back_buffer_pixmap = -1;
        if (instance->redirected_window && instance->allow_back_buffer) {
            back_buffer_pixmap = xcb_generate_id(instance->xcb_conn);
            if (back_buffer_pixmap == -1) {
                FBLOG_WARN(instance->ext_instance_ref, "failed to allocate ID for back-buffer pixmap");
            } else {
                xcb_void_cookie = xcb_composite_name_window_pixmap_checked(instance->xcb_conn, instance->window, back_buffer_pixmap);
                xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
                if (!xcb_err) {
                    FBLOG_DEBUG(instance->ext_instance_ref, "back-buffer pixmap is available");
                    back_buffer_pixmap_available = true;
                    source_drawable = back_buffer_pixmap;
                } else {
                    FBLOG_WARN(instance->ext_instance_ref, "xcb_composite_name_window_pixmap_checked failed, back-buffer pixmap unavailable; X11 error code %hu", xcb_err->error_code);
                }
            }
        }

        // check if the window is (fully) on screen
        xcb_translate_coordinates_cookie_t translate_coordinates_cookie = xcb_translate_coordinates(instance->xcb_conn, instance->window, root_window, geom->x, geom->y);
        xcb_translate_coordinates_reply_t *translate_coordinates_reply = xcb_translate_coordinates_reply(instance->xcb_conn, translate_coordinates_cookie, &xcb_err);
        int16_t window_offset_x = 0;
        int16_t window_offset_y = 0;
        bool offset_known = false;
        if (translate_coordinates_reply && !xcb_err) {
            // TODO: there also is "same_screen"; is that relevant?
            window_offset_x = translate_coordinates_reply->dst_x;
            window_offset_y = translate_coordinates_reply->dst_y;
            offset_known = true;
        }
        bool got_translate_coordinates_reply = false;
        if (translate_coordinates_reply) {
            got_translate_coordinates_reply = true;
            free(translate_coordinates_reply);
            translate_coordinates_reply = NULL;
        }
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_translate_coordinates failed; X11 error code %hu", xcb_err->error_code);
            free(xcb_err);
            xcb_err = NULL;
        } else if (!got_translate_coordinates_reply) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_translate_coordinates failed without X11 error code");
        }

        bool window_fully_rendered = false;
        if (!offset_known) {
            FBLOG_WARN(instance->ext_instance_ref, "window offset relative to screen is unknown - this may lead to issues accessing window contents");
        } else {
            // TODO: we may also have issues if the window has been minimized, check for that as well?
            int right_edge = geom->width + window_offset_x;
            int bottom_edge = geom->height + window_offset_y;
            window_fully_rendered = !((window_offset_x < 0) || (window_offset_y < 0) || (right_edge > screen_width) || (bottom_edge > screen_height));
            FBLOG_DEBUG(instance->ext_instance_ref, "window_fully_rendered=%d (window offset %d/%d, window size %u/%u, edges %d/%d, screen %u/%u)", window_fully_rendered, window_offset_x, window_offset_y, geom->width, geom->height, right_edge, bottom_edge, screen_width, screen_height);
        }

        // unless we read from the backing pixmap, SHM can only be used if the window is completely on screen
        if (use_shm && (!window_fully_rendered && !back_buffer_pixmap_available)) {
            FBLOG_INFO(instance->ext_instance_ref, "SHM cannot be used because window does not get fully rendered (partially off-screen)");
            use_shm = false;
        }

        // get additional window attributes
        xcb_visualtype = NULL; // do not free; pointers returned by XCB seem to be constants, although not declared as such
        xcb_get_window_attributes_cookie_t get_window_attributes_cookie = xcb_get_window_attributes(instance->xcb_conn, instance->window);
        xcb_get_window_attributes_reply_t *get_window_attributes_reply = xcb_get_window_attributes_reply(instance->xcb_conn, get_window_attributes_cookie, &xcb_err);
        if (get_window_attributes_reply && !xcb_err) {
            xcb_visualtype = xcb_aux_get_visualtype(instance->xcb_conn, screen_number, get_window_attributes_reply->visual);
        }
        bool got_get_window_attributes_reply = false;
        if (get_window_attributes_reply) {
            got_get_window_attributes_reply = true;
            free(get_window_attributes_reply);
            get_window_attributes_reply = NULL;
        }
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_get_window_attributes failed; X11 error code %hu", xcb_err->error_code);
            free(xcb_err);
            xcb_err = NULL;
        } else if (!got_get_window_attributes_reply) {
            FBLOG_WARN(instance->ext_instance_ref, "xcb_get_window_attributes failed without X11 error code");
        }

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

        // create SHM XImage structure
        if (use_shm) {
            instance->shm_info.shmseg = xcb_generate_id(instance->xcb_conn);
            if (instance->shm_info.shmseg == -1) {
                FBLOG_WARN(instance->ext_instance_ref, "xcb_generate_id failed to generate ID for xcb_shm_seg_t, disabling SHM support");
                use_shm = false;
            }
        }

        // retrieve a SHM memory segment according to geometry
        ssize_t shm_buffer_size = 0;
        if (use_shm) {
            shm_buffer_size = X_BYTES_PER_PIXEL * geom->width * geom->height;
            FBLOG_DEBUG(instance->ext_instance_ref, "requesting %lu bytes for SHM buffer", shm_buffer_size);
            instance->shm_info.shmid = shmget(IPC_PRIVATE, shm_buffer_size, IPC_CREAT|0777);
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
            xcb_void_cookie = xcb_shm_attach_checked(instance->xcb_conn, instance->shm_info.shmseg, instance->shm_info.shmid, 0);
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
        uint32_t subscribe_event_mask_values[] = { XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
        xcb_void_cookie = xcb_change_window_attributes_checked(instance->xcb_conn, instance->window, XCB_CW_EVENT_MASK, &subscribe_event_mask_values);
        xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to subscribe to window changes; changes in size will not be detected");
            free(xcb_err);
            xcb_err = NULL;
        }
        if (!destroy_damages(instance, &damages)) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to unregister some XDamage instances on reconfiguration");
            free_damages_list(damages);
            damages = NULL;
        }
        if (use_damage) {
            damages = create_damages(instance, instance->window, true);
            if (!damages) {
                FBLOG_WARN(instance->ext_instance_ref, "failed to create XDamage monitors, disabling");
                use_damage = false;
            }
        }

        bool update_available = true;
        const size_t expected_image_length = geom->width * geom->height * X_BYTES_PER_PIXEL;
        uint32_t *image_data = use_shm ? (uint32_t*) instance->shm_info.shmaddr : NULL; // NOTE: must not be freed!
        xcb_get_image_reply_t *get_image_reply = NULL;
        while (!instance->stop_generator) {
            if (!use_damage || update_available) {
                update_available = false;
                bool visual_changed = false;

                clock_t benchStart = clock(); // DEBUG
                if (use_shm) {
                    xcb_shm_get_image_cookie_t shm_get_image_cookie = xcb_shm_get_image(instance->xcb_conn, source_drawable, geom->x, geom->y, geom->width, geom->height, 0xFFFFFFFF, format, instance->shm_info.shmseg, 0);
                    xcb_shm_get_image_reply_t *shm_get_image_reply = xcb_shm_get_image_reply(instance->xcb_conn, shm_get_image_cookie, &xcb_err);
                    if (shm_get_image_reply && !xcb_err) {
                        visual_changed = shm_get_image_reply->visual && (xcb_visualtype->visual_id != shm_get_image_reply->visual);
                    }
                    bool got_reply = false;
                    if (shm_get_image_reply) {
                        got_reply = true;
                        free(shm_get_image_reply);
                        shm_get_image_reply = NULL;
                    }
                    if (xcb_err) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_shm_get_image failed; X11 error code %hu", xcb_err->error_code);
                        free(xcb_err);
                        xcb_err = NULL;
                        break;
                    } else if (!got_reply) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_shm_get_image failed without X11 error code");
                        break;
                    }
                } else {
                    xcb_get_image_cookie_t get_image_cookie = xcb_get_image(instance->xcb_conn, format, source_drawable, geom->x, geom->y, geom->width, geom->height, 0xFFFFFFFF);
                    get_image_reply = xcb_get_image_reply(instance->xcb_conn, get_image_cookie, &xcb_err);
                    if (get_image_reply && !xcb_err) {
                        visual_changed = get_image_reply->visual && (xcb_visualtype->visual_id != get_image_reply->visual);
                        image_data = (uint32_t*) xcb_get_image_data(get_image_reply);
                        int received_len = xcb_get_image_data_length(get_image_reply);
                        if (!image_data) {
                            FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed: xcb_get_image_data returned NULL");
                            break;
                        } else if (received_len != expected_image_length) {
                            FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed: unexpected number of bytes received; got %d, expected %lu", received_len, expected_image_length);
                            break;
                        }
                    }
                    bool got_reply = false;
                    if (get_image_reply) {
                        got_reply = true;
                        // used and freed outside, keep in memory
                    }
                    if (xcb_err) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed; X11 error code %hu", xcb_err->error_code);
                        free(xcb_err);
                        xcb_err = NULL;
                        break;
                    } else if (!got_reply) {
                        FBLOG_WARN(instance->ext_instance_ref, "xcb_get_image failed without X11 error code");
                        break;
                    }
                }
                clock_t benchGotImage = clock(); // DEBUG

                if (visual_changed) {
                    FBLOG_WARN(instance->ext_instance_ref, "visual type changed; conversion is not possible, reinitializing...");
                    break;
                }

                if (!image_data) {
                    FBLOG_WARN(instance->ext_instance_ref, "missing image data, reinitializing...");
                    break;
                }

                int work_buffer = (instance->complete_buffer + 1) % 2;
                uint32_t *in = image_data;
                unsigned char *out = instance->buffers[work_buffer];

                const uint32_t *end = in + (geom->height * geom->width);
                while (in < end) {
                    const uint32_t in_val = *in;
                    *(out++) = (in_val & red_mask) >> red_shift;
                    *(out++) = (in_val & green_mask) >> green_shift;
                    *(out++) = (in_val & blue_mask) >> blue_shift;
                    in++;
                }
                clock_t benchConverted = clock(); // DEBUG

                if (get_image_reply) {
                    free(get_image_reply);
                    get_image_reply = NULL;
                }

                // mark buffer as complete
                if (mtx_lock(&instance->mutex) != thrd_success) {
                    // just drop the frame if we cannot get hold of our mutex
                    FBLOG_WARN(instance->ext_instance_ref, "dropping frame because mutex failed to lock");
                    continue;
                }

                instance->complete_buffer = work_buffer;
                instance->frame_token++;

                mtx_unlock(&instance->mutex);
                clock_t benchUnlocked = clock(); // DEBUG

                framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
                clock_t benchPublished = clock(); // DEBUG

                clock_t benchDone = clock(); // DEBUG

                FBLOG_DEBUG(instance->ext_instance_ref, "benchmark: total %.2fms = %.1f FPS, XImage %.2f, convert %.2f, transfer %.2f, notify %.2f, destroy %.2f | use_shm=%d, use_damage=%d, back buffer? %d", clock2ms(benchDone-benchStart), 1000.0/clock2ms(benchDone-benchStart), clock2ms(benchGotImage-benchStart), clock2ms(benchConverted-benchGotImage), clock2ms(benchUnlocked-benchConverted), clock2ms(benchPublished-benchUnlocked), clock2ms(benchDone-benchPublished), use_shm, use_damage, back_buffer_pixmap_available);

                processed_image = true;
            }

            if (get_image_reply) {
                free(get_image_reply);
                get_image_reply = NULL;
            }

            // process events
            bool window_configuration_changed = false;
            xcb_generic_event_t *event = NULL;
            xcb_aux_sync(instance->xcb_conn);
            while ((event = xcb_poll_for_queued_event(instance->xcb_conn)) != NULL) {
                uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(event);
                FBLOG_TRACE(instance->ext_instance_ref, "received event type: 0x%02x | damage event base 0x%02x", event->response_type, damage_event_base);
                if (event_type == damage_event_base + XCB_DAMAGE_NOTIFY) {
                    // just ignore in case we stopped using damage but events keep coming in
                    if (use_damage) {
                        xcb_damage_notify_event_t *e = (xcb_damage_notify_event_t*) event;
                        FBLOG_TRACE(instance->ext_instance_ref, "XDamage notification received for 0x%08x", e->drawable);
                        update_available = true;
                        xcb_void_cookie = xcb_damage_subtract_checked(instance->xcb_conn, e->damage, XCB_NONE, XCB_NONE);
                        xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
                        if (xcb_err) {
                            FBLOG_WARN(instance->ext_instance_ref, "xcb_damage_subtract_checked failed to process update to 0x%08x, disabling XDamage; X11 error code %hu", e->drawable, xcb_err->error_code);
                            use_damage = false;
                            if (!destroy_damages(instance, &damages)) {
                                FBLOG_WARN(instance->ext_instance_ref, "failed to unregister some XDamage instances");
                            }
                            free(xcb_err);
                            xcb_err = NULL;
                        }
                    }
                } else if (event_type == XCB_CONFIGURE_NOTIFY) {
                    xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t*) event;
                    FBLOG_TRACE(instance->ext_instance_ref, "Configure notification received for 0x%08x", e->window);
                    if ((e->window == instance->window) && ((e->width != geom->width) || (e->height != geom->height))) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "Configure notification indicates size has changed from %dx%d to %dx%d", geom->width, geom->height, e->width, e->height);
                        window_configuration_changed = true;
                    }
                } else if (event_type == XCB_EXPOSE) {
                    xcb_expose_event_t *e = (xcb_expose_event_t*) event;
                    FBLOG_TRACE(instance->ext_instance_ref, "Expose notification received for 0x%08x", e->window);
                    if (e->window == instance->window) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "Window exposure changed (contents became unavailable or returned); reinitialization needed");
                        window_configuration_changed = true;
                    }
                } else if (event_type == XCB_MAP_NOTIFY) {
                    xcb_map_notify_event_t *e = (xcb_map_notify_event_t*) event;
                    FBLOG_TRACE(instance->ext_instance_ref, "Map notification received for 0x%08x", e->window);
                    if (e->window == instance->window) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "Mapping changed (mapped; compositing toggled on/off?); reinitialization needed");
                        window_configuration_changed = true;
                    }
                } else if (event_type == XCB_UNMAP_NOTIFY) {
                    xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t*) event;
                    FBLOG_TRACE(instance->ext_instance_ref, "Unmap notification received for 0x%08x", e->window);
                    if (e->window == instance->window) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "Mapping changed (unmapped; compositing toggled on/off?); reinitialization needed");
                        window_configuration_changed = true;
                    }
                } else if (event_type == XCB_DESTROY_NOTIFY) {
                    xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t*) event;
                    FBLOG_TRACE(instance->ext_instance_ref, "Destroy notification received for 0x%08x", e->window);
                    if (e->window == instance->window) {
                        FBLOG_DEBUG(instance->ext_instance_ref, "window has been destroyed; reinitialization needed");
                        window_configuration_changed = true;
                    }
                } else {
                    FBLOG_DEBUG(instance->ext_instance_ref, "unhandled event type: 0x%02x (%hu)", event->response_type, event->response_type);
                }

                free(event);
                event = NULL;
            }

            if (window_configuration_changed) {
                FBLOG_INFO(instance->ext_instance_ref, "window size or availability changed, reinitializing");
                break;
            }
            if (use_damage && !update_available) {
                // TODO: unfortunately XCB does not appear to have a timed method to wait for events, so we need to keep polling or we would not be able to properly shut down if no further events are received
                //wait_for_events(instance);
                thrd_yield();
            }
        }

        // if we got here we are reinitializing or stopping and need to reset
        uint32_t unsubscribe_event_mask_values[] = { XCB_NONE }; // TODO: check if removing the listener really works this way (taken over from our Xlib implementation where I wasn't sure either)
        xcb_void_cookie = xcb_change_window_attributes_checked(instance->xcb_conn, instance->window, XCB_CW_EVENT_MASK, &unsubscribe_event_mask_values);
        xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
        if (xcb_err) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to unsubscribe from window changes");
            free(xcb_err);
            xcb_err = NULL;
        }

        if (get_image_reply) {
            free(get_image_reply);
            get_image_reply = NULL;
        }

        if (back_buffer_pixmap_available) {
            xcb_void_cookie = xcb_free_pixmap_checked(instance->xcb_conn, back_buffer_pixmap);
            xcb_err = xcb_request_check(instance->xcb_conn, xcb_void_cookie);
            if (!xcb_err) {
                FBLOG_TRACE(instance->ext_instance_ref, "released back-buffer pixmap");
                back_buffer_pixmap = -1;
                back_buffer_pixmap_available = false;
            } else {
                FBLOG_WARN(instance->ext_instance_ref, "failed to release back-buffer pixmap; X11 error code %hu", xcb_err->error_code);
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

    if (geom) {
        free(geom);
        geom = NULL;
    }

    if (!destroy_damages(instance, &damages)) {
        FBLOG_WARN(instance->ext_instance_ref, "failed to unregister some XDamage instances during termination");
        free_damages_list(damages);
        damages = NULL;
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
    instance->allow_back_buffer = get_config_boolean(instance, "allowBackBuffer", true);
    instance->allow_damage = get_config_boolean(instance, "allowDamage", true);

    // connect to X server
    if (success) {
        instance->display_name = copy_string(get_config_pointer(instance, "display")); // defaults to environment if null
        instance->xcb_conn = xcb_connect(instance->display_name, NULL);
        if (!instance->xcb_conn) {
            FBLOG_WARN(instance->ext_instance_ref, "connection to X server display failed (%s)", instance->display_name ? instance->display_name : "default");
            success = false;
        }
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
