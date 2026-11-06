#include "framebuffet_dll_shim.h"

// TODO: auto-generate; see framebuffet_dll_shim.c as well

static const framebuffet_dll_shim_t _framebuffet_dll_shim_fwside = {
    ._shim_hash = "10fdd7f1ac069270f72f38fab4a31bb1ca8bc902e038de10961848411e103755",
    ._shim_loaded = 0,
    
    .framebuffet_publish_num_buffers = framebuffet_publish_num_buffers,
    .framebuffet_publish_update = framebuffet_publish_update,
    .framebuffet_log_va = framebuffet_log_va,
    .framebuffet_is_log_level_enabled = framebuffet_is_log_level_enabled,
    .framebuffet_get_source_name_reference = framebuffet_get_source_name_reference,
    
    .framebuffet_get_source_instance = framebuffet_get_source_instance,
    .framebuffet_release_source_instance = framebuffet_release_source_instance,
    .framebuffet_get_source_configuration = framebuffet_get_source_configuration,
    .framebuffet_get_num_buffers = framebuffet_get_num_buffers,
    .framebuffet_get_buffer_configuration = framebuffet_get_buffer_configuration,
    .framebuffet_copy_latest_frame = framebuffet_copy_latest_frame,
    .framebuffet_has_update = framebuffet_has_update,
    .framebuffet_wait_for_update = framebuffet_wait_for_update,
    .framebuffet_wait_for_update_micros = framebuffet_wait_for_update_micros,

    .fbcollections_map_create = fbcollections_map_create,
    .fbcollections_map_copy_with_payloads = fbcollections_map_copy_with_payloads,
    .fbcollections_map_find_key = fbcollections_map_find_key,
    .fbcollections_map_put = fbcollections_map_put,
    .fbcollections_map_put_and_free_old_entry_with_payloads = fbcollections_map_put_and_free_old_entry_with_payloads,
    .fbcollections_map_remove = fbcollections_map_remove,
    .fbcollections_map_free_entry_and_payloads = fbcollections_map_free_entry_and_payloads,
    .fbcollections_map_free_map_and_payloads = fbcollections_map_free_map_and_payloads,
    .fbcollections_map_tail = fbcollections_map_tail,
    
    .fbutil_set_current_thread_name_va = fbutil_set_current_thread_name_va
};
