#include "../../framebuffet.h"

#ifndef FRAMEBUFFET_DLL_SHIM_H
#define FRAMEBUFFET_DLL_SHIM_H

#define FRAMEBUFFET_DLL_SHIM_HASH_LENGTH 64 /* never decrease, only increase if needed */

typedef int (*framebuffet_publish_num_buffers_f)(void *instance_ref, int num_buffers);
typedef int (*framebuffet_publish_update_f)(void *instance_ref, int buffer_id, fbsource_token_t token);

typedef fbcollections_map_entry_t* (*fbcollections_map_copy_with_payloads_f)(fbcollections_map_entry_t *entry);
typedef fbcollections_map_entry_t* (*fbcollections_map_find_key_f)(fbcollections_map_entry_t *entry, void *key, unsigned long key_size);
typedef void (*fbcollections_map_free_map_and_payloads_f)(fbcollections_map_entry_t *entry);

typedef struct {
    char _shim_hash[FRAMEBUFFET_DLL_SHIM_HASH_LENGTH];
    char _shim_loaded;
    
    framebuffet_publish_num_buffers_f framebuffet_publish_num_buffers;
    framebuffet_publish_update_f framebuffet_publish_update;
    
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
