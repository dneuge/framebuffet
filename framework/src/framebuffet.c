#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "framebuffet.h"

static int load_module(fbsource_module_registration_t *registration, char *file_path);

#ifdef TARGET_UNIX
#include <dlfcn.h>

static int load_module(fbsource_module_registration_t *registration, char *file_path) {
  //void *lib = dlopen(file_path, RTLD_LOCAL | RTLD_LAZY);
  void *lib = dlopen(file_path, RTLD_GLOBAL | RTLD_NOW);
  char *err_s = dlerror();
  if (lib == NULL || err_s != NULL) {
    printf("loading module from %s failed: %s\n", file_path, err_s); // TODO: use logger
    return FBERROR_UNSPECIFIC;
  }
  
  char *name = dlsym(lib, "_fbsource_module_name");
  err_s = dlerror();
  if (name == NULL || err_s != NULL) {
    printf("failed to get module name from %s: %s\n", file_path, err_s); // TODO: use logger
  }
  
  char *license = dlsym(lib, "_fbsource_module_license");
  err_s = dlerror();
  if (license == NULL || err_s != NULL) {
    printf("failed to get module license from %s: %s\n", file_path, err_s); // TODO: use logger
  }
  
  fbsource_init_f *init = dlsym(lib, "_fbsource_module_init");
  err_s = dlerror();
  if (init == NULL || err_s != NULL) {
    printf("failed to get module initialization from %s: %s\n", file_path, err_s); // TODO: use logger
  }

  if (name == NULL || license == NULL || init == NULL) {
    return FBERROR_UNSPECIFIC;
  }

  fbsource_module_t *module = malloc(sizeof(fbsource_module_t));
  if (module == NULL) {
    return FBERROR_MEMORY_ALLOCATION;
  }

  printf("initializing\n"); // DEBUG
  fbsource_module_t module_tmp = (*init)();
  printf("... done\n"); // DEBUG
  memcpy(module, &module_tmp, sizeof(fbsource_module_t));

  registration->module = module;
  registration->name = name;
  registration->license = license;

  return FBERROR_NONE;
}
#endif

framebuffet_t* framebuffet_init() {
  framebuffet_t *out = malloc(sizeof(framebuffet_t));

  if (out != NULL) {
    memset(out, 0, sizeof(framebuffet_t));
  }

  out->modules = fbcollections_map_create();
  out->config = fbcollections_map_create();
  out->sources = fbcollections_map_create();

  int success = (out->modules != NULL) && (out->config != NULL) && (out->sources != NULL);
  if (success) {
    return out;
  }

  // something went wrong, deallocate memory
  fbcollections_map_free_map_and_payloads(out->modules);
  fbcollections_map_free_map_and_payloads(out->config);
  fbcollections_map_free_map_and_payloads(out->sources);
  free(out);
  
  return NULL;
}

int framebuffet_destroy(framebuffet_t *framebuffet) {
  // FIXME: implement (stop and destroy all module instances and the framework instance)
  return FBERROR_UNSPECIFIC;
}

int framebuffet_load_module(framebuffet_t* framebuffet, char *file_path) {
  fbsource_module_registration_t *registration = malloc(sizeof(fbsource_module_registration_t));
  if (registration == NULL) {
    return FBERROR_MEMORY_ALLOCATION;
  }

  int err = load_module(registration, file_path);
  if (err != FBERROR_NONE) {
    return err;
  }

  int name_size = strlen(registration->name) + 1;
  fbcollections_map_entry_t *existing = fbcollections_map_find_key(framebuffet->modules, registration->name, name_size);
  if (existing != NULL) {
    return FBERROR_UNSPECIFIC;
  }
  
  fbcollections_map_put(framebuffet->modules, &existing, registration->name, name_size, registration, sizeof(fbsource_module_registration_t));

  return FBERROR_NONE;
}

int framebuffet_create_source_instance(framebuffet_t* framebuffet, fbsource_module_instance_entry_t **source_instance, char *module_name, char *instance_name) {
  int instance_name_size = strlen(instance_name) + 1;
  
  fbcollections_map_entry_t *registration_entry = fbcollections_map_find_key(framebuffet->modules, module_name, strlen(module_name) + 1);
  if (registration_entry == NULL) {
    printf("registration entry not found for module \"%s\"\n", module_name); // TODO: log
    return FBERROR_UNSPECIFIC;
  }
  
  fbsource_module_registration_t *registration = registration_entry->value;

  fbcollections_map_entry_t *existing_source_entry = fbcollections_map_find_key(framebuffet->sources, instance_name, instance_name_size);
  if (existing_source_entry != NULL) {
    printf("source \"%s\" has already been defined\n", instance_name); // TODO: log
    return FBERROR_UNSPECIFIC;
  }

  fbsource_module_instance_entry_t *instance = malloc(sizeof(fbsource_module_instance_entry_t));
  if (instance == NULL) {
    return FBERROR_MEMORY_ALLOCATION;
  }
  memset(instance, 0, sizeof(fbsource_module_instance_entry_t));
  
  void *data = NULL;
  int err = registration->module->_create(framebuffet, &data);
  if (err != FBERROR_NONE) {
    return err;
  }

  instance->module = registration;
  instance->data = data;

  instance->instance_name = malloc(instance_name_size);
  memcpy(instance->instance_name, instance_name, instance_name_size);
  instance->instance_name[instance_name_size - 1] = 0;

  fbcollections_map_put(framebuffet->sources, &existing_source_entry, instance_name, instance_name_size, instance, sizeof(fbsource_module_instance_entry_t));
  *source_instance = instance;

  return FBERROR_NONE;
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

int framebuffet_copy_latest_frame(fbsource_module_instance_entry_t* source_instance, unsigned char *destination, unsigned long buffer_size, int buffer_id, unsigned int *token) {
  return source_instance->module->module->copy_latest_frame(source_instance->data, destination, buffer_size, buffer_id, token);
}

int framebuffet_has_update(fbsource_module_instance_entry_t* source_instance, int buffer_id, unsigned int last_token) {
  return source_instance->module->module->has_update(source_instance->data, buffer_id, last_token);
}
