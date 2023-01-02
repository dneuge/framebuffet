#include "framebuffet_dll_shim.h"

// TODO: auto-generate; see framebuffet_dll_shim.c as well

static const framebuffet_dll_shim_t _framebuffet_dll_shim_fwside = {
    ._shim_hash = "a2efb12e785f274b1797ce9217f8940b9090a0a1b33c52721d52e4dd16c64093",
    ._shim_loaded = 0,
    
    .framebuffet_publish_num_buffers = framebuffet_publish_num_buffers,
    .framebuffet_publish_update = framebuffet_publish_update,
    .framebuffet_log_va = framebuffet_log_va,
    .framebuffet_get_source_name_reference = framebuffet_get_source_name_reference,
    
    .framebuffet_get_source_instance = framebuffet_get_source_instance,
    .framebuffet_release_source_instance = framebuffet_release_source_instance,
    .framebuffet_get_num_buffers = framebuffet_get_num_buffers,
    .framebuffet_get_buffer_configuration = framebuffet_get_buffer_configuration,
    .framebuffet_copy_latest_frame = framebuffet_copy_latest_frame,
    .framebuffet_wait_for_update_micros = framebuffet_wait_for_update_micros,
    
    .fbcollections_map_copy_with_payloads = fbcollections_map_copy_with_payloads,
    .fbcollections_map_find_key = fbcollections_map_find_key,
    .fbcollections_map_free_map_and_payloads = fbcollections_map_free_map_and_payloads,
    
    .fbutil_set_current_thread_name_va = fbutil_set_current_thread_name_va
};
