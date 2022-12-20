#include "framebuffet_dll_shim.h"

/* This file needs to be compiled into the framework (not plugin modules)
 * to resolve the missing runtime symbol created by the shim header file.
 */
/*
int _framebuffet_dll_shim_complete(framebuffet_dll_shim_t runtime) {
    // this should never be called
    return FBERROR_UNSPECIFIC;
}

*/

// TODO: auto-generate; see framebuffet_dll_shim.c as well

const framebuffet_dll_shim_t _framebuffet_dll_shim_fwside = {
    ._shim_hash = "58ec4908232ee8f5dc796d6c8030346ef360d7827e5aadd80a9dd1b084739c1a",
    ._shim_loaded = 0,
    
    .framebuffet_publish_num_buffers = framebuffet_publish_num_buffers,
    .framebuffet_publish_update = framebuffet_publish_update,
    
    .fbcollections_map_copy_with_payloads = fbcollections_map_copy_with_payloads,
    .fbcollections_map_find_key = fbcollections_map_find_key,
    .fbcollections_map_free_map_and_payloads = fbcollections_map_free_map_and_payloads
};
