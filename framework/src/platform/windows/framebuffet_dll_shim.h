#include "../../framebuffet.h"

#ifndef FRAMEBUFFET_DLL_SHIM_H
#define FRAMEBUFFET_DLL_SHIM_H

#define FRAMEBUFFET_DLL_SHIM_HASH_LENGTH 64 /* never decrease, only increase if needed */

typedef int (*framebuffet_publish_num_buffers_f)(void *instance_ref, int num_buffers);
typedef int (*framebuffet_publish_update_f)(void *instance_ref, int buffer_id, fbsource_token_t token);
typedef void (*framebuffet_log_va_f)(void *instance_ref, fblog_level_t level, char *format, va_list args);

typedef fbsource_module_instance_entry_t* (*framebuffet_get_source_instance_f)(framebuffet_t* framebuffet, char *instance_name);
typedef int (*framebuffet_get_num_buffers_f)(fbsource_module_instance_entry_t *source_instance);
typedef fbsource_buffer_config_t* (*framebuffet_get_buffer_configuration_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id);
typedef int (*framebuffet_copy_latest_frame_f)(fbsource_module_instance_entry_t *source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token);
typedef int (*framebuffet_wait_for_update_micros_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token, uint32_t microseconds);

typedef fbcollections_map_entry_t* (*fbcollections_map_copy_with_payloads_f)(fbcollections_map_entry_t *entry);
typedef fbcollections_map_entry_t* (*fbcollections_map_find_key_f)(fbcollections_map_entry_t *entry, void *key, unsigned long key_size);
typedef void (*fbcollections_map_free_map_and_payloads_f)(fbcollections_map_entry_t *entry);

typedef struct {
    char _shim_hash[FRAMEBUFFET_DLL_SHIM_HASH_LENGTH];
    char _shim_loaded;
    
    framebuffet_publish_num_buffers_f framebuffet_publish_num_buffers;
    framebuffet_publish_update_f framebuffet_publish_update;
    framebuffet_log_va_f framebuffet_log_va;
    
    framebuffet_get_source_instance_f framebuffet_get_source_instance;
    framebuffet_get_num_buffers_f framebuffet_get_num_buffers;
    framebuffet_get_buffer_configuration_f framebuffet_get_buffer_configuration;
    framebuffet_copy_latest_frame_f framebuffet_copy_latest_frame;
    framebuffet_wait_for_update_micros_f framebuffet_wait_for_update_micros;
    
    fbcollections_map_copy_with_payloads_f fbcollections_map_copy_with_payloads;
    fbcollections_map_find_key_f fbcollections_map_find_key;
    fbcollections_map_free_map_and_payloads_f fbcollections_map_free_map_and_payloads;
} framebuffet_dll_shim_t;

#ifdef __cplusplus
extern "C" {
#endif

int _framebuffet_dll_shim_complete(framebuffet_dll_shim_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
