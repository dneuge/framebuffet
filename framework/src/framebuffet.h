#ifndef FRAMEBUFFET_H
#define FRAMEBUFFET_H

#include <stdarg.h>
#include <stdint.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include "fbcollections.h"

// license identification constants
#define FBMODULE_LICENSE_MIT "MIT" /* MIT license */
#define FBMODULE_LICENSE_GPL2 "GPL2" /* suits "GPL 2" and "GPL 2 or later" licenses */

/**
 * @def FBERROR_NONE
 * @brief error code indicating no error has occurred
 */
#define FBERROR_NONE 0

/**
 * @def FBERROR_UNSPECIFIC
 * @brief error code indicating an unspecific error has occurred
 */
#define FBERROR_UNSPECIFIC 1

/**
 * @def FBERROR_BUFFER_TOO_SMALL
 * @brief error code indicating the provided buffer was too small to fit an image copy; the buffer may or may not have been updated
 */
#define FBERROR_BUFFER_TOO_SMALL 2

/**
 * @def FBERROR_NO_FRAME
 * @brief error code indicating no frame was available; buffer may be blank or not have been written to at all
 */
#define FBERROR_NO_FRAME 3

/**
 * @def FBERROR_NO_SUCH_BUFFER
 * @brief error code indicating the requested buffer does not exist
 */
#define FBERROR_NO_SUCH_BUFFER 4

/**
 * @def FBERROR_MEMORY_ALLOCATION
 * @brief error code indicating memory allocation failed
 */
#define FBERROR_MEMORY_ALLOCATION 5

/**
 * @def FBLOG_LEVEL_ERROR
 * @brief Messages logged with this level describe critical errors which prevent further operation (recovery is unlikely).
 */
#define FBLOG_LEVEL_ERROR 127

/**
 * @def FBLOG_LEVEL_WARN
 * @brief Messages logged with this level describe errors which impact operation but might be possible to be recovered from.
 */
#define FBLOG_LEVEL_WARN 63

/**
 * @def FBLOG_LEVEL_INFO
 * @brief used for any messages that are deemed useful to inform users of the module or framework about
 */
#define FBLOG_LEVEL_INFO 31

/**
 * @def FBLOG_LEVEL_DEBUG
 * @brief used for regular debugging messages
 */
#define FBLOG_LEVEL_DEBUG 15

/**
 * @def FBLOG_LEVEL_TRACE
 * @brief used for detailed debugging
 */
#define FBLOG_LEVEL_TRACE 7

#define FBLOG_MESSAGE_BUFFER_SIZE 4096

#ifndef FBLOG_COMPILED_MIN_LOG_LEVEL
#define FBLOG_COMPILED_MIN_LOG_LEVEL FBLOG_LEVEL_INFO
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_ERROR)
#define FBLOG_ERROR(ref, format, ...) ;framebuffet_log(ref, FBLOG_LEVEL_ERROR, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_ERROR(ref, format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_WARN)
#define FBLOG_WARN(ref, format, ...) ;framebuffet_log(ref, FBLOG_LEVEL_WARN, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_WARN(ref, format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_INFO)
#define FBLOG_INFO(ref, format, ...) ;framebuffet_log(ref, FBLOG_LEVEL_INFO, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_INFO(ref, format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_DEBUG)
#define FBLOG_DEBUG(ref, format, ...) ;framebuffet_log(ref, FBLOG_LEVEL_DEBUG, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_DEBUG(ref, format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_TRACE)
#define FBLOG_TRACE(ref, format, ...) ;framebuffet_log(ref, FBLOG_LEVEL_TRACE, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_TRACE(ref, format, ...) ;
#endif

typedef uint8_t fblog_level_t;

/**
 * Will be called for every message not filtered out by currently set minimum log level.
 *
 * The message must only be read, not written to and not be freed. The message will be freed by
 * the framework when this callback returns, so the message contents must be copied in
 * case it should be referred to after this call ends.
 *
 * @param level log level of the message
 * @param message fully formatted message, null-terminated; must only be read
 */
typedef void (*fblog_receiver_f)(fblog_level_t level, char *message);

typedef struct {
    fbcollections_map_entry_t *modules;
    fbcollections_map_entry_t *configurations;
    fbcollections_map_entry_t *sources;

    // this root-level mutex is only used to protect main data structures,
    // not for frame updates or logging
    mtx_t mutex;

    fblog_receiver_f log_receiver;
    fblog_level_t minimum_log_level;
} framebuffet_t;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned long size;
} fbsource_buffer_config_t;

typedef int fbsource_token_t;

/**
 * Will be called by the framework to create a new module instance.
 * The framework provides a pointer to its own structure in case the module
 * wants to use the framework on its own. The pointer is only provided on
 * construction so it should be saved if needed at any later point.
 * Do not call directly when using the framework!
 * 
 * @param framebuffet the framework instance that requested instantiation
 * @param instance_ref reference pointer used by the framework; do not read or manipulate, only store and return to framework where indicated
 * @param data module instance data to be allocated and set by the module as needed, will be provided on further calls addressing this instance; initially NULL
 * @return error code, use FBERROR defines
 */
typedef int (*fbsource_create_f)(framebuffet_t *framebuffet, void *instance_ref, void **data);

/**
 * Will be called by the framework to shutdown and deallocate a module instance.
 * In general this should never fail. Returning any error code will be interpreted
 * as undefined behaviour with a near guaranteed memory/resource leak by framework and callers
 * and thus may lead to termination of the application; only use if absolutely necessary.
 * Do not call directly when using the framework!
 *
 * Memory/access constraints:
 * - The module must free all memory of this instance.
 * - The framework will no longer keep track of the data pointer after this call.
 *
 * @param data module instance data as set on creation
 * @return error code, use FBERROR defines
 */
typedef int (*fbsource_destroy_f)(void *data);

/**
 * Configures the module instance. The instance is supposed to be ready to be used
 * afterwards. Reconfiguration of an instance is allowed at any time.
 * The map has to hold key-value pairs of null-terminated char* ("strings") as payloads.
 *
 * Memory/access constraints:
 * - The module shall copy/apply the required information but not manipulate nor free the original config.
 * - The module must not keep references to map payloads as they may get destroyed shortly after this call.
 * - The caller must null-terminate all char*.
 * - The caller must provide independent map and payloads, it must not share any self-owned pointers with the module.
 * - The caller shall free the config incl. payloads when no longer needed.
 *
 * @param data module instance data as set on creation
 * @param config configuration data as a map from char* to char* ("strings")
 * @return error code, use FBERROR defines
 */
typedef int (*fbsource_configure_module_f)(void *data, fbcollections_map_entry_t *config);

/**
 * Creates a copy of the current configuration of the module instance.
 * The map holds key-value pairs of null-terminated char* ("strings") as payloads.
 *
 * Memory/access constraints:
 * - The module shall allocate the required data structure and set the config pointer accordingly.
 * - The module must not read from given config pointer.
 * - The module must null-terminate all char*.
 * - The module must create independent map and payloads, it must not share any self-owned pointers with the caller.
 * - The module must not leave any memory allocated when indicating an error.
 * - The caller shall take ownership of the config map and free it incl. its payloads when no longer needed.
 *
 * @param data module instance data as set on creation
 * @param config_entries a NULL terminated array of key-value pairs holding configuration data
 * @return error code, use FBERROR defines
 */
typedef int (*fbsource_get_module_config_f)(void *data, fbcollections_map_entry_t **config);

/**
 * Returns the number of frame buffers available from the module instance.
 *
 * @param data module instance data as set on creation
 * @return number of frame buffers available from the instance
 */
typedef int (*fbsource_get_num_buffers_f)(void *data);

/**
 * Writes the configuration of the queried framebuffer of a module instance to the provided structure.
 * Call get_num_buffers to retrieve the number of available framebuffers of an instance.
 *
 * Memory/access constraints:
 * - The caller must disregard any information stored on buffer_config if the call indicates an error.
 *
 * @param data module instance data as set on creation
 * @param buffer_id buffer to be queried, counting from 0
 * @param buffer_config a pointer to the data structure to write the configuration of the queried frame buffer into
 * @return error code, see FBERROR defines
 */
typedef int (*fbsource_get_buffer_config_f)(void *data, int buffer_id, fbsource_buffer_config_t *buffer_config);

/**
 * Copies the latest available frame to the specified destination address.
 * The token is only written to, not read by this function. Callers should use has_update 
 * Call get_num_buffers to retrieve the number of available framebuffers of an instance.
 *
 * Memory/access constraints:
 * - The module always writes a complete image to the destination unless a memory error occurred.
 * - The module must return an error if the image could not be copied.
 * - The caller must ensure proper allocation of the buffer to be written to.
 *
 * @param data module instance data as set on creation
 * @param destination memory location to copy the frame to
 * @param buffer_size the maximum size addressable from the destination address
 * @param buffer_id buffer to be copied, counting from 0
 * @param token can be used to perform a light-weight check if a new frame is available before invoking this function
 * @return error code, see FBERROR defines
 */
typedef int (*fbsource_copy_latest_frame_f)(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token);

/**
 * Checks if an updated frame is available.
 * Use the token received by a previous copy operation.
 * Implementations are not required to support this operation in which case
 * they will always indicate presence of a new frame.
 *
 * @param data module instance data as set on creation
 * @param buffer_id buffer to be copied, counting from 0
 * @param last_token token last received from copy operation
 * @return non-zero if an updated frame is available, zero if not
 */
typedef int (*fbsource_has_update_f)(void *data, int buffer_id, fbsource_token_t last_token);

#define FRAMEBUFFET_MODULE_API_VERSION 1

typedef struct {
    // functions to be implemented by modules
    // see type descriptions for details
    fbsource_create_f _create;
    fbsource_destroy_f _destroy;
    fbsource_configure_module_f configure_module;
    fbsource_get_num_buffers_f get_num_buffers;
    fbsource_get_module_config_f get_module_configuration;
    fbsource_get_buffer_config_f get_buffer_configuration;
    fbsource_copy_latest_frame_f copy_latest_frame;
    fbsource_has_update_f has_update;
} fbsource_module_t;

typedef struct {
    uint8_t api_version;
    char *name;
    char *license;
    fbsource_module_t ops;
} fbsource_module_descriptor_t;

typedef fbsource_module_t (*fbsource_init_f)(void);

#define FBSOURCE_MODULE_DESCRIPTOR(descriptorid, modulename, modulelicense, moduleops) ; \
    static char _fbsource_module_name__##descriptorid[]=modulename;     \
    static char _fbsource_module_license__##descriptorid[]=modulelicense; \
    const char _fbsource_module_descriptor_symbol[]="_fbsource_module_descriptor__"#descriptorid; \
    const fbsource_module_descriptor_t _fbsource_module_descriptor__##descriptorid = { \
        .api_version = FRAMEBUFFET_MODULE_API_VERSION,                  \
        .name = _fbsource_module_name__##descriptorid,                  \
        .license = _fbsource_module_license__##descriptorid,            \
        .ops = moduleops                                                \
    };

#ifdef FBSOURCE_BUILD_STATIC_MODULE
/* modules that should be statically linked must omit _fbsource_module_descriptor_symbol,
   else linking more than one module together will break */
#undef FBSOURCE_MODULE_DESCRIPTOR
#define FBSOURCE_MODULE_DESCRIPTOR(descriptorid, modulename, modulelicense, moduleops) ; \
    static char _fbsource_module_name__##descriptorid[]=modulename;     \
    static char _fbsource_module_license__##descriptorid[]=modulelicense; \
    const fbsource_module_descriptor_t _fbsource_module_descriptor__##descriptorid = { \
        .api_version = FRAMEBUFFET_MODULE_API_VERSION,                  \
        .name = _fbsource_module_name__##descriptorid,                  \
        .license = _fbsource_module_license__##descriptorid,            \
        .ops = moduleops                                                \
    };
#endif

typedef struct {
    fbsource_module_t *module;
    char *name;
    char *license;
} fbsource_module_registration_t;


typedef struct {
    // always lock the mutex and back off if destruction_pending
    uint8_t destruction_pending;

    fbsource_token_t token;
    mtx_t mutex;
    cnd_t wait;
} fbsource_module_instance_frame_update_t;

typedef struct {
    framebuffet_t *framebuffet;
    char *log_prefix;
    uint16_t log_prefix_length; // strlen omitting null termination

    fbsource_module_registration_t *module;
    char *instance_name;
    void *data;

    mtx_t mutex;
    fbsource_module_instance_frame_update_t *frame_updates;
    int num_buffers;
} fbsource_module_instance_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

// API meant only for fbsource modules to publish updates to framework
int framebuffet_publish_num_buffers(void *instance_ref, int num_buffers);
int framebuffet_publish_update(void *instance_ref, int buffer_id, fbsource_token_t token);

void framebuffet_log(void *instance_ref, fblog_level_t level, char *format, ...);
void framebuffet_log_va(void *instance_ref, fblog_level_t level, char *format, va_list args);

// API for general use
framebuffet_t* framebuffet_init();
int framebuffet_destroy(framebuffet_t *framebuffet);

void framebuffet_provide_log_receiver(framebuffet_t *framebuffet, fblog_receiver_f log_receiver);
int framebuffet_set_minimum_log_level(framebuffet_t *framebuffet, fblog_level_t level);

int framebuffet_load_module(framebuffet_t *framebuffet, char *file_path);
//int framebuffet_load_modules(framebuffet_t* framebuffet, char *directory_path);

int framebuffet_configure_from_actions_json(framebuffet_t* framebuffet, char *json_string);
int framebuffet_configure_from_actions_json_file(framebuffet_t* framebuffet, char *file_path);

int framebuffet_register_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t module_descriptor);

int framebuffet_set_configuration(framebuffet_t *framebuffet, char *config_name, fbcollections_map_entry_t *config);
int framebuffet_get_configuration(framebuffet_t *framebuffet, char *config_name, fbcollections_map_entry_t **config);

int framebuffet_create_source_instance(framebuffet_t *framebuffet, fbsource_module_instance_entry_t **source_instance, char *module_name, char *instance_name);
fbsource_module_instance_entry_t* framebuffet_get_source_instance(framebuffet_t* framebuffet, char *instance_name);

int framebuffet_configure_source_instance(fbsource_module_instance_entry_t *source_instance, fbcollections_map_entry_t *config);
int framebuffet_get_source_configuration(fbsource_module_instance_entry_t *source_instance, fbcollections_map_entry_t **config);

int framebuffet_get_num_buffers(fbsource_module_instance_entry_t *source_instance);
int framebuffet_get_buffer_configuration(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_buffer_config_t *buffer_config);
int framebuffet_copy_latest_frame(fbsource_module_instance_entry_t *source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token);
int framebuffet_has_update(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token);
int framebuffet_wait_for_update(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token);
int framebuffet_wait_for_update_micros(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token, uint32_t microseconds);

#ifdef __cplusplus
}
#endif

#endif
