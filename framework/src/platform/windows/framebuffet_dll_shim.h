#include "../../framebuffet.h"

#ifndef FRAMEBUFFET_DLL_SHIM_H
#define FRAMEBUFFET_DLL_SHIM_H

#define FRAMEBUFFET_DLL_SHIM_HASH_LENGTH 64 /* never decrease, only increase if needed */

// plugin API from framebuffet.h
typedef fberror_t (*framebuffet_publish_num_buffers_f)(void *instance_ref, int num_buffers);
typedef fberror_t (*framebuffet_publish_update_f)(void *instance_ref, int buffer_id, fbsource_token_t token);
// intentionally omitted and reimplemented below: void framebuffet_log(void *instance_ref, fblog_level_t level, char *format, ...);
typedef void (*framebuffet_log_va_f)(void *instance_ref, fblog_level_t level, char *format, va_list args);
typedef bool (*framebuffet_is_log_level_enabled_f)(fbsource_module_instance_entry_t *module_instance_entry, fblog_level_t level);
typedef char* (*framebuffet_get_source_name_reference_f)(void *instance_ref);

// intentionally omitted: application API (not relevant for module-binding shim)

// general API from framebuffet.h
typedef fbsource_module_instance_entry_t* (*framebuffet_get_source_instance_f)(framebuffet_t* framebuffet, char *instance_name);
typedef fberror_t (*framebuffet_release_source_instance_f)(fbsource_module_instance_entry_t *source_instance);
typedef fberror_t (*framebuffet_get_source_configuration_f)(fbsource_module_instance_entry_t *source_instance, fbcollections_map_entry_t **config);
typedef int (*framebuffet_get_num_buffers_f)(fbsource_module_instance_entry_t *source_instance);
typedef fberror_t (*framebuffet_get_buffer_configuration_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_buffer_config_t *buffer_config);
typedef fberror_t (*framebuffet_copy_latest_frame_f)(fbsource_module_instance_entry_t *source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token);
typedef bool (*framebuffet_has_update_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token);
typedef fberror_t (*framebuffet_wait_for_update_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token);
typedef fberror_t (*framebuffet_wait_for_update_micros_f)(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token, uint32_t microseconds);

// from fbcollections.h
typedef fbcollections_map_entry_t* (*fbcollections_map_create_f)();
typedef fbcollections_map_entry_t* (*fbcollections_map_copy_with_payloads_f)(fbcollections_map_entry_t *entry);
typedef fbcollections_map_entry_t* (*fbcollections_map_find_key_f)(fbcollections_map_entry_t *entry, void *key, unsigned long key_size);
typedef int (*fbcollections_map_put_f)(fbcollections_map_entry_t *entry, fbcollections_map_entry_t **old_entry, void *key, unsigned long key_size, void *value, unsigned long value_size);
typedef int (*fbcollections_map_put_and_free_old_entry_with_payloads_f)(fbcollections_map_entry_t *entry, void *key, unsigned long key_size, void *value, unsigned long value_size);
typedef fbcollections_map_entry_t* (*fbcollections_map_remove_f)(fbcollections_map_entry_t *entry, void *key, unsigned long key_size);
typedef void (*fbcollections_map_free_entry_and_payloads_f)(fbcollections_map_entry_t *entry);
typedef void (*fbcollections_map_free_map_and_payloads_f)(fbcollections_map_entry_t *entry);
typedef fbcollections_map_entry_t* (*fbcollections_map_tail_f)(fbcollections_map_entry_t *entry);

// from fbutil.h
// intentionally omitted and reimplemented below: void fbutil_set_current_thread_name(char *format, ...);
typedef void (*fbutil_set_current_thread_name_va_f)(char *format, va_list args);

typedef struct {
    char _shim_hash[FRAMEBUFFET_DLL_SHIM_HASH_LENGTH];
    char _shim_loaded;
    
    framebuffet_publish_num_buffers_f framebuffet_publish_num_buffers;
    framebuffet_publish_update_f framebuffet_publish_update;
    framebuffet_log_va_f framebuffet_log_va;
    framebuffet_is_log_level_enabled_f framebuffet_is_log_level_enabled;
    framebuffet_get_source_name_reference_f framebuffet_get_source_name_reference;
    
    framebuffet_get_source_instance_f framebuffet_get_source_instance;
    framebuffet_release_source_instance_f framebuffet_release_source_instance;
    framebuffet_get_source_configuration_f framebuffet_get_source_configuration;
    framebuffet_get_num_buffers_f framebuffet_get_num_buffers;
    framebuffet_get_buffer_configuration_f framebuffet_get_buffer_configuration;
    framebuffet_copy_latest_frame_f framebuffet_copy_latest_frame;
    framebuffet_has_update_f framebuffet_has_update;
    framebuffet_wait_for_update_f framebuffet_wait_for_update;
    framebuffet_wait_for_update_micros_f framebuffet_wait_for_update_micros;

    fbcollections_map_create_f fbcollections_map_create;
    fbcollections_map_copy_with_payloads_f fbcollections_map_copy_with_payloads;
    fbcollections_map_find_key_f fbcollections_map_find_key;
    fbcollections_map_put_f fbcollections_map_put;
    fbcollections_map_put_and_free_old_entry_with_payloads_f fbcollections_map_put_and_free_old_entry_with_payloads;
    fbcollections_map_remove_f fbcollections_map_remove;
    fbcollections_map_free_entry_and_payloads_f fbcollections_map_free_entry_and_payloads;
    fbcollections_map_free_map_and_payloads_f fbcollections_map_free_map_and_payloads;
    fbcollections_map_tail_f fbcollections_map_tail;
    
    fbutil_set_current_thread_name_va_f fbutil_set_current_thread_name_va;
} framebuffet_dll_shim_t;

#ifdef __cplusplus
extern "C" {
#endif

FBLINK_EXPORT fberror_t _framebuffet_dll_shim_complete(framebuffet_dll_shim_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
