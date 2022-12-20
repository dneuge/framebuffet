#include <stdio.h>

#include "framebuffet_dll_shim.h"

/* What the $&!% is this?!
 * 
 * Windows requires all DLLs to already know where to get their symbols from at compile-time.
 *
 * To support dynamic loading of module DLLs that actually call into framework code that might
 * not be available from the same framework DLL used during compile-time (e.g. if the framework
 * has been compiled statically into something else or is just in some other location) loading
 * a module DLL would either fail due to unsatisfied dependencies or it might end up using a
 * potentially different framework implementation than the framework itself.
 *
 * To work around that issue, Windows DLL modules should link statically to this shim instead of
 * the actual framework, thus removing any compile-time external framework dependencies.
 *
 * When Frame Buffet loads a module on Windows it will look up the _framebuffet_dll_shim_complete
 * symbol exported by the DLL and provide it with a framebuffet_dll_shim_t pointing to its
 * internal functions. A pre-calculated hash generated during compile-time is used to check for
 * API compatibility. The shim is only allowed to be filled once and must not hold any null pointers.
 */

// TODO: auto-generate from exported symbols and .h files
// TODO: auto-update shim hash; currently: cat src/fbcollections.h src/framebuffet.h | sha256sum

static framebuffet_dll_shim_t _framebuffet_dll_shim = {
    ._shim_hash = "58ec4908232ee8f5dc796d6c8030346ef360d7827e5aadd80a9dd1b084739c1a",
    ._shim_loaded = 0,
    0
};

static void link_shim(framebuffet_dll_shim_t *runtime) {
    _framebuffet_dll_shim.framebuffet_publish_num_buffers = runtime->framebuffet_publish_num_buffers;
    _framebuffet_dll_shim.framebuffet_publish_update = runtime->framebuffet_publish_update;

    _framebuffet_dll_shim.fbcollections_map_copy_with_payloads = runtime->fbcollections_map_copy_with_payloads;
    _framebuffet_dll_shim.fbcollections_map_find_key = runtime->fbcollections_map_find_key;
    _framebuffet_dll_shim.fbcollections_map_free_map_and_payloads = runtime->fbcollections_map_free_map_and_payloads;    
}

static const framebuffet_dll_shim_t zero_shim = {0};

static void clear_shim() {
    link_shim((framebuffet_dll_shim_t*) &zero_shim);
}

static int check_shim_complete(framebuffet_dll_shim_t *shim) {
    if (!shim) return 0;
    
    if (!shim->framebuffet_publish_num_buffers) return 0;
    if (!shim->framebuffet_publish_update) return 0;
    
    if (!shim->fbcollections_map_copy_with_payloads) return 0;
    if (!shim->fbcollections_map_find_key) return 0;
    if (!shim->fbcollections_map_free_map_and_payloads) return 0;

    return 1;
}

int _framebuffet_dll_shim_complete(framebuffet_dll_shim_t *runtime) {
    if (!runtime) {
        printf("Frame Buffet DLL shim received NULL pointer from runtime\r\n");
        return FBERROR_UNSPECIFIC;
    }
    
    if (_framebuffet_dll_shim._shim_loaded) {
        printf("Frame Buffet DLL shim has already been completed, refusing to relink\r\n");
        return FBERROR_UNSPECIFIC;
    }

    if (memcmp(_framebuffet_dll_shim._shim_hash, runtime->_shim_hash, FRAMEBUFFET_DLL_SHIM_HASH_LENGTH)) {
        printf("Frame Buffet DLL shim received bad hash, refusing to link\r\n");
        return FBERROR_UNSPECIFIC;
    }

    if (!check_shim_complete(runtime)) {
        printf("Frame Buffet DLL shim received incomplete pointers, refusing to link\r\n");
        return FBERROR_UNSPECIFIC;
    }

    link_shim(runtime);
    
    if (!check_shim_complete(&_framebuffet_dll_shim)) {
        printf("Frame Buffet DLL shim broke during application, resetting\r\n");
        clear_shim();
        return FBERROR_UNSPECIFIC;
    }

    _framebuffet_dll_shim._shim_loaded = 1;
    
    return FBERROR_NONE;
}

fbcollections_map_entry_t* fbcollections_map_copy_with_payloads(fbcollections_map_entry_t *entry) {
    if (!_framebuffet_dll_shim._shim_loaded) return NULL;
    return _framebuffet_dll_shim.fbcollections_map_copy_with_payloads(entry);
}

fbcollections_map_entry_t* fbcollections_map_find_key(fbcollections_map_entry_t *entry, void *key, unsigned long key_size) {
    if (!_framebuffet_dll_shim._shim_loaded) return NULL;
    return _framebuffet_dll_shim.fbcollections_map_find_key(entry, key, key_size);
}

void fbcollections_map_free_map_and_payloads(fbcollections_map_entry_t *entry) {
    if (!_framebuffet_dll_shim._shim_loaded) return;
    _framebuffet_dll_shim.fbcollections_map_free_map_and_payloads(entry);
}

int framebuffet_publish_num_buffers(void *instance_ref, int num_buffers) {
    if (!_framebuffet_dll_shim._shim_loaded) return FBERROR_UNSPECIFIC;
    return _framebuffet_dll_shim.framebuffet_publish_num_buffers(instance_ref, num_buffers);
}

int framebuffet_publish_update(void *instance_ref, int buffer_id, fbsource_token_t token) {
    if (!_framebuffet_dll_shim._shim_loaded) return FBERROR_UNSPECIFIC;
    return _framebuffet_dll_shim.framebuffet_publish_update(instance_ref, buffer_id, token);
}
