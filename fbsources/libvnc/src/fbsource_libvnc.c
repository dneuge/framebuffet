#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>

#include <stdbool.h>

#include <rfb/rfbclient.h>

#include <framebuffet.h>

#define VNC_DEFAULT_PORT 5900
#define VNC_DEFAULT_CONNECT_TIMEOUT_SECONDS 5
#define VNC_DEFAULT_READ_TIMEOUT_SECONDS 1

#define VNC_BITS_PER_SAMPLE 8
#define VNC_SAMPLES_PER_PIXEL 3
#define VNC_BYTES_PER_PIXEL 3 /* TODO: documentation says we need a "multiple of eight" and comes out at 4 = 32 bits instead of 3 = 24 bits */

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

    unsigned int frame_token;
    fbcollections_map_entry_t* source_config;

    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;

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

static int set_wanted_connection_state(fbsource_libvnc_t *instance, vnc_state_t state) {
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    instance->wanted_connection_state = state;

    cnd_broadcast(&instance->connection_state_wait);
    
    mtx_unlock(&instance->mutex); // TODO: how to handle an error?
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
    
    printf("vnc_malloc_frame_buffer\n"); // DEBUG

    if (client->width <= 0 || client->height <= 0 || client->format.bitsPerPixel != 3 * 8) {
        printf("bad format requested by libvncclient: width=%d, height=%d, bitsPerPixel=%d\n", client->width, client->height, client->format.bitsPerPixel); // DEBUG
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

    // allocate buffers
    instance->buffers[0] = (unsigned char*) malloc(instance->buffer_size);
    instance->buffers[1] = (unsigned char*) malloc(instance->buffer_size);
    instance->client_buffer = (unsigned char*) malloc(instance->buffer_size);

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
    }

    bool success = (instance->buffer_size > 0);
    if (success) {
        printf("vnc_malloc_frame_buffer: success\n"); // DEBUG
        client->frameBuffer = instance->client_buffer;
    } else {
        printf("vnc_malloc_frame_buffer: failed\n"); // DEBUG
    }

    mtx_unlock(&instance->mutex);

    return success ? RFBBOOL_TRUE : RFBBOOL_FALSE;
}

static void vnc_finished_frame_buffer_update(rfbClient *client) {
    fbsource_libvnc_t *instance = rfbClientGetClientData(client, NULL);

    printf("vnc_finished_frame_buffer_update\n"); // DEBUG
    
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return;
    }

    // TODO: check if holding the mutex during memcpy has a noticeable performance hit and find a better way if it has
    // TODO: double-buffering on fbsource doesn't really make sense if we hold the mutex?

    int work_buffer = (instance->complete_buffer + 1) % 2;
    unsigned char *buffer = instance->buffers[work_buffer];
    if (buffer) {
        memcpy(buffer, instance->client_buffer, instance->buffer_size);
        instance->complete_buffer = work_buffer;
        instance->frame_token++;
    } else {
        printf("vnc_finished_frame_buffer_update: no buffer!\n"); // DEBUG
    }

    mtx_unlock(&instance->mutex); // TODO: how to handle an error?
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
    rfbClient *client = rfbGetClient(VNC_BITS_PER_SAMPLE, VNC_SAMPLES_PER_PIXEL, VNC_BYTES_PER_PIXEL);
    client->MallocFrameBuffer = vnc_malloc_frame_buffer;
    client->canHandleNewFBSize = RFBBOOL_TRUE;
    //client->GotFrameBufferUpdate = vnc_got_frame_buffer_update;
    client->FinishedFrameBufferUpdate = vnc_finished_frame_buffer_update;
    client->GetPassword = vnc_get_password;
    rfbClientSetClientData(client, NULL, instance);

    if (mtx_lock(&instance->mutex) != thrd_success || !instance->hostname || instance->port <= 0) {
        printf("create_rfb_client: failed\n"); // DEBUG
        destroy_client(client);
        return NULL;
    }

    client->connectTimeout = instance->connect_timeout_seconds;
    client->readTimeout = instance->read_timeout_seconds;
    client->serverHost = copy_string(instance->hostname);
    client->serverPort = instance->port;
    
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
        //printf("client loop, before: actual=%d, wanted=%d\n", instance->actual_connection_state, wanted_state); // DEBUG

        // handle requested state changes
        if (wanted_state != VNC_STATE_CLEARED) {
            printf("client loop, state transition wanted: actual=%d, wanted=%d\n", instance->actual_connection_state, wanted_state); // DEBUG
            if (wanted_state == VNC_STATE_TERMINATED) {
                // client thread is asked to terminate
                printf("client loop terminates\n"); // DEBUG
                break;
            } else if (wanted_state == VNC_STATE_DISCONNECTED) {
                // we should disconnect
                printf("client loop disconnects\n"); // DEBUG
                destroy_client(client);
                client = NULL;
                instance->actual_connection_state = VNC_STATE_DISCONNECTED;
            } else if (wanted_state == VNC_STATE_CONNECTED) {
                // we should (re)connect
                if (client) {
                    // we may still be connected, disconnect first
                    printf("client loop disconnects for reconnect\n"); // DEBUG
                    destroy_client(client);
                }
                
                printf("client loop initializes connection\n"); // DEBUG
                client = create_rfb_client(instance);

                if (!client) {
                    printf("client failed to initialize\n"); // DEBUG
                }

                // in case of success we transition to "connecting" state, otherwise
                // to "disconnected" as there is no hope to succeed by retrying if we failed that early
                instance->actual_connection_state = client ? VNC_STATE_CONNECTING : VNC_STATE_DISCONNECTED;
            }
        }

        // in "connecting" state (re)try to connect to the server
        if (instance->actual_connection_state == VNC_STATE_CONNECTING) {
            printf("client loop connects\n"); // DEBUG

            if (rfbInitClient(client, NULL, NULL)) {
                printf("client loop: connected\n"); // DEBUG
                instance->actual_connection_state = VNC_STATE_CONNECTED;
            } else {
                printf("client loop: connection failed\n"); // DEBUG

                // unfortunately client has been freed by rfbInitClient at this point,
                // we need to fully reinitialize
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
            /*
            if (ConnectToRFBServer(client, client->serverHost, client->serverPort)) {
                printf("client loop: connected\n"); // DEBUG
                if (InitialiseRFBConnection(client)) {
                    // we are now connected - keep trying on next iteration in case the connection attempt failed
                    printf("client loop: connected & RFB initialised\n"); // DEBUG
                    instance->actual_connection_state = VNC_STATE_CONNECTED;
                }
            }
            */
        }

        // in "connected" state run libvnc's event processing
        if (instance->actual_connection_state == VNC_STATE_CONNECTED) {
            //printf("client loop processes VNC events\n"); // DEBUG
            need_reset = FALSE;
            res = WaitForMessage(client, VNC_WAIT_TIMEOUT_MICROSECONDS);
            if (res < 0) {
                // something went wrong
                need_reset = TRUE;
            } else if (res) {
                // we got a message to process
                if (!HandleRFBServerMessage(client)) {
                    // something else went wrong
                    need_reset = TRUE;
                }
            }

            // In case one of the above commands failed we need to reconnect.
            // This is performed by queueing a request for reconnect on next iteration.
            if (need_reset) {
                printf("client loop wants to reset VNC\n"); // DEBUG
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

        //printf("client loop, after: actual=%d, wanted=%d\n", instance->actual_connection_state, wanted_state); // DEBUG
        
        // clear the requested state transition if processed
        if (wanted_state != VNC_STATE_CLEARED && wanted_state != VNC_STATE_ERROR && wanted_state == instance->wanted_connection_state) {
            printf("client loop clears wanted state\n"); // DEBUG
            if (mtx_lock(&instance->mutex) == thrd_success) {
                if (wanted_state == instance->wanted_connection_state) {
                    instance->wanted_connection_state = VNC_STATE_CLEARED;
                }
                mtx_unlock(&instance->mutex);
            } else {
                // if mutex lock failed we still need to clear the requested state
                if (wanted_state == instance->wanted_connection_state) {
                    instance->wanted_connection_state = VNC_STATE_CLEARED;
                }
            }
        }

        // go to sleep until a state change is requested in case we are disconnected and have nothing to do
        if (instance->actual_connection_state == VNC_STATE_DISCONNECTED && instance->wanted_connection_state == VNC_STATE_CLEARED) {
            // it looks like we don't have anything to do, lock the mutex to check that's really the case
            printf("client loop goes to sleep\n"); // DEBUG
            if (mtx_lock(&instance->mutex) == thrd_success) {
                if (wanted_state == VNC_STATE_CLEARED) {
                    // confirmed that we have nothing to do, wait indefinitely
                    cnd_wait(&instance->connection_state_wait, &instance->mutex);
                }
                mtx_unlock(&instance->mutex);
            }
            printf("client loop continues\n"); // DEBUG
        }
    }

    printf("client loop destroys client\n"); // DEBUG
    destroy_client(client);
    
    instance->actual_connection_state = VNC_STATE_TERMINATED;
}

static int fblibvnc_create(framebuffet_t *framebuffet, void **data) {
    rfbClientLog = rfbClientErr = printf; // FIXME: add some logger, if possible scoped to instance (is that possible?)
    rfbEnableClientLogging = RFBBOOL_TRUE;
    
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

    printf("hostname=%s, port=%d, password=%s, connectTimeout=%ds, readTimeout=%ds\n", instance->hostname, instance->port, instance->password, instance->connect_timeout_seconds, instance->read_timeout_seconds); // DEBUG
    
    if (success) {
        printf("config success\n"); // DEBUG
        set_wanted_connection_state(instance, VNC_STATE_CONNECTED);
        return FBERROR_NONE;
    } else {
        printf("config failed\n"); // DEBUG
        set_wanted_connection_state(instance, VNC_STATE_DISCONNECTED);
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

static fbsource_buffer_config_t* fblibvnc_get_buffer_configuration(void *data, int buffer_id) {
    fbsource_libvnc_t *instance = data;
  
    if (buffer_id != 0) {
        // this module only generates a single buffer
        return NULL;
    }

    fbsource_buffer_config_t *out = malloc(sizeof(fbsource_buffer_config_t));
    if (out == NULL) {
        return NULL;
    }

    memset(out, 0, sizeof(fbsource_buffer_config_t));

    if (mtx_lock(&instance->mutex) != thrd_success) {
        free(out);
        return NULL;
    }
  
    out->width = instance->width;
    out->height = instance->height;
    out->size = instance->buffer_size;

    mtx_unlock(&instance->mutex); // TODO: handle error?
  
    return out;
}

static int fblibvnc_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, unsigned int *token) {
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

static int fblibvnc_has_update(void *data, int buffer_id, unsigned int last_token) {
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
