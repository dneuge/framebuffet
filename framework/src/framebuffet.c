#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_C11_THREADS
#include <time.h>
#else
#include <c11/time.h>
#endif

// FIXME: get emacs/FlyCheck using the include dir and reference properly
#include "../../lib/_build/cJSON.h"

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

#define FILE_READ_CHUNK_SIZE (512 * 1024) /* read up to 512kB at a time */

#define FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE 7

typedef struct {
    int major; // to be incremented on breaking changes
    int minor; // to be incremented as functionality gets extended which does not break (structural) compatibility
} framebuffet_config_version_t;

// older versions may continue to be supported by adapting contents on load
static const framebuffet_config_version_t supported_min_framebuffet_config_version = {
    .major = 1,
    .minor = 0
};

// forward compatibility:
// - higher major revision should not be attempted to be loaded
// - higher minor revision may still be supported and should be loaded
//   - warning should be logged to inform about possible reason for incompatibility in advance
//   - unsupported features might be in use at which point loading should fail
static const framebuffet_config_version_t supported_max_framebuffet_config_version = {
    .major = 1,
    .minor = 0
};

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

static char* copy_string_format_safe(char *s) {
    char *out = copy_string(s);
    if (out) {
        // % is interpreted when used in format strings; change to something less likely to cause issues
        for (int i=strlen(out); i>=0; i--) {
            if (out[i] == '%') {
                out[i] = '_';
            }
        }
    }

    return out;
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

void framebuffet_log_va(void *instance_ref, fblog_level_t level, char *format, va_list args) {
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
    
    framebuffet_log_internal(framebuffet, level, internal_format, args);
    free(internal_format);
}

void framebuffet_log(void *instance_ref, fblog_level_t level, char *format, ...) {
    va_list args;
    va_start(args, format);
    framebuffet_log_va(instance_ref, level, format, args);
    va_end(args);
}

static char* read_file_as_string(framebuffet_t* framebuffet, char *file_path) {
    char *buffer = NULL;
    long size = 0;

    FBLOG_DEBUG("read_file_as_string \"%s\"", file_path);
    
    FILE *fh = fopen(file_path, "rb");
    if (!fh) {
        FBLOG_WARN("failed to open file for reading: \"%s\"", file_path);
        return NULL;
    }

    if (fseek(fh, 0, SEEK_END)) {
        FBLOG_WARN("failed to seek in file \"%s\", unable to determine file size", file_path);
        goto stop;
    }

    size = ftell(fh);
    if (size == -1) {
        FBLOG_WARN("unable to tell size of file \"%s\"", file_path);
        goto stop;
    }

    FBLOG_TRACE("file size of %s is %ld bytes", file_path, size);

    buffer = malloc(size + 1); // we need one more character for null-termination
    memset(buffer, 0, size);
    if (!buffer) {
        FBLOG_WARN("failed to allocate %ld bytes to load \"%s\"", size, file_path);
        goto stop;
    }

    rewind(fh);
    if (ftell(fh) != 0) {
        FBLOG_WARN("failed to rewind \"%s\"", file_path);
        free(buffer);
        buffer = NULL;
        goto stop;
    }

    size_t offset = 0;
    size_t remaining = size;
    while (remaining > 0) {
        int chunkSize = (remaining < FILE_READ_CHUNK_SIZE) ? remaining : FILE_READ_CHUNK_SIZE;
        int res = fread(buffer+offset, 1, chunkSize, fh);
        offset += res;
        remaining -= res;
        
        if (feof(fh)) {
            break;
        } else if (ferror(fh)) {
            FBLOG_WARN("error reading \"%s\"", file_path);
            remaining = -1;
            break;
        }
    }

    if (remaining != 0) {
        FBLOG_WARN("file could not be read completely (remaining: %ld), aborting: \"%s\"", remaining, file_path);
        free(buffer);
        buffer = NULL;
    }

 stop:
    if (fclose(fh)) {
        FBLOG_WARN("failed to close file \"%s\"", file_path);
    }

    if (buffer) {
        // this may be paranoid but we want to make *really* sure that what we just read is still null-terminated
        buffer[size] = 0;
    }
    
    return buffer;
}

static int load_module(framebuffet_t *framebuffet, fbsource_module_descriptor_t **module_descriptor, char *file_path);

#include "platform/fbplatform.c"

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
    FBLOG_INFO("instantiating source module \"%s\" as \"%s\"", module_name, instance_name);
  
    // copy instance_name for use as map key
    char *key = copy_string(instance_name);
    if (!key) {
        FBLOG_WARN("failed to copy instance name for source instantiation");
        return FBERROR_MEMORY_ALLOCATION;
    }

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
    fbcollections_map_entry_t *existing_source_entry = fbcollections_map_find_key(framebuffet->sources, key, strlen(key)+1);
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

    // copy instance and module names, sanitized for use as format on *printf functions
    char *safe_instance_name = copy_string_format_safe(instance_name);
    char *safe_module_name = copy_string_format_safe(module_name);
    if (!safe_instance_name || !safe_module_name) {
        mtx_unlock(&framebuffet->mutex);
        
        free(instance);
        free(key);

        if (safe_instance_name) {
            free(safe_instance_name);
        }

        if (safe_module_name) {
            free(safe_module_name);
        }
        
        return FBERROR_MEMORY_ALLOCATION;
    }
    
    // assemble log prefix
    int instance_name_length = strlen(safe_instance_name);
    int module_name_length = strlen(safe_module_name);
    instance->log_prefix_length = 4 // "[", ":" and "] "
        + module_name_length
        + instance_name_length;
    int log_prefix_size = instance->log_prefix_length + 1;
    instance->log_prefix = malloc(log_prefix_size);
    if (!instance->log_prefix) {
        mtx_unlock(&framebuffet->mutex);
        free(instance->instance_name);
        free(instance);
        free(key);
        free(safe_instance_name);
        free(safe_module_name);
        return FBERROR_MEMORY_ALLOCATION;
    }

    if (snprintf(instance->log_prefix, log_prefix_size, "[%s:%s] ", safe_module_name, safe_instance_name) != instance->log_prefix_length) {
        mtx_unlock(&framebuffet->mutex);
        FBLOG_WARN("failed to construct log prefix for source \"%s\"", instance_name);
        free(instance->instance_name);
        free(instance);
        free(key);
        free(safe_instance_name);
        free(safe_module_name);
        return FBERROR_UNSPECIFIC;
    }
    instance->log_prefix[log_prefix_size-1] = 0;

    free(safe_instance_name);
    safe_instance_name = NULL;
    
    free(safe_module_name);
    safe_module_name = NULL;
    
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
    FBLOG_TRACE("recording source \"%s\", size=%ld", key, strlen(key)+1);
    if (fbcollections_map_put(framebuffet->sources, &existing_source_entry, key, strlen(key)+1, instance, sizeof(fbsource_module_instance_entry_t)) < 0) {
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

static int parse_framebuffet_config_version(framebuffet_t* framebuffet, framebuffet_config_version_t *version, char *s) {
    char verification_buffer[FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE] = { 0 };
    
    if (!s) {
        FBLOG_DEBUG("parse_framebuffet_config_version called with null string");
        return FBERROR_UNSPECIFIC;
    }

    if (strlen(s) > FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE) {
        FBLOG_WARN("unable to parse excessively large version number \"%s\"; maximum supported string length is %d", s, FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE);
        return FBERROR_UNSPECIFIC;
    }
    
    char *copy = copy_string(s);
    if (!copy) {
        FBLOG_WARN("parse_framebuffet_config_version failed to copy string");
        return FBERROR_MEMORY_ALLOCATION;
    }
    
    int err = FBERROR_UNSPECIFIC;

    FBLOG_TRACE("parse_framebuffet_config_version \"%s\"", s);
    
    char *first_dot = strchr(copy, '.');
    if (!first_dot) {
        FBLOG_DEBUG("failed to extract version from \"%s\": no dot", s);
        goto end;
    }
    
    if (strlen(first_dot) < 2) {
        FBLOG_DEBUG("failed to extract version from \"%s\": minor revision is empty", s);
        goto end;
    }

    if (strchr(first_dot+1, '.')) {
        FBLOG_DEBUG("failed to extract version from \"%s\": more than one dot", s);
        goto end;
    }

    version->minor = atoi(first_dot + 1);

    // make minor revision unreachable, leaving only the major revision accessible in "copy"
    *first_dot = '\0';
    
    version->major = atoi(copy);

    FBLOG_DEBUG("parsed config version \"%s\": major %d, minor %d", s, version->major, version->minor);

    if ((version->major < 0) || (version->minor < 0)) {
        FBLOG_DEBUG("failed to extract version from \"%s\": parsed to negative numbers", s);
        goto end;
    }

    if (snprintf(verification_buffer, FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE, "%d.%d", version->major, version->minor) <= 0) {
        FBLOG_DEBUG("failed to print config version to verification buffer");
        goto end;
    }

    verification_buffer[FBCONFIG_VERSION_VERIFICATION_BUFFER_SIZE - 1] = 0;
    
    if ((strlen(s) != strlen(verification_buffer)) || strncmp(verification_buffer, s, strlen(s))) {
        FBLOG_DEBUG("parsed version does not match original string: \"%s\" != \"%s\"", verification_buffer, s);
        goto end;
    }

    err = FBERROR_NONE;

 end:
    free(copy);
    
    return err;
}

static int check_actions_json_version(framebuffet_t *framebuffet, cJSON *json) {
    framebuffet_config_version_t config_version;
    
    cJSON *json_config_version = cJSON_GetObjectItemCaseSensitive(json, "_framebuffet_config_version");
    if (!json_config_version || !cJSON_IsString(json_config_version)) {
        FBLOG_WARN("provided JSON content does not appear to be a Frame Buffet actions configuration, unable to apply");
        return FBERROR_UNSPECIFIC;
    }

    int err = parse_framebuffet_config_version(framebuffet, &config_version, json_config_version->valuestring);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("failed to parse actions configuration format version \"%s\": %d", json_config_version->valuestring, err);
        return err;
    }

    int is_min_major_revision = (config_version.major == supported_min_framebuffet_config_version.major);
    int is_older_min_major_revision = (config_version.major < supported_min_framebuffet_config_version.major);
    int is_older_min_minor_revision = is_min_major_revision && (config_version.minor < supported_min_framebuffet_config_version.minor);
    int is_older_min_revision = is_older_min_major_revision || is_older_min_minor_revision;

    int is_later_max_major_revision = config_version.major > supported_max_framebuffet_config_version.major;
    
    int is_max_major_revision = config_version.major == supported_max_framebuffet_config_version.major;
    int is_later_max_minor_revision = is_max_major_revision && (config_version.minor > supported_max_framebuffet_config_version.minor);
    
    if (is_older_min_revision) {
        FBLOG_WARN("actions JSON version %d.%d is older than the minimum supported version %d.%d; this version of Frame Buffet is too recent to load the configuration", config_version.major, config_version.minor, supported_min_framebuffet_config_version.major, supported_min_framebuffet_config_version.minor);
        return FBERROR_UNSPECIFIC;
    }

    if (is_later_max_major_revision) {
        FBLOG_WARN("actions JSON version %d.%d is newer than the maximum supported version %d.%d; this version of Frame Buffet is too old to load the configuration", config_version.major, config_version.minor, supported_max_framebuffet_config_version.major, supported_max_framebuffet_config_version.minor);
        return FBERROR_UNSPECIFIC;
    }

    if (is_later_max_minor_revision) {
        FBLOG_WARN("actions JSON version %d.%d is newer than the maximum supported version %d.%d; Frame Buffet will still try to load the configuration but application may fail, this version of Frame Buffet may be too old", config_version.major, config_version.minor, supported_max_framebuffet_config_version.major, supported_max_framebuffet_config_version.minor);
    }

    FBLOG_DEBUG("framebuffet_configure_from_actions_json: actions config version %d.%d is assumed to be compatible (supported: min %d.%d, max %d.%d)", config_version.major, config_version.minor, supported_min_framebuffet_config_version.major, supported_min_framebuffet_config_version.minor, supported_max_framebuffet_config_version.major, supported_max_framebuffet_config_version.minor);

    return FBERROR_NONE;
}

static int build_configuration_from_json_action(framebuffet_t *framebuffet, fbcollections_map_entry_t **config, cJSON *json_action) {
    // shared by set_configuration and create_source_instance

    int err = FBERROR_NONE;

    *config = NULL;

    cJSON *json_base_config = cJSON_GetObjectItemCaseSensitive(json_action, "base_config");
    if (!json_base_config) {
        FBLOG_DEBUG("no base_config, creating empty map");
        *config = fbcollections_map_create();
        if (!*config) {
            FBLOG_WARN("failed to create a new configuration map");
            err = FBERROR_MEMORY_ALLOCATION;
            goto end;
        }
    } else {
        if (!cJSON_IsString(json_base_config) || (strlen(json_base_config->valuestring) < 1)) {
            FBLOG_WARN("base_config is set but not a non-empty string");
            err = FBERROR_UNSPECIFIC;
            goto end;
        }

        FBLOG_DEBUG("retrieving base_config \"%s\"", json_base_config->valuestring);
        err = framebuffet_get_configuration(framebuffet, json_base_config->valuestring, config); // config will be a copy on success
        if (err != FBERROR_NONE) {
            FBLOG_WARN("failed to retrieve base configuration \"%s\"", json_base_config->valuestring);
            goto end;
        }
    }

    cJSON *json_config = cJSON_GetObjectItemCaseSensitive(json_action, "config");
    if (!json_config) {
        // not providing any config entries is only allowed if we inherited from a base_config instead
        FBLOG_DEBUG("no config entries provided");
        if (!json_base_config) {
            FBLOG_WARN("neither \"base_config\" nor \"config\" have been set; at least one of these properties is required");
            err = FBERROR_UNSPECIFIC;
        }
        goto end;
    }

    if (!cJSON_IsObject(json_config) || (cJSON_GetArraySize(json_config) < 1)) {
        FBLOG_WARN("\"config\" must be a non-empty object, if set");
        err = FBERROR_UNSPECIFIC;
        goto end;
    }
    
    cJSON *json_config_entry = NULL;
    cJSON_ArrayForEach(json_config_entry, json_config) {
        if (!json_config_entry->string) {
            FBLOG_WARN("unexpected cJSON structure: \"config\" entry has no \"string\" (object key)");
            err = FBERROR_UNSPECIFIC;
            goto end;
        }

        char *value = NULL;

        // cJSON parses all number types as double, resulting in possible loss of precision, so we only support strings
        if (!cJSON_IsString(json_config_entry)) {
            FBLOG_WARN("value of entry \"%s\" is not a string", json_config_entry->string);
            err = FBERROR_UNSPECIFIC;
            goto end;
        }
        
        value = copy_string(json_config_entry->valuestring);
        if (!value) {
            FBLOG_WARN("failed to copy value from string entry \"%s\"", json_config_entry->string);
            err = FBERROR_MEMORY_ALLOCATION;
            goto end;
        }

        char *key = copy_string(json_config_entry->string);
        if (!key) {
            FBLOG_WARN("failed to copy key \"%s\"", json_config_entry->string);
            free(value);
            err = FBERROR_MEMORY_ALLOCATION;
            goto end;
        }

        FBLOG_TRACE("set \"%s\" = \"%s\"", key, value);
        if (fbcollections_map_put_and_free_old_entry_with_payloads(*config, key, strlen(key) + 1, value, strlen(value) + 1) < 0) {
            FBLOG_WARN("failed to store key \"%s\", value \"%s\" in configuration map", key, value);
            free(key);
            free(value);
        }
    }

    FBLOG_TRACE("build_configuration_from_json_action success");
    
 end:
    if (*config && (err != FBERROR_NONE)) {
        FBLOG_DEBUG("build_configuration_from_json_action freeing map after error %d", err);
        fbcollections_map_free_map_and_payloads(*config);
        *config = NULL;
    }

    return err;
}

static int perform_json_action_set_configuration(framebuffet_t *framebuffet, cJSON *json_action) {
    int err = FBERROR_UNSPECIFIC;
    fbcollections_map_entry_t *config = NULL;
    
    cJSON *json_config_name = cJSON_GetObjectItemCaseSensitive(json_action, "name");
    if (!json_config_name || !cJSON_IsString(json_config_name) || (strlen(json_config_name->valuestring) < 1)) {
        FBLOG_WARN("set_configuration needs a non-empty string as \"name\"");
        return FBERROR_UNSPECIFIC;
    }

    FBLOG_DEBUG("perform_json_action_set_configuration name=\"%s\"", json_config_name->valuestring);
    
    err = build_configuration_from_json_action(framebuffet, &config, json_action);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("set_configuration failed to build a configuration from given input (error: %d)", err);
        return err;
    }

    err = framebuffet_set_configuration(framebuffet, json_config_name->valuestring, config);
    if (err == FBERROR_NONE) {
        FBLOG_TRACE("performjson_action_set_configuration success");
    } else {
        FBLOG_WARN("set_configuration failed to store \"%s\" (error: %d)", json_config_name->valuestring, err);
    }

    if (config) {
        FBLOG_DEBUG("configure_from_json_action_create_configuration freeing map");
        fbcollections_map_free_map_and_payloads(config);
    }
    
    return err;
}

static int perform_json_action_create_source_instance(framebuffet_t *framebuffet, cJSON *json_action) {
    int err = FBERROR_UNSPECIFIC;
    fbcollections_map_entry_t *config = NULL;
    
    cJSON *json_source_name = cJSON_GetObjectItemCaseSensitive(json_action, "name");
    if (!json_source_name || !cJSON_IsString(json_source_name) || (strlen(json_source_name->valuestring) < 1)) {
        FBLOG_WARN("create_source_instance needs a non-empty string as \"name\"");
        return FBERROR_UNSPECIFIC;
    }

    cJSON *json_module_name = cJSON_GetObjectItemCaseSensitive(json_action, "module");
    if (!json_module_name || !cJSON_IsString(json_module_name) || (strlen(json_module_name->valuestring) < 1)) {
        FBLOG_WARN("create_source_instance needs a non-empty string as \"module\"");
        return FBERROR_UNSPECIFIC;
    }

    FBLOG_DEBUG("perform_json_action_create_source_instance name=\"%s\", module=\"%s\"", json_source_name->valuestring, json_module_name->valuestring);

    // TODO: configuration during create_source_instance should be optional; don't fail if not requested
    err = build_configuration_from_json_action(framebuffet, &config, json_action);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("create_source_instance failed to build a configuration from given input (error: %d)", err);
        return err;
    }

    fbsource_module_instance_entry_t *source_instance = NULL;
    err = framebuffet_create_source_instance(framebuffet, &source_instance, json_module_name->valuestring, json_source_name->valuestring);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("create_source_instance failed to create \"%s\" from module \"%s\" (error: %d)", json_source_name->valuestring, json_module_name->valuestring, err);
        goto end;
    }

    FBLOG_TRACE("perform_json_action_create_source_instance created \"%s\", configuring...", json_source_name->valuestring);
    err = framebuffet_configure_source_instance(source_instance, config);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("create_source_instance failed to configure \"%s\" (error: %d)", json_source_name->valuestring, err);
        goto end;
    }    

    FBLOG_TRACE("perform_json_action_create_source_instance configured \"%s\"", json_source_name->valuestring);
    
 end:
    if (config) {
        FBLOG_DEBUG("configure_from_json_action_create_configuration freeing map");
        fbcollections_map_free_map_and_payloads(config);
    }
    
    return err;
}

static int perform_json_action(framebuffet_t *framebuffet, char *action_name, cJSON *json_action) {
    if (!strcmp(action_name, "set_configuration")) {
        return perform_json_action_set_configuration(framebuffet, json_action);
    }
    
    if (!strcmp(action_name, "create_source_instance")) {
        return perform_json_action_create_source_instance(framebuffet, json_action);
    }

    FBLOG_WARN("unsupported action \"%s\"", action_name);
    
    return FBERROR_UNSPECIFIC;
}

int framebuffet_configure_from_actions_json(framebuffet_t *framebuffet, char *json_string) {
    int err = FBERROR_UNSPECIFIC;
    
    cJSON *json = cJSON_Parse(json_string);
    if (!json) {
        FBLOG_WARN("failed to parse actions JSON");
        return FBERROR_UNSPECIFIC;
    }

    if (!cJSON_IsObject(json)) {
        FBLOG_WARN("provided JSON content did not parse to an object");
        goto end;
    }

    err = check_actions_json_version(framebuffet, json);
    if (err != FBERROR_NONE) {
        // details have already been logged
        goto end;
    }

    cJSON *json_actions = cJSON_GetObjectItemCaseSensitive(json, "actions");
    if (!json_actions) {
        FBLOG_WARN("provided JSON content does not define any actions");
        goto end;
    }

    if (!cJSON_IsArray(json_actions)) {
        FBLOG_WARN("\"actions\" needs to be an array of objects");
        goto end;
    }
    
    cJSON *json_action = NULL;
    int action_index = -1; // used for log messages
    cJSON_ArrayForEach(json_action, json_actions) {
        action_index++;

        if (!cJSON_IsObject(json_action)) {
            FBLOG_WARN("actions[%d] is not an object", action_index);
            goto end;
        }

        cJSON *json_action_action = cJSON_GetObjectItemCaseSensitive(json_action, "action");
        if (!json_action_action || !cJSON_IsString(json_action_action) || (strlen(json_action_action->valuestring) <= 0)) {
            FBLOG_WARN("actions[%d][\"action\"] has to be a non-empty string", action_index);
            goto end;
        }

        FBLOG_DEBUG("processing \"%s\" from actions[%d]", json_action_action->valuestring, action_index);
        err = perform_json_action(framebuffet, json_action_action->valuestring, json_action);
        if (err != FBERROR_NONE) {
            FBLOG_WARN("actions[%d] failed processing (error %d)", action_index, err);
            goto end;
        }
    }

    if (action_index < 0) {
        FBLOG_WARN("no actions have been processed, at least one action is expected per JSON actions file");
        err = FBERROR_UNSPECIFIC;
    }
    
 end:
    cJSON_Delete(json);
    
    return err;
}

int framebuffet_configure_from_actions_json_file(framebuffet_t* framebuffet, char *file_path) {
    char *json = read_file_as_string(framebuffet, file_path);

    if (!json) {
        FBLOG_WARN("failed to read actions JSON file \"%s\"", file_path);
        return FBERROR_UNSPECIFIC;
    }

    int err = framebuffet_configure_from_actions_json(framebuffet, json);
    if (err != FBERROR_NONE) {
        FBLOG_WARN("failed to process actions JSON file \"%s\"", file_path);
    }

    free(json);

    return err;
}
