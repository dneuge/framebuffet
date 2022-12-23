#include <stdlib.h>
#include <string.h>

#ifdef HAVE_C11_THREADS
#include <time.h>
#include <threads.h>
#else
#include <c11/time.h>
#include <c11/threads.h>
#endif

#include <stdbool.h>

#include <rfb/rfbclient.h>

#include "../../../framework/src/framebuffet.h"

#define VNC_DEFAULT_PORT 5900
#define VNC_DEFAULT_CONNECT_TIMEOUT_SECONDS 5
#define VNC_DEFAULT_READ_TIMEOUT_SECONDS 1

#define VNC_BITS_PER_SAMPLE 8
#define VNC_SAMPLES_PER_PIXEL 3
#define VNC_BYTES_PER_PIXEL 4 /* 4 = 32 bits, setting 3 = 24 bits leads to "Rect too large" error messages */

/* VNC states are essentially controlling a statemachine:
 * Controlling threads read the actual_connection_state and make requests by setting wanted_connection_state.
 * Client thread reacts to wanted_connection_state and resets it to CLEARED once it has picked up that request.
 *
 * - initially a client is DISCONNECTED.
 * - CONNECTED is requested when everything was set up; the client will change to CONNECTING
 * - when done, the actual state changes to CONNECTED
 * - the client will automatically attempt to reconnect (state changes back to CONNECTING in that case)
 * - DISCONNECTED can be requested to... disconnect (surprise, surprise)
 * - when connection parameters change, a request has to be made for CONNECTED state again
 * - TERMINATED is requested to shut down the thread; this should only be done by source instance destructor
 *
 * The thread is expected to exist throughout the lifetime of the source instance.
 */
// states are defined as bits of an unsigned byte to make connection state atomic while keeping them lock-free
#define VNC_STATE_CLEARED 0
#define VNC_STATE_DISCONNECTED (1 << 0)
#define VNC_STATE_CONNECTING (1 << 1) /* for information only, don't set from outside client loop */
#define VNC_STATE_CONNECTED (1 << 2)
#define VNC_STATE_TERMINATED (1 << 3) /* internal, don't set from outside client loop */
#define VNC_STATE_ERROR (1 << 7) /* internal, don't set from outside client loop */
typedef uint8_t vnc_state_t;

#define VNC_WAIT_TIMEOUT_MICROSECONDS (500 * 1000)

// see rfbproto.h
#define RFBBOOL_FALSE 0
#define RFBBOOL_TRUE -1

typedef struct {
    mtx_t mutex;

    void *ext_instance_ref;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;
    unsigned int num_pixels;

    char *hostname;
    int port;
    char *password;
    int connect_timeout_seconds;
    int read_timeout_seconds;

    unsigned char *buffers[2];
    int complete_buffer;

    thrd_t client_thread;
    unsigned char *client_buffer;
    vnc_state_t actual_connection_state;
    vnc_state_t wanted_connection_state;
    cnd_t connection_state_wait;
} fbsource_libvnc_t;

static tss_t *rfb_log_data_key = NULL;

static const struct timespec failed_connection_delay = {
    // TODO: make configurable
    .tv_sec = 2,
    .tv_nsec = 0
};

static int set_wanted_connection_state(fbsource_libvnc_t *instance, vnc_state_t state) {
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    instance->wanted_connection_state = state;

    cnd_broadcast(&instance->connection_state_wait);
    
    mtx_unlock(&instance->mutex); // TODO: how to handle an error?

    return FBERROR_NONE;
}

static void destroy_client(rfbClient *client) {
    if (!client) {
        return;
    }

    if (client->serverHost != NULL) {
        free(client->serverHost);
        client->serverHost = NULL;
    }

    // the framebuffer is registered to the fbsource instance and should be managed from there, not here

    rfbClientCleanup(client);
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

static rfbBool vnc_malloc_frame_buffer(rfbClient *client) {
    fbsource_libvnc_t *instance = rfbClientGetClientData(client, NULL);
    
    FBLOG_TRACE(instance->ext_instance_ref, "vnc_malloc_frame_buffer");

    if (client->width <= 0 || client->height <= 0 || client->format.bitsPerPixel != 4 * 8) {
        FBLOG_WARN(instance->ext_instance_ref, "bad format requested by libvncclient: width=%d, height=%d, bitsPerPixel=%d", client->width, client->height, client->format.bitsPerPixel);
        return RFBBOOL_FALSE;
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return RFBBOOL_FALSE;
    }

    // release old buffers
    if (instance->buffer_size > 0) {
        free(instance->buffers[0]);
        free(instance->buffers[1]);
        free(instance->client_buffer);
    }
  
    instance->buffers[0] = NULL;
    instance->buffers[1] = NULL;
    instance->client_buffer = NULL;
    client->frameBuffer = NULL;

    instance->complete_buffer = 0;

    // apply configuration
    instance->width = client->width;
    instance->height = client->height;
    instance->buffer_size = instance->width * instance->height * 3;
    instance->num_pixels = instance->width * instance->height;
    int client_buffer_size = instance->num_pixels * 4;

    // allocate buffers
    instance->buffers[0] = (unsigned char*) malloc(instance->buffer_size);
    instance->buffers[1] = (unsigned char*) malloc(instance->buffer_size);
    instance->client_buffer = (unsigned char*) malloc(client_buffer_size);

    // reset on error (buffers remain unallocated if input is wrong)
    if (!instance->buffers[0] || !instance->buffers[1] || !instance->client_buffer) {
        if (instance->buffers[0]) {
            free(instance->buffers[0]);
            instance->buffers[0] = NULL;
        }

        if (instance->buffers[1]) {
            free(instance->buffers[1]);
            instance->buffers[1] = NULL;
        }

        if (instance->client_buffer) {
            free(instance->client_buffer);
            instance->client_buffer = NULL;
        }

        instance->width = 0;
        instance->height = 0;
        instance->buffer_size = 0;
        instance->num_pixels = 0;
    }

    bool success = (instance->buffer_size > 0);
    if (success) {
        FBLOG_TRACE(instance->ext_instance_ref, "vnc_malloc_frame_buffer: success");
        client->frameBuffer = instance->client_buffer;
    } else {
        FBLOG_TRACE(instance->ext_instance_ref, "vnc_malloc_frame_buffer: failed");
    }

    mtx_unlock(&instance->mutex);

    return success ? RFBBOOL_TRUE : RFBBOOL_FALSE;
}

static void vnc_finished_frame_buffer_update(rfbClient *client) {
    fbsource_libvnc_t *instance = rfbClientGetClientData(client, NULL);

    //FBLOG_TRACE(instance->ext_instance_ref, "vnc_finished_frame_buffer_update");

    // Locking should not be necessary until we switch the buffer because
    // the only way to resize buffers is through the client event loop which
    // is currently busy calling this code.
    
    int work_buffer = (instance->complete_buffer + 1) % 2;
    unsigned char *buffer = instance->buffers[work_buffer];
    unsigned char *client_buffer = instance->client_buffer;
    int numPixels = instance->num_pixels;
    if (!buffer) {
        FBLOG_WARN(instance->ext_instance_ref, "vnc_finished_frame_buffer_update: no buffer!");
    } else {
        // Unfortunately 24bpp does not work, so we need to read 32bpp and skip
        // every 4th byte...
        int in = 0;
        int out = 0;
        for (unsigned int i=0; i<numPixels; i++) {
            buffer[out++] = client_buffer[in++]; // Red
            buffer[out++] = client_buffer[in++]; // Green
            buffer[out++] = client_buffer[in++]; // Blue
            in++;                                // Fill Byte
        }

        if (mtx_lock(&instance->mutex) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "vnc_finished_frame_buffer_update: failed to get lock; dropping frame");
            return;
        }

        instance->complete_buffer = work_buffer;
        instance->frame_token++;
        
        mtx_unlock(&instance->mutex); // TODO: how to handle an error?

        framebuffet_publish_update(instance->ext_instance_ref, 0, instance->frame_token);
    }
}

static char* vnc_get_password(rfbClient *client) {
    fbsource_libvnc_t *instance = rfbClientGetClientData(client, NULL);

    if (mtx_lock(&instance->mutex) != thrd_success || !instance->password) {
        return NULL;
    }

    // API documentation states that memory will be managed (i.e. freed) by libvnc
    char *password = copy_string(instance->password);
    
    mtx_unlock(&instance->mutex);

    return password;
}

static rfbClient* create_rfb_client(fbsource_libvnc_t *instance) {
    if (!rfb_log_data_key) {
        FBLOG_WARN(instance->ext_instance_ref, "create_rfb_client: no TSS key for logging; messages from libvncclient will not be available through Frame Buffet");
    } else {
        if (tss_set(*rfb_log_data_key, instance) != thrd_success) {
            FBLOG_WARN(instance->ext_instance_ref, "create_rfb_client: failed to store reference in TSS; messages from libvncclient will not be available through Frame Buffet");
        }
    }
    
    rfbClient *client = rfbGetClient(VNC_BITS_PER_SAMPLE, VNC_SAMPLES_PER_PIXEL, VNC_BYTES_PER_PIXEL);
    client->MallocFrameBuffer = vnc_malloc_frame_buffer;
    client->canHandleNewFBSize = RFBBOOL_TRUE;
    //client->GotFrameBufferUpdate = vnc_got_frame_buffer_update;
    client->FinishedFrameBufferUpdate = vnc_finished_frame_buffer_update;
    client->GetPassword = vnc_get_password;
    rfbClientSetClientData(client, NULL, instance);

    if (mtx_lock(&instance->mutex) != thrd_success || !instance->hostname || instance->port <= 0) {
        FBLOG_WARN(instance->ext_instance_ref, "create_rfb_client: failed");
        destroy_client(client);
        return NULL;
    }

    client->connectTimeout = instance->connect_timeout_seconds;
    client->readTimeout = instance->read_timeout_seconds;
    client->serverHost = copy_string(instance->hostname);
    client->serverPort = instance->port;

    client->appData.shareDesktop = RFBBOOL_TRUE;
    client->appData.viewOnly = RFBBOOL_TRUE;
    client->appData.enableJPEG = RFBBOOL_FALSE; // TODO: make configurable
    
    mtx_unlock(&instance->mutex);

    return client;
}

static int client_loop(void *data) {
    fbsource_libvnc_t *instance = data;
    int res;
    bool need_reset;
    vnc_state_t wanted_state;
    rfbClient *client = NULL;
    
    while (true) {
        wanted_state = instance->wanted_connection_state;
        FBLOG_TRACE(instance->ext_instance_ref, "client loop, before: actual=%d, wanted=%d", instance->actual_connection_state, wanted_state);

        // handle requested state changes
        if (wanted_state != VNC_STATE_CLEARED) {
            FBLOG_DEBUG(instance->ext_instance_ref, "client loop, state transition wanted: actual=%d, wanted=%d", instance->actual_connection_state, wanted_state);
            if (wanted_state == VNC_STATE_TERMINATED) {
                // client thread is asked to terminate
                FBLOG_DEBUG(instance->ext_instance_ref, "client loop terminates");
                break;
            } else if (wanted_state == VNC_STATE_DISCONNECTED) {
                // we should disconnect
                FBLOG_INFO(instance->ext_instance_ref, "client loop disconnects");
                destroy_client(client);
                client = NULL;
                instance->actual_connection_state = VNC_STATE_DISCONNECTED;
            } else if (wanted_state == VNC_STATE_CONNECTED) {
                // we should (re)connect
                if (client) {
                    // we may still be connected, disconnect first
                    FBLOG_INFO(instance->ext_instance_ref, "client loop disconnects for reconnect");
                    destroy_client(client);
                }
                
                FBLOG_DEBUG(instance->ext_instance_ref, "client loop initializes connection");
                client = create_rfb_client(instance);

                if (!client) {
                    FBLOG_WARN(instance->ext_instance_ref, "client failed to initialize");
                }

                // in case of success we transition to "connecting" state, otherwise
                // to "disconnected" as there is no hope to succeed by retrying if we failed that early
                instance->actual_connection_state = client ? VNC_STATE_CONNECTING : VNC_STATE_DISCONNECTED;
            }
        }

        // in "connecting" state (re)try to connect to the server
        if (instance->actual_connection_state == VNC_STATE_CONNECTING) {
            FBLOG_INFO(instance->ext_instance_ref, "client loop connects");

            if (rfbInitClient(client, NULL, NULL)) {
                FBLOG_INFO(instance->ext_instance_ref, "client loop: connected");
                instance->actual_connection_state = VNC_STATE_CONNECTED;
            } else {
                FBLOG_INFO(instance->ext_instance_ref, "client loop: connection failed");
                
                // unfortunately client has been freed by rfbInitClient at this point...
                client = NULL;

                // ... so we need to fully reinitialize
                if (mtx_lock(&instance->mutex) == thrd_success) {
                    if (instance->wanted_connection_state == VNC_STATE_CLEARED) {
                        instance->wanted_connection_state = VNC_STATE_CONNECTED;
                    }
                    mtx_unlock(&instance->mutex);
                } else {
                    // if mutex lock failed we still need to request a reconnect
                    if (instance->wanted_connection_state == VNC_STATE_CLEARED) {
                        instance->wanted_connection_state = VNC_STATE_CONNECTED;
                    }
                }

                // prevent state clearing
                wanted_state = VNC_STATE_ERROR;

                // wait a moment to not run berzerk if repeated connection failures happen with very low delay
                thrd_sleep(&failed_connection_delay, NULL);
            }
            /*
            if (ConnectToRFBServer(client, client->serverHost, client->serverPort)) {
                FBLOG_DEBUG(instance->ext_instance_ref, "client loop: connected");
                if (InitialiseRFBConnection(client)) {
                    // we are now connected - keep trying on next iteration in case the connection attempt failed
                    FBLOG_DEBUG(instance->ext_instance_ref, "client loop: connected & RFB initialised");
                    instance->actual_connection_state = VNC_STATE_CONNECTED;
                }
            }
            */
        }

        // in "connected" state run libvnc's event processing
        if (instance->actual_connection_state == VNC_STATE_CONNECTED) {
            //FBLOG_TRACE(instance->ext_instance_ref, "client loop processes VNC events");
            need_reset = FALSE;
            res = WaitForMessage(client, VNC_WAIT_TIMEOUT_MICROSECONDS);
            if (res < 0) {
                // something went wrong
                FBLOG_WARN(instance->ext_instance_ref, "WaitForMessage failed");
                need_reset = TRUE;
            } else if (res) {
                // we got a message to process
                if (!HandleRFBServerMessage(client)) {
                    // something else went wrong
                    FBLOG_WARN(instance->ext_instance_ref, "HandleRFBServerMessage failed");
                    need_reset = TRUE;
                }
            }

            // In case one of the above commands failed we need to reconnect.
            // This is performed by queueing a request for reconnect on next iteration.
            if (need_reset) {
                FBLOG_DEBUG(instance->ext_instance_ref, "client loop wants to reset VNC");
                instance->actual_connection_state = VNC_STATE_CONNECTING;

                if (mtx_lock(&instance->mutex) == thrd_success) {
                    if (instance->wanted_connection_state == VNC_STATE_CLEARED) {
                        instance->wanted_connection_state = VNC_STATE_CONNECTED;
                    }
                    mtx_unlock(&instance->mutex);
                } else {
                    // if mutex lock failed we still need to request a reconnect
                    if (instance->wanted_connection_state == VNC_STATE_CLEARED) {
                        instance->wanted_connection_state = VNC_STATE_CONNECTED;
                    }
                }

                // prevent state clearing
                wanted_state = VNC_STATE_ERROR;
            }
        }

        FBLOG_TRACE(instance->ext_instance_ref, "client loop, after: actual=%d, wanted=%d", instance->actual_connection_state, wanted_state);
        
        // clear the requested state transition if processed
        if (wanted_state != VNC_STATE_CLEARED && wanted_state != VNC_STATE_ERROR && wanted_state == instance->wanted_connection_state) {
            FBLOG_DEBUG(instance->ext_instance_ref, "client loop clears wanted state");
            if (mtx_lock(&instance->mutex) == thrd_success) {
                if (wanted_state == instance->wanted_connection_state) {
                    instance->wanted_connection_state = VNC_STATE_CLEARED;
                }
                mtx_unlock(&instance->mutex);
            } else {
                // if mutex lock failed we still need to clear the requested state
                FBLOG_WARN(instance->ext_instance_ref, "client loop failed to lock mutex to clear wanted state");
                if (wanted_state == instance->wanted_connection_state) {
                    instance->wanted_connection_state = VNC_STATE_CLEARED;
                }
            }
        }

        // go to sleep until a state change is requested in case we are disconnected and have nothing to do
        if (instance->actual_connection_state == VNC_STATE_DISCONNECTED && instance->wanted_connection_state == VNC_STATE_CLEARED) {
            // it looks like we don't have anything to do, lock the mutex to check that's really the case
            FBLOG_DEBUG(instance->ext_instance_ref, "client loop goes to sleep");
            if (mtx_lock(&instance->mutex) == thrd_success) {
                if (instance->wanted_connection_state == VNC_STATE_CLEARED) {
                    // confirmed that we have nothing to do, wait indefinitely
                    cnd_wait(&instance->connection_state_wait, &instance->mutex);
                }
                mtx_unlock(&instance->mutex);
            }
            FBLOG_DEBUG(instance->ext_instance_ref, "client loop continues");
        }
    }

    FBLOG_DEBUG(instance->ext_instance_ref, "client loop destroys client");
    destroy_client(client);
    
    instance->actual_connection_state = VNC_STATE_TERMINATED;
}

static void rfb_log(fblog_level_t level, const char *format, va_list args) {
    fbsource_libvnc_t *instance = NULL;

    // TODO: check if log level is enabled before processing
    
    if (rfb_log_data_key) {
        instance = tss_get(*rfb_log_data_key);
    }

    if (!instance) {
        // fallback to just stdout/stderr using original format
        FILE *stream = ((level == FBLOG_LEVEL_WARN) || (level == FBLOG_LEVEL_ERROR)) ? stderr : stdout;
        vfprintf(stream, format, args);
        return;
    }

    char *internal_format = NULL;

    // the hostname we add to the format string must not contain % as it would get interpreted by *printf
    char *safe_hostname = copy_string(instance->hostname ? instance->hostname : "null");
    if (safe_hostname) {
        int hostname_length = strlen(safe_hostname);
        for (int i=0; i<hostname_length; i++) {
            if (safe_hostname[i] == '%') {
                safe_hostname[i] = '_';
            }
        }

        int format_length = strlen(format);
        int internal_format_size = format_length
            + 17 /* "[libvncclient ", ":" and "] " */
            + hostname_length
            + (sizeof(int) * 3) + 1 /* not accurate but greater than necessary to fit all formatted port numbers including a minuts character for negative ones */
            + 1; /* null termination */
            
        internal_format = malloc(internal_format_size);
        if (internal_format) {
            snprintf(internal_format, internal_format_size, "[libvncclient %s:%d] %s", safe_hostname, instance->port, format);
            internal_format[internal_format_size - 1] = 0;
            
            // remove trailing line breaks from libvnc's format string
            for (int i=strlen(internal_format)-1; i>0; i--) {
                char ch = internal_format[i];
                if ((ch == '\r') || (ch == '\n')) {
                    internal_format[i] = 0;
                } else {
                    break;
                }
            }
        }
    }
        
    // submit to framework
    char *actual_format = internal_format ? internal_format : (char*) format;
    framebuffet_log_va(instance->ext_instance_ref, level, actual_format, args);
        
    if (safe_hostname) {
        free(safe_hostname);
    }
    
    if (internal_format) {
        free(internal_format);
    }
}

static void rfb_log_normal(const char *format, ...) {
    va_list args;
    va_start(args, format);

    rfb_log(FBLOG_LEVEL_DEBUG, format, args);

    va_end(args);
}

static void rfb_log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);

    rfb_log(FBLOG_LEVEL_WARN, format, args);

    va_end(args);    
}

static int fblibvnc_create(framebuffet_t *framebuffet, void *instance_ref, void **data) {
    // libvncclient only has a single global logger configuration but we need to log instance-based
    // we use thread-local storage to store a reference only visible to the thread actually running
    // the client
    // NOTE: this is not thread-safe, avoid concurrent instantiation
    if (rfb_log_data_key) {
        FBLOG_DEBUG(instance_ref, "TSS key for logging is already present");
    } else {
        FBLOG_DEBUG(instance_ref, "requesting TSS key for logging");
        rfb_log_data_key = malloc(sizeof(tss_t));
        if (!rfb_log_data_key) {
            FBLOG_WARN(instance_ref, "failed to allocate memory for TSS key for logging");
        } else if (tss_create(rfb_log_data_key, NULL) != thrd_success) {
            FBLOG_WARN(instance_ref, "failed to create TSS key for logging");
            free(rfb_log_data_key);
            rfb_log_data_key = NULL;
        } else {
            FBLOG_DEBUG(instance_ref, "bind libvncclient log functions");
            rfbClientLog = rfb_log_normal;
            rfbClientErr = rfb_log_error;

            FBLOG_DEBUG(instance_ref, "enable libvncclient logging");
            rfbEnableClientLogging = RFBBOOL_TRUE;
            
            FBLOG_DEBUG(instance_ref, "instance-based libvncclient logging is enabled");
        }
    }

    fbsource_libvnc_t *instance = malloc(sizeof(fbsource_libvnc_t));
    if (instance == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memset(instance, 0, sizeof(fbsource_libvnc_t));

    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    if (cnd_init(&instance->connection_state_wait) != thrd_success) {
        mtx_destroy(&instance->mutex);
        free(instance);
        return FBERROR_UNSPECIFIC;
    }

    instance->ext_instance_ref = instance_ref;
    instance->wanted_connection_state = VNC_STATE_CLEARED;
    instance->actual_connection_state = VNC_STATE_DISCONNECTED;
    instance->connect_timeout_seconds = VNC_DEFAULT_CONNECT_TIMEOUT_SECONDS;
    instance->read_timeout_seconds = VNC_DEFAULT_READ_TIMEOUT_SECONDS;
    
    if (thrd_create(&instance->client_thread, client_loop, instance) != thrd_success) {
        cnd_destroy(&instance->connection_state_wait);
        mtx_destroy(&instance->mutex);
        free(instance);
        return FBERROR_UNSPECIFIC;
    }
    
    *data = instance;

    framebuffet_publish_num_buffers(instance_ref, 1);
    
    return FBERROR_NONE;
}

static int fblibvnc_destroy(void *data) {
    fbsource_libvnc_t *instance = data;
    int out_error = FBERROR_NONE;
  
    if (instance == NULL) {
        return FBERROR_NONE;
    }

    set_wanted_connection_state(instance, VNC_STATE_TERMINATED);
    int thread_res;
    if (thrd_join(instance->client_thread, &thread_res) != thrd_success) {
        // TODO: what can be done if joining the thread fails?
        out_error = FBERROR_UNSPECIFIC;
    }

    fbcollections_map_free_map_and_payloads(instance->source_config);

    if (instance->buffers[0]) {
        free(instance->buffers[0]);
    }

    if (instance->buffers[1]) {
        free(instance->buffers[1]);
    }

    // FIXME: make sure nothing is waiting for the mutex before it gets destroyed
    cnd_destroy(&instance->connection_state_wait);
    mtx_destroy(&instance->mutex);
    free(instance);
  
    return out_error;
}

static void* get_config_pointer(fbsource_libvnc_t *instance, char *key) {
    int key_size = strlen(key) + 1;
  
    fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

static int get_config_int(fbsource_libvnc_t *instance, char *key, int default_value) {
    char *value = get_config_pointer(instance, key);
    
    if (value == NULL) {
        return default_value;
    }

    return atoi(value);
}

static int fblibvnc_configure_module(void *data, fbcollections_map_entry_t *config) {
    fbsource_libvnc_t *instance = data;

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_free_map_and_payloads(instance->source_config);
    instance->source_config = fbcollections_map_copy_with_payloads(config);
    if (instance->source_config == NULL) {
        mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error
        return FBERROR_UNSPECIFIC;
    }

    if (instance->hostname) {
        free(instance->hostname);
    }
    instance->hostname = copy_string(get_config_pointer(instance, "hostname"));
    
    if (instance->password) {
        free(instance->password);
    }
    instance->password = copy_string(get_config_pointer(instance, "password"));
    
    instance->port = get_config_int(instance, "port", VNC_DEFAULT_PORT);
    instance->connect_timeout_seconds = get_config_int(instance, "connectTimeoutSeconds", VNC_DEFAULT_CONNECT_TIMEOUT_SECONDS);
    instance->read_timeout_seconds = get_config_int(instance, "readTimeoutSeconds", VNC_DEFAULT_READ_TIMEOUT_SECONDS);

    bool success = (instance->port > 0) && (instance->connect_timeout_seconds >= 0) && (instance->read_timeout_seconds >= 0) && instance->hostname && instance->password;

    mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error

    FBLOG_TRACE(instance->ext_instance_ref, "hostname=%s, port=%d, connectTimeout=%ds, readTimeout=%ds", instance->hostname, instance->port, instance->connect_timeout_seconds, instance->read_timeout_seconds);
    
    if (success) {
        FBLOG_DEBUG(instance->ext_instance_ref, "config success");
        return set_wanted_connection_state(instance, VNC_STATE_CONNECTED);
    } else {
        FBLOG_DEBUG(instance->ext_instance_ref, "config failed");
        int err = set_wanted_connection_state(instance, VNC_STATE_DISCONNECTED);
        if (err != FBERROR_NONE) {
            FBLOG_WARN(instance->ext_instance_ref, "failed to update wanted connection state to 'disconnected' after configuration failure (error: %d)", err);
        }
        return FBERROR_UNSPECIFIC;
    }
}

static int fblibvnc_get_num_buffers(void *data) {
    // this module only generates a single buffer
    return 1;
}

static int fblibvnc_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
    fbsource_libvnc_t *instance = data;
    *config = fbcollections_map_copy_with_payloads(instance->source_config);
    return FBERROR_NONE;
}

static int fblibvnc_get_buffer_configuration(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config) {
    fbsource_libvnc_t *instance = data;

    if (!buffer_config) {
        FBLOG_WARN(instance->ext_instance_ref, "get_buffer_configuration was called with null buffer_config");
        return FBERROR_UNSPECIFIC;
    }
  
    if (buffer_id != 0) {
        // this module only generates a single buffer
        return FBERROR_NO_SUCH_BUFFER;
    }

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }
  
    buffer_config->width = instance->width;
    buffer_config->height = instance->height;
    buffer_config->size = instance->buffer_size;

    mtx_unlock(&instance->mutex); // TODO: handle error?
  
    return FBERROR_NONE;
}

static int fblibvnc_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    fbsource_libvnc_t *instance = data;
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

static int fblibvnc_has_update(void *data, int buffer_id, fbsource_token_t last_token) {
    if (buffer_id != 0) {
        return FALSE;
    }
  
    fbsource_libvnc_t *instance = data;
    return (last_token != instance->frame_token);
}

static const fbsource_module_t fblibvnc_module = {
    ._create = fblibvnc_create,
    ._destroy = fblibvnc_destroy,
    .configure_module = fblibvnc_configure_module,
    .get_num_buffers = fblibvnc_get_num_buffers,
    .get_module_configuration = fblibvnc_get_module_configuration,
    .get_buffer_configuration = fblibvnc_get_buffer_configuration,
    .copy_latest_frame = fblibvnc_copy_latest_frame,
    .has_update = fblibvnc_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(fblibvnc, "LibVNC", FBMODULE_LICENSE_GPL2, fblibvnc_module)
