#include <windows.h>

#include "framebuffet_dll_shim_fwside.h"
#include "framebuffet_dll_shim_fwside.c"

// FIXME: make this a proper module (requires framework to be split up)

static fberror_t complete_framebuffet_dll_shim(framebuffet_t *framebuffet, _framebuffet_dll_shim_complete_f dll_shim_complete) {
    framebuffet_dll_shim_t *copy = malloc(sizeof(framebuffet_dll_shim_t));
    if (!copy) {
        FBLOG_WARN("failed to allocate memory for copy of DLL shim map");
        return FBERROR_MEMORY_ALLOCATION;
    }

    memcpy(copy, &_framebuffet_dll_shim_fwside, sizeof(framebuffet_dll_shim_t));

    FBLOG_TRACE("Calling into DLL for shim completion... (Runtime pointer: %p)", copy);
    fberror_t err = dll_shim_complete(copy);
    FBLOG_TRACE("DLL shim completion finished with %d", err);

    free(copy);

    return err;
}

static fberror_t load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path) {
    *module_descriptor = NULL;

    HINSTANCE lib = LoadLibrary(file_path);
    if (!lib) {
        FBLOG_WARN("loading module from %s failed", file_path);
        return FBERROR_UNSPECIFIC;
    }

    char *module_descriptor_symbol = (char*) GetProcAddress(lib, "_fbsource_module_descriptor_symbol");
    if (!module_descriptor_symbol) {
        FBLOG_WARN("failed to get module descriptor symbol name from %s", file_path);
        return FBERROR_UNSPECIFIC;
    }

    *module_descriptor = (fbsource_module_descriptor_t*) GetProcAddress(lib, module_descriptor_symbol);
    if (!module_descriptor) {
        FBLOG_WARN("failed to get module descriptor %s from %s", module_descriptor_symbol, file_path);
        return FBERROR_UNSPECIFIC;
    }

    _framebuffet_dll_shim_complete_f dll_shim_complete = (_framebuffet_dll_shim_complete_f) GetProcAddress(lib, "_framebuffet_dll_shim_complete");
    if (!dll_shim_complete) {
        FBLOG_WARN("DLL is missing shim completion which is mandatory for dynamic module loading in Frame Buffet on Windows: %s", file_path);
        return FBERROR_UNSPECIFIC;
    }

    fberror_t err = complete_framebuffet_dll_shim(framebuffet, dll_shim_complete);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("DLL shim could not be completed (error %d) for %s (runtime reverse linking/DI failed)", err, file_path);
        return err;
    }
    
    return FBERROR_NONE;
}
