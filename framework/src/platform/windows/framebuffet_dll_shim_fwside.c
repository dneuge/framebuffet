#include "framebuffet_dll_shim.h"

// TODO: auto-generate; see framebuffet_dll_shim.c as well

static const framebuffet_dll_shim_t _framebuffet_dll_shim_fwside = {
    ._shim_hash = "58ec4908232ee8f5dc796d6c8030346ef360d7827e5aadd80a9dd1b084739c1a",
    ._shim_loaded = 0,
    
    .framebuffet_publish_num_buffers = framebuffet_publish_num_buffers,
    .framebuffet_publish_update = framebuffet_publish_update,
    .framebuffet_log_va = framebuffet_log_va,
    
    .framebuffet_get_source_instance = framebuffet_get_source_instance,
    .framebuffet_get_num_buffers = framebuffet_get_num_buffers,
    .framebuffet_get_buffer_configuration = framebuffet_get_buffer_configuration,
    .framebuffet_copy_latest_frame = framebuffet_copy_latest_frame,
    .framebuffet_wait_for_update_micros = framebuffet_wait_for_update_micros,
    
    .fbcollections_map_copy_with_payloads = fbcollections_map_copy_with_payloads,
    .fbcollections_map_find_key = fbcollections_map_find_key,
    .fbcollections_map_free_map_and_payloads = fbcollections_map_free_map_and_payloads
};
