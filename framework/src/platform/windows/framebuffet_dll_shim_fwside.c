#include "framebuffet_dll_shim.h"

// TODO: auto-generate; see framebuffet_dll_shim.c as well

static const framebuffet_dll_shim_t _framebuffet_dll_shim_fwside = {
    ._shim_hash = "64fb30489418615ac27cb76544721f262ac5256b6e9d7432dd5407f53032873d",
    ._shim_loaded = 0,
    
    .framebuffet_publish_num_buffers = framebuffet_publish_num_buffers,
    .framebuffet_publish_update = framebuffet_publish_update,
    .framebuffet_log_va = framebuffet_log_va,
    .framebuffet_get_source_name_reference = framebuffet_get_source_name_reference,
    
    .framebuffet_get_source_instance = framebuffet_get_source_instance,
    .framebuffet_get_num_buffers = framebuffet_get_num_buffers,
    .framebuffet_get_buffer_configuration = framebuffet_get_buffer_configuration,
    .framebuffet_copy_latest_frame = framebuffet_copy_latest_frame,
    .framebuffet_wait_for_update_micros = framebuffet_wait_for_update_micros,
    
    .fbcollections_map_copy_with_payloads = fbcollections_map_copy_with_payloads,
    .fbcollections_map_find_key = fbcollections_map_find_key,
    .fbcollections_map_free_map_and_payloads = fbcollections_map_free_map_and_payloads,
    
    .fbutil_set_current_thread_name_va = fbutil_set_current_thread_name_va
};
