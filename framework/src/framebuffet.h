#ifndef FRAMEBUFFET_H
#define FRAMEBUFFET_H

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


typedef struct {
  fbcollections_map_entry_t *modules;
  fbcollections_map_entry_t *config;
  fbcollections_map_entry_t *sources;
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
 * @param data module instance data to be allocated and set by the module as needed, will be provided on further calls addressing this instance; initially NULL
 * @return error code, use FBERROR defines
 */
typedef int (*fbsource_create_f)(framebuffet_t *framebuffet, void **data);

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
 * Returns a new data structure holding the configuration of the queried framebuffer on a module instance.
 * Call get_num_buffers to retrieve the number of available framebuffers of an instance.
 *
 * Memory/access constraints:
 * - The module must allocate the result to be returned.
 * - The module must not leave any memory allocated when returning NULL.
 * - The caller must expect NULL to be returned if the queried framebuffer is not available.
 * - The caller must free the result when no longer needed.
 *
 * @param data module instance data as set on creation
 * @param buffer_id buffer to be queried, counting from 0
 * @return configuration of the queried frame buffer; NULL if unavailable
 */
typedef fbsource_buffer_config_t* (*fbsource_get_buffer_config_f)(void *data, int buffer_id);

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
typedef int (*fbsource_copy_latest_frame_f)(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, unsigned int *token);

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
typedef int (*fbsource_has_update_f)(void *data, int buffer_id, unsigned int last_token);

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

typedef fbsource_module_t (*fbsource_init_f)(void);

#define _FBSOURCE_MODULE_NAME _fbsource_module_name
#define FBSOURCE_MODULE_NAME(name) ;const char _FBSOURCE_MODULE_NAME[]=name;

#define _FBSOURCE_MODULE_LICENSE _fbsource_module_license
#define FBSOURCE_MODULE_LICENSE(license) ;const char _FBSOURCE_MODULE_LICENSE[]=license;

#define _FBSOURCE_MODULE_INIT _fbsource_module_init
#define FBSOURCE_MODULE_INIT(module_init) ;const fbsource_init_f _FBSOURCE_MODULE_INIT=module_init;


typedef struct {
  fbsource_module_t *module;
  char *name;
  char *license;
} fbsource_module_registration_t;

typedef struct {
  fbsource_module_registration_t *module;
  char *instance_name;
  void *data;
} fbsource_module_instance_entry_t;

framebuffet_t* framebuffet_init();
int framebuffet_destroy(framebuffet_t *framebuffet);

int framebuffet_load_module(framebuffet_t *framebuffet, char *file_path);
//int framebuffet_load_modules(framebuffet_t* framebuffet, char *directory_path);
//int framebuffet_load_configuration(framebuffet_t* framebuffet, char *file_path);

int framebuffet_create_source_instance(framebuffet_t *framebuffet, fbsource_module_instance_entry_t **source_instance, char *module_name, char *instance_name);
//fbsource_module_instance_entry_t* framebuffet_get_source_instance(framebuffet_t* framebuffet, char *instance_name);

int framebuffet_configure_source_instance(fbsource_module_instance_entry_t *source_instance, fbcollections_map_entry_t *config);
int framebuffet_get_source_configuration(fbsource_module_instance_entry_t *source_instance, fbcollections_map_entry_t **config);

int framebuffet_get_num_buffers(fbsource_module_instance_entry_t *source_instance);
fbsource_buffer_config_t* framebuffet_get_buffer_configuration(fbsource_module_instance_entry_t *source_instance, int buffer_id);
int framebuffet_copy_latest_frame(fbsource_module_instance_entry_t *source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, unsigned int *token);
int framebuffet_has_update(fbsource_module_instance_entry_t *source_instance, int buffer_id, unsigned int last_token);

#endif
