#include <windows.h>

// FIXME: make this a proper module (requires framework to be split up)

static int load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path) {
    *module_descriptor = NULL;
    
    HINSTANCE lib = LoadLibrary(file_path);
    if (!lib) {
        FBLOG_WARN("loading module from %s failed", file_path);
        return FBERROR_UNSPECIFIC;
    }

    char *module_descriptor_symbol = GetProcAddress(lib, "_fbsource_module_descriptor_symbol");
    if (!module_descriptor_symbol) {
        FBLOG_WARN("failed to get module descriptor symbol name from %s", file_path);
        return FBERROR_UNSPECIFIC;
    }

    *module_descriptor = GetProcAddress(lib, module_descriptor_symbol);
    if (!module_descriptor) {
        FBLOG_WARN("failed to get module descriptor %s from %s", module_descriptor_symbol, file_path);
        return FBERROR_UNSPECIFIC;
    }
    
    return FBERROR_NONE;
}
