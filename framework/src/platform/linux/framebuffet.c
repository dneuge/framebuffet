#include <dlfcn.h>

// FIXME: make this a proper module (requires framework to be split up)

static fberror_t load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path) {
    *module_descriptor = NULL;

    //void *lib = dlopen(file_path, RTLD_LOCAL | RTLD_LAZY);
    void *lib = dlopen(file_path, RTLD_GLOBAL | RTLD_NOW);
    char *err_s = dlerror();
    if (lib == NULL || err_s != NULL) {
        FBLOG_WARN("loading module from %s failed: %s", file_path, err_s);
        return FBERROR_UNSPECIFIC;
    }

    char *module_descriptor_symbol = dlsym(lib, "_fbsource_module_descriptor_symbol");
    err_s = dlerror();
    if (module_descriptor_symbol == NULL || err_s != NULL) {
        FBLOG_WARN("failed to get module descriptor symbol name from %s: %s", file_path, err_s);
        return FBERROR_UNSPECIFIC;
    }

    *module_descriptor = dlsym(lib, module_descriptor_symbol);
    err_s = dlerror();
    if (*module_descriptor == NULL || err_s != NULL) {
        FBLOG_WARN("failed to get module descriptor %s from %s: %s", module_descriptor_symbol, file_path, err_s);
        *module_descriptor = NULL;
        return FBERROR_UNSPECIFIC;
    }

    return FBERROR_NONE;
}
