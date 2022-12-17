#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "framebuffet.h"

// redefine log macros to use the framework-internal log function
// NOTE: using the macros requires that the framework instance is present as framebuffet_t *framebuffet
#undef FBLOG_ERROR
#undef FBLOG_WARN
#undef FBLOG_INFO
#undef FBLOG_DEBUG
#undef FBLOG_TRACE

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_ERROR)
#define FBLOG_ERROR(format, ...) ;framebuffet_log_framework(framebuffet, FBLOG_LEVEL_ERROR, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_ERROR(format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_WARN)
#define FBLOG_WARN(format, ...) ;framebuffet_log_framework(framebuffet, FBLOG_LEVEL_WARN, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_WARN(format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_INFO)
#define FBLOG_INFO(format, ...) ;framebuffet_log_framework(framebuffet, FBLOG_LEVEL_INFO, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_INFO(format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_DEBUG)
#define FBLOG_DEBUG(format, ...) ;framebuffet_log_framework(framebuffet, FBLOG_LEVEL_DEBUG, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_DEBUG(format, ...) ;
#endif

#if (FBLOG_COMPILED_MIN_LOG_LEVEL <= FBLOG_LEVEL_TRACE)
#define FBLOG_TRACE(format, ...) ;framebuffet_log_framework(framebuffet, FBLOG_LEVEL_TRACE, format __VA_OPT__(,) __VA_ARGS__);
#else
#define FBLOG_TRACE(format, ...) ;
#endif

#define FBLOG_DEFAULT_MIN_LOG_LEVEL FBLOG_LEVEL_INFO

static char* copy_string(char *s) {
    if (!s) {
        return NULL;
    }
    
    int length = strlen(s);
    char *copy = malloc(length + 1);
    
    if (copy) {
        memcpy(copy, s, length);
        copy[length] = 0;
    }

    return copy;
}

static void framebuffet_log_internal(framebuffet_t *framebuffet, fblog_level_t level, char *format, va_list args) {
    // we need to have a log receiver and the log level has to be runtime-enabled to continue
    if (!framebuffet->log_receiver || framebuffet->minimum_log_level > level) {
        return;
    }

    char buffer[FBLOG_MESSAGE_BUFFER_SIZE] = { 0 };
    if (!vsnprintf(buffer, FBLOG_MESSAGE_BUFFER_SIZE, format, args)) {
        return;
    }

    framebuffet->log_receiver(level, buffer);
}

static void framebuffet_log_framework(framebuffet_t *framebuffet, fblog_level_t level, char *format, ...) {
    // used to log within the framework itself

    va_list args;
    va_start(args, format);
    framebuffet_log_internal(framebuffet, level, format, args);
    va_end(args);
}

void framebuffet_log(void *instance_ref, fblog_level_t level, char *format, ...) {
    fbsource_module_instance_entry_t *module_instance_entry = instance_ref;
    framebuffet_t *framebuffet = module_instance_entry->framebuffet;

    // avoid unnecessary overhead if the message won't be logged anyway
    if (!framebuffet->log_receiver || framebuffet->minimum_log_level > level) {
        return;
    }

    // TODO: avoid malloc

    int format_length = strlen(format);
    size_t internal_format_size = format_length + module_instance_entry->log_prefix_length + 1;
    char *internal_format = malloc(internal_format_size);
    if (!internal_format) {
        // drop message if "out of memory"
        // FIXME: log
        //framebuffet_log_internal(framebuffet, FBLOG_LEVEL_WARN, "dropped fbsource log message");
        return;
    }

    memcpy(internal_format, module_instance_entry->log_prefix, module_instance_entry->log_prefix_length);
    memcpy(internal_format + module_instance_entry->log_prefix_length, format, format_length);
    internal_format[internal_format_size - 1] = 0;
    
    va_list args;
    va_start(args, format);

    framebuffet_log_internal(framebuffet, level, internal_format, args);
    free(internal_format);

    va_end(args);
}

static int load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path);

#ifdef TARGET_UNIX
#include <dlfcn.h>

static int load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path) {
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

#endif

void framebuffet_provide_log_receiver(framebuffet_t *framebuffet, fblog_receiver_f log_receiver) {
    framebuffet->log_receiver = log_receiver;
}

int framebuffet_set_minimum_log_level(framebuffet_t *framebuffet, fblog_level_t level) {
    int is_valid_level = (level == FBLOG_LEVEL_ERROR) || (level == FBLOG_LEVEL_WARN) || (level == FBLOG_LEVEL_INFO) || (level == FBLOG_LEVEL_DEBUG) || (level == FBLOG_LEVEL_TRACE);
    if (!is_valid_level) {
        return FBERROR_UNSPECIFIC;
    }
    
    framebuffet->minimum_log_level = level;
    
    return FBERROR_NONE;
}

framebuffet_t* framebuffet_init() {
    framebuffet_t *out = malloc(sizeof(framebuffet_t));
    if (!out) {
        return NULL;
    }

    memset(out, 0, sizeof(framebuffet_t));

    if (mtx_init(&out->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        free(out);
        return NULL;
    }

    out->minimum_log_level = FBLOG_DEFAULT_MIN_LOG_LEVEL;
    
    out->modules = fbcollections_map_create();
    out->configurations = fbcollections_map_create();
    out->sources = fbcollections_map_create();

    int success = (out->modules != NULL) && (out->configurations != NULL) && (out->sources != NULL);
    if (success) {
        return out;
    }

    // something went wrong, deallocate memory
    fbcollections_map_free_map_and_payloads(out->modules);
    fbcollections_map_free_map_and_payloads(out->configurations);
    fbcollections_map_free_map_and_payloads(out->sources);
    mtx_destroy(&out->mutex);
    free(out);
  
    return NULL;
}

int framebuffet_destroy(framebuffet_t *framebuffet) {
    // FIXME: implement (stop and destroy all module instances and the framework instance)
    return FBERROR_UNSPECIFIC;
}

static void destroy_frame_update_entry(fbsource_module_instance_frame_update_t *frame_update) {
    // we need to make sure no one is waiting for the lock any more before we can destroy it,
    // so first we prevent new threads from starting to wait...
    frame_update->destruction_pending = 1;

    // ... then we unblock all waiting threads...
    cnd_broadcast(&frame_update->wait);

    // ... and lock the mutex used for the condition ourselves, in the hope that
    // all previously blocked threads will have been granted the lock before us
    while (mtx_lock(&frame_update->mutex) != thrd_success) {
        // TODO: is there any better way of doing this?!
        thrd_yield();
    }
    mtx_unlock(&frame_update->mutex);

    mtx_destroy(&frame_update->mutex);
    cnd_destroy(&frame_update->wait);
}

static int create_frame_update_entry(fbsource_module_instance_frame_update_t *entry) {
    if (mtx_init(&entry->mutex, mtx_plain) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    if (cnd_init(&entry->wait) != thrd_success) {
        mtx_destroy(&entry->mutex);
        return FBERROR_UNSPECIFIC;
    }

    return FBERROR_NONE;
}

static void destroy_frame_update_entries(fbsource_module_instance_frame_update_t *frame_updates, int first_entry, int num_buffers) {
    for (int i=first_entry; i<num_buffers; i++) {
        destroy_frame_update_entry(&(frame_updates[i]));
    }
}

int framebuffet_publish_num_buffers(void *instance_ref, int num_buffers) {
    // FIXME: reconfiguration untested
    
    fbsource_module_instance_entry_t *instance = instance_ref;
    
    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    if (num_buffers == instance->num_buffers) {
        // nothing to do if number of buffers has not changed
        mtx_unlock(&instance->mutex);
        return FBERROR_NONE;
    }

    size_t new_size = sizeof(fbsource_module_instance_frame_update_t[num_buffers]);
    
    if (num_buffers == 0) {
        // all buffers are gone
        // just release the old array, if we had any
        if (instance->frame_updates) {
            destroy_frame_update_entries(instance->frame_updates, 0, instance->num_buffers);
            free(instance->frame_updates);
        }
    } else if (num_buffers < instance->num_buffers) {
        // number of buffers shrinks

        // create the new array
        fbsource_module_instance_frame_update_t *new_entries = malloc(new_size);
        if (!new_entries) {
            mtx_unlock(&instance->mutex);
            return FBERROR_UNSPECIFIC;
        }
        memset(new_entries, 0, new_size);

        if (instance->frame_updates) {
            // destroy all entries that are going to be removed
            destroy_frame_update_entries(instance->frame_updates, num_buffers, instance->num_buffers);
            
            // copy all remaining entries to a new array
            memcpy(new_entries, instance->frame_updates, new_size);
            
            free(instance->frame_updates);
        }

        instance->frame_updates = new_entries;
    } else {
        // number of buffers grows

        // create the new array
        fbsource_module_instance_frame_update_t *new_entries = malloc(new_size);
        if (!new_entries) {
            mtx_unlock(&instance->mutex);
            return FBERROR_UNSPECIFIC;
        }
        memset(new_entries, 0, new_size);

        if (instance->frame_updates) {
            // copy existing entries
            size_t old_size = sizeof(fbsource_module_instance_frame_update_t[instance->num_buffers]);
            memcpy(new_entries, instance->frame_updates, old_size);
        }

        // initialize new entries
        for (int i=instance->num_buffers; i<num_buffers; i++) {
            int res = create_frame_update_entry(&(new_entries[i]));
            if (res != FBERROR_NONE) {
                // destroy already created entries
                destroy_frame_update_entries(new_entries, instance->num_buffers, i);
                free(new_entries);
                mtx_unlock(&instance->mutex);
                return FBERROR_UNSPECIFIC;
            }
        }

        if (instance->frame_updates) {
            free(instance->frame_updates);
        }

        instance->frame_updates = new_entries;
    }

    instance->num_buffers = num_buffers;

    mtx_unlock(&instance->mutex);

    return FBERROR_NONE;
}

int framebuffet_publish_update(void *instance_ref, int buffer_id, fbsource_token_t token) {
    fbsource_module_instance_entry_t *instance = instance_ref;

    if (mtx_lock(&instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    if (buffer_id >= instance->num_buffers || buffer_id < 0) {
        mtx_unlock(&instance->mutex);
        return FBERROR_NO_SUCH_BUFFER;
    }

    fbsource_module_instance_frame_update_t *frame_update = &(instance->frame_updates[buffer_id]);

    if (frame_update->destruction_pending) {
        mtx_unlock(&instance->mutex);
        return FBERROR_NO_SUCH_BUFFER;
    }
    
    if (mtx_lock(&frame_update->mutex) != thrd_success) {
        mtx_unlock(&instance->mutex);
        return FBERROR_UNSPECIFIC;
    }

    if (frame_update->destruction_pending) {
        mtx_unlock(&instance->mutex);
        mtx_unlock(&frame_update->mutex);
        return FBERROR_UNSPECIFIC;
    }

    frame_update->token = token;
    cnd_broadcast(&frame_update->wait);

    mtx_unlock(&frame_update->mutex);
    mtx_unlock(&instance->mutex);

    return FBERROR_NONE;
}

static int apply_module_descriptor(framebuffet_t *framebuffet, fbsource_module_descriptor_t module_descriptor, fbsource_module_registration_t *registration) {
    if (module_descriptor.api_version != FRAMEBUFFET_MODULE_API_VERSION) {
        FBLOG_WARN("module has been compiled with an incompatible API version %d", module_descriptor.api_version);
        return FBERROR_UNSPECIFIC;
    }
  
    char *name = module_descriptor.name;
    char *license = module_descriptor.license;

    if (name == NULL || license == NULL) {
        FBLOG_WARN("module is missing name or license");
        return FBERROR_UNSPECIFIC;
    }

    fbsource_module_t *module = malloc(sizeof(fbsource_module_t));
    if (module == NULL) {
        return FBERROR_MEMORY_ALLOCATION;
    }

    memcpy(module, &module_descriptor.ops, sizeof(fbsource_module_t));

    registration->module = module;
    registration->name = name;
    registration->license = license;

    return FBERROR_NONE;
}

int framebuffet_load_module(framebuffet_t* framebuffet, char *file_path) {
    fbsource_module_descriptor_t *module_descriptor = NULL;

    FBLOG_INFO("loading module %s", file_path);
  
    int err = load_module(framebuffet, &module_descriptor, file_path);
    if (err != FBERROR_NONE) {
        return err;
    }

    return framebuffet_register_module(framebuffet, *module_descriptor);
}

int framebuffet_register_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t module_descriptor) {
    if (mtx_lock(&framebuffet->mutex) != thrd_success) {
        FBLOG_WARN("failed to lock framework mutex for module registration");
        return FBERROR_UNSPECIFIC;
    }
    
    fbsource_module_registration_t *registration = malloc(sizeof(fbsource_module_registration_t));
    if (registration == NULL) {
        mtx_unlock(&framebuffet->mutex);
        return FBERROR_MEMORY_ALLOCATION;
    }

    int err = apply_module_descriptor(framebuffet, module_descriptor, registration);
    if (err != FBERROR_NONE) {
        mtx_unlock(&framebuffet->mutex);
        return err;
    }

    int name_size = strlen(registration->name) + 1;
    fbcollections_map_entry_t *existing = fbcollections_map_find_key(framebuffet->modules, registration->name, name_size);
    if (existing != NULL) {
        mtx_unlock(&framebuffet->mutex);
        return FBERROR_UNSPECIFIC;
    }
  
    fbcollections_map_put(framebuffet->modules, &existing, registration->name, name_size, registration, sizeof(fbsource_module_registration_t));
    
    mtx_unlock(&framebuffet->mutex);

    FBLOG_INFO("registered source module \"%s\" (license: %s)", registration->name, registration->license);

    return FBERROR_NONE;
}

int framebuffet_set_configuration(framebuffet_t *framebuffet, char *config_name, fbcollections_map_entry_t *config) {
    if (!config_name) {
        FBLOG_WARN("framebuffet_set_configuration called with null config name");
        return FBERROR_UNSPECIFIC;
    }
    
    if (!config) {
        FBLOG_WARN("framebuffet_set_configuration called with null config for \"%s\"", config_name);
        return FBERROR_UNSPECIFIC;
    }

    char *key = copy_string(config_name);
    if (!key) {
        FBLOG_WARN("framebuffet_set_configuration failed to copy name");
        return FBERROR_MEMORY_ALLOCATION;
    }
    
    fbcollections_map_entry_t *copied_config = fbcollections_map_copy_with_payloads(config);
    if (!copied_config) {
        FBLOG_WARN("framebuffet_set_configuration failed to copy provided configuration for \"%s\"", config_name);
        return FBERROR_UNSPECIFIC;
    }
    
    if (mtx_lock(&framebuffet->mutex) != thrd_success) {
        FBLOG_WARN("failed to lock framework mutex to set configuration \"%s\"", config_name);
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_entry_t *previous_config = NULL;
    
    FBLOG_DEBUG("framebuffet_set_configuration storing configuration \"%s\", %p", key, copied_config);
    int res = fbcollections_map_put(framebuffet->configurations, &previous_config, key, strlen(key)+1, copied_config, sizeof(fbcollections_map_entry_t));
    
    mtx_unlock(&framebuffet->mutex);
    
    if (res < 0) {
        FBLOG_WARN("failed to store configuration \"%s\" in map (error: %d)", key, res);
        fbcollections_map_free_map_and_payloads(copied_config);
        return FBERROR_UNSPECIFIC;
    }

    if (previous_config) {
        FBLOG_TRACE("framebuffet_set_configuration freeing previous configuration \"%s\", %p", key, previous_config);
        fbcollections_map_free_map_and_payloads(previous_config);
    }
    
    return FBERROR_NONE;
}

int framebuffet_get_configuration(framebuffet_t *framebuffet, char *config_name, fbcollections_map_entry_t **config) {
    if (!config_name) {
        FBLOG_WARN("framebuffet_get_configuration called with null config name");
        return FBERROR_UNSPECIFIC;
    }
    
    if (!config) {
        FBLOG_WARN("framebuffet_get_configuration called with null config (out variable) for \"%s\"", config_name);
        return FBERROR_UNSPECIFIC;
    }

    *config = NULL;
    int err = FBERROR_NONE;

    if (mtx_lock(&framebuffet->mutex) != thrd_success) {
        FBLOG_WARN("failed to lock framework mutex to get configuration \"%s\"", config_name);
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_entry_t *entry = fbcollections_map_find_key(framebuffet->configurations, config_name, strlen(config_name)+1);
    if (entry && entry->value) {
        FBLOG_TRACE("framebuffet_get_configuration copying configuration for \"%s\", %p", config_name, entry->value);
        *config = fbcollections_map_copy_with_payloads(entry->value);
        if (*config == NULL) {
            FBLOG_WARN("failed to copy configuration retrieved for \"%s\", %p", config_name, entry->value);
            err = FBERROR_UNSPECIFIC;
        }
    }

    mtx_unlock(&framebuffet->mutex);

    return err;
}

int framebuffet_create_source_instance(framebuffet_t* framebuffet, fbsource_module_instance_entry_t **source_instance, char *module_name, char *instance_name) {
    int instance_name_length = strlen(instance_name);
    int instance_name_size = instance_name_length + 1;

    FBLOG_INFO("instantiating source module \"%s\" as \"%s\"", module_name, instance_name);
  
    // copy instance_name for use as map key
    char *key = malloc(instance_name_size);
    if (!key) {
        FBLOG_WARN("failed to copy instance name for source instantiation");
        return FBERROR_MEMORY_ALLOCATION;
    }
    memcpy(key, instance_name, instance_name_length);
    key[instance_name_length] = 0;

    if (mtx_lock(&framebuffet->mutex) != thrd_success) {
        FBLOG_WARN("failed to lock framework mutex for source instantiation");
        free(key);
        return FBERROR_UNSPECIFIC;
    }

    fbcollections_map_entry_t *registration_entry = fbcollections_map_find_key(framebuffet->modules, module_name, strlen(module_name) + 1);
    if (registration_entry == NULL) {
        mtx_unlock(&framebuffet->mutex);
        FBLOG_WARN("registration entry not found for module \"%s\"", module_name);
        free(key);
        return FBERROR_UNSPECIFIC;
    }
  
    fbsource_module_registration_t *registration = registration_entry->value;

    // module instances must be unique per name within framebuffet instance
    fbcollections_map_entry_t *existing_source_entry = fbcollections_map_find_key(framebuffet->sources, key, instance_name_size);
    if (existing_source_entry != NULL) {
        mtx_unlock(&framebuffet->mutex);
        FBLOG_WARN("source \"%s\" has already been defined", instance_name);
        free(key);
        return FBERROR_UNSPECIFIC;
    }

    // create basic module instance entry
    fbsource_module_instance_entry_t *instance = malloc(sizeof(fbsource_module_instance_entry_t));
    if (instance == NULL) {
        mtx_unlock(&framebuffet->mutex);
        free(key);
        return FBERROR_MEMORY_ALLOCATION;
    }
    memset(instance, 0, sizeof(fbsource_module_instance_entry_t));

    instance->framebuffet = framebuffet;
    instance->module = registration;

    // copy instance name
    instance->instance_name = malloc(instance_name_size);
    if (!instance->instance_name) {
        mtx_unlock(&framebuffet->mutex);
        free(instance);
        free(key);
        return FBERROR_MEMORY_ALLOCATION;
    }
    
    memcpy(instance->instance_name, instance_name, instance_name_size);
    instance->instance_name[instance_name_size - 1] = 0;

    // assemble log prefix
    int module_name_length = strlen(registration->name);
    instance->log_prefix_length = 4 // "[", ":" and "] "
        + module_name_length
        + instance_name_length;
    instance->log_prefix = malloc(instance->log_prefix_length + 1);
    if (!instance->log_prefix) {
        mtx_unlock(&framebuffet->mutex);
        free(instance->instance_name);
        free(instance);
        free(key);
        return FBERROR_MEMORY_ALLOCATION;
    }

    memcpy(instance->log_prefix, "[", 1);
    memcpy(instance->log_prefix + 1, registration->name, module_name_length);
    memcpy(instance->log_prefix + 1 + module_name_length, ":", 1);
    memcpy(instance->log_prefix + 2 + module_name_length, instance_name, instance_name_length);
    memcpy(instance->log_prefix + 2 + module_name_length + instance_name_length, "] \0", 3);

    // initialize mutex
    if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
        mtx_unlock(&framebuffet->mutex);
        FBLOG_WARN("failed to initialize mutex for source instance \"%s\"", instance_name);
        free(instance->instance_name);
        free(instance->log_prefix);
        free(instance);
        free(key);
        return FBERROR_UNSPECIFIC;
    }

    // instantiate fbsource
    FBLOG_DEBUG("calling constructor for \"%s\", instance name \"%s\"", module_name, instance_name);
    void *data = NULL;
    int err = registration->module->_create(framebuffet, (void*) instance, &data);
    FBLOG_TRACE("constructor call for \"%s\", instance name \"%s\" finished with %d, %p", module_name, instance_name, err, data);
    if (err != FBERROR_NONE) {
        mtx_unlock(&framebuffet->mutex);
        free(instance->instance_name);
        free(instance->log_prefix);
        free(instance);
        free(key);
        return err;
    }
    instance->data = data;

    // register to known sources
    FBLOG_TRACE("recording source \"%s\", size=%ld", instance_name, instance_name_size);
    if (fbcollections_map_put(framebuffet->sources, &existing_source_entry, key, instance_name_size, instance, sizeof(fbsource_module_instance_entry_t)) < 0) {
        mtx_unlock(&framebuffet->mutex);
        FBLOG_WARN("source \"%s\" failed to register", instance_name);
        err = registration->module->_destroy(data);
        data = NULL;
        if (err != FBERROR_NONE) {
            FBLOG_ERROR("source \"%s\" failed deconstruction with %d", err);
            free(key);
            return FBERROR_UNSPECIFIC;
        }
        free(instance->instance_name);
        free(instance->log_prefix);
        free(instance);
        free(key);
        return FBERROR_UNSPECIFIC;
    }

    mtx_unlock(&framebuffet->mutex);
    
    *source_instance = instance;
    FBLOG_INFO("registered source \"%s\"", instance_name);

    return FBERROR_NONE;
}

fbsource_module_instance_entry_t* framebuffet_get_source_instance(framebuffet_t* framebuffet, char *instance_name) {
    size_t instance_name_size = strlen(instance_name) + 1;
    FBLOG_TRACE("framebuffet_get_source_instance instance_name=\"%s\", size=%lu", instance_name, instance_name_size);
    
    if (mtx_lock(&framebuffet->mutex) != thrd_success) {
        FBLOG_WARN("failed to lock framework mutex for source lookup");
        return NULL;
    }
    
    fbcollections_map_entry_t *instance_entry = fbcollections_map_find_key(framebuffet->sources, instance_name, instance_name_size);

    mtx_unlock(&framebuffet->mutex);
    
    FBLOG_TRACE("framebuffet_get_source_instance got entry %p for \"%s\", value %p", instance_entry, instance_name, (instance_entry ? instance_entry->value : NULL));

    if (instance_entry && instance_entry->value) {
        return instance_entry->value;
    }

    return NULL;
}

int framebuffet_configure_source_instance(fbsource_module_instance_entry_t* source_instance, fbcollections_map_entry_t *config) {
    return source_instance->module->module->configure_module(source_instance->data, config);
}

int framebuffet_get_source_configuration(fbsource_module_instance_entry_t* source_instance, fbcollections_map_entry_t **config) {
    return source_instance->module->module->get_module_configuration(source_instance->data, config);
}

int framebuffet_get_num_buffers(fbsource_module_instance_entry_t* source_instance) {
    return source_instance->module->module->get_num_buffers(source_instance->data);
}

fbsource_buffer_config_t* framebuffet_get_buffer_configuration(fbsource_module_instance_entry_t* source_instance, int buffer_id) {
    return source_instance->module->module->get_buffer_configuration(source_instance->data, buffer_id);
}

int framebuffet_copy_latest_frame(fbsource_module_instance_entry_t* source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, fbsource_token_t *token) {
    return source_instance->module->module->copy_latest_frame(source_instance->data, destination, buffer_size, buffer_id, token);
}

int framebuffet_has_update(fbsource_module_instance_entry_t* source_instance, int buffer_id, fbsource_token_t last_token) {
    return source_instance->module->module->has_update(source_instance->data, buffer_id, last_token);
}

static const struct timespec wait_indefinitely = {
    .tv_sec = 0,
    .tv_nsec = 0
};

static int framebuffet_wait_for_update_internal(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token, struct timespec wait_until) {
    if (mtx_lock(&source_instance->mutex) != thrd_success) {
        return FBERROR_UNSPECIFIC;
    }

    if (buffer_id >= source_instance->num_buffers) {
        mtx_unlock(&source_instance->mutex);
        return FBERROR_NO_SUCH_BUFFER;
    }

    fbsource_module_instance_frame_update_t *frame_update = &(source_instance->frame_updates[buffer_id]);

    // early token check, we may not need to get the lock
    if (last_token != frame_update->token) {
        mtx_unlock(&source_instance->mutex);
        return FBERROR_NONE;
    }

    // back off before trying to get a lock if destruction is pending
    if (frame_update->destruction_pending) {
        mtx_unlock(&source_instance->mutex);
        return FBERROR_NO_SUCH_BUFFER;
    }

    // switch to frame update lock, source instance can be unlocked already
    mtx_lock(&frame_update->mutex);
    mtx_unlock(&source_instance->mutex);

    // repeat token check
    if (last_token != frame_update->token) {
        mtx_unlock(&frame_update->mutex);
        return FBERROR_NONE;
    }

    // repeat destruction check
    if (frame_update->destruction_pending) {
        mtx_unlock(&frame_update->mutex);
        return FBERROR_NO_SUCH_BUFFER;
    }
    
    // wait now that we confirmed no update to be available yet and that it's safe access the condition
    int res;
    if (wait_until.tv_sec && wait_until.tv_nsec) {
        // non-zero timestamp means we should do a timed wait
        res = cnd_timedwait(&frame_update->wait, &frame_update->mutex, &wait_until);
    } else {
        // zero timestamp means we should wait indefinitely
        res = cnd_wait(&frame_update->wait, &frame_update->mutex);
    }
    
    if (frame_update->destruction_pending) {
        // buffer has stopped to exist and is pending deregistration on framework
        // (this gets triggered from destroy_frame_update_entry)
        res = FBERROR_NO_SUCH_BUFFER;
    } else if (res == thrd_success) {
        // we were notified about some change, no further check needed
        res = FBERROR_NONE;
    } else if (res == thrd_timedout) {
        // we timed out; without an additional check it is safe to assume there also
        // is no update as none could have been published yet
        res = FBERROR_NO_FRAME;
    } else {
        res = FBERROR_UNSPECIFIC;
    }
    
    mtx_unlock(&frame_update->mutex);
    
    return res;
}

int framebuffet_wait_for_update(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token) {
    return framebuffet_wait_for_update_internal(source_instance, buffer_id, last_token, wait_indefinitely);
}

int framebuffet_wait_for_update_micros(fbsource_module_instance_entry_t *source_instance, int buffer_id, fbsource_token_t last_token, uint32_t microseconds) {
    // FIXME: not fully tested/verified
    
    struct timespec timeout;
    if (!timespec_get(&timeout, TIME_UTC)) {
        return FBERROR_UNSPECIFIC;
    }

    time_t seconds_to_add = microseconds / 1000000;
    long nanoseconds_to_add = (microseconds % 1000000) * 1000;

    // add to timestamp
    timeout.tv_sec += seconds_to_add;
    timeout.tv_nsec += nanoseconds_to_add;

    // overflow to seconds
    timeout.tv_sec += timeout.tv_nsec / 1000000000;
    timeout.tv_nsec = timeout.tv_nsec % 1000000000;

    return framebuffet_wait_for_update_internal(source_instance, buffer_id, last_token, timeout);
}
