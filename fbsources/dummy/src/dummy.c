#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>

#include <framebuffet.h>

typedef short bool;
#define FALSE 0
#define TRUE 1

typedef struct {
  mtx_t mutex;
  
  bool stop_generator;
  thrd_t generator_thread;
  bool has_generator_thread;

  unsigned int frame_token;
  fbcollections_map_entry_t* source_config;

  unsigned int width;
  unsigned int height;
  unsigned long buffer_size;

  unsigned char *buffers[2];
  int complete_buffer;
} fbsource_dummy_t;

static int generate(void *data) {
  fbsource_dummy_t *instance = data;
  
  int gen = 0;
  int last_shift = 0;
  while (!instance->stop_generator) {
    // get the buffer to work on
    int work_buffer = (instance->complete_buffer + 1) % 2;
    unsigned char *buffer = instance->buffers[work_buffer];
    if (!buffer) {
      break;
    }

    // get configuration for this frame
    if (mtx_lock(&instance->mutex) != thrd_success) {
      continue;
    }
    
    unsigned int width = instance->width;
    unsigned int height = instance->height;
    
    mtx_unlock(&instance->mutex); // TODO: how to handle an error?

    // draw some image, shift to make it somewhat animated
    int milliseconds = (int) ((double) clock() / CLOCKS_PER_SEC * 1000.0);
    int shift = (milliseconds / 20) % 256;

    if (shift < last_shift) {
      gen = (gen + 1) % 4;
    }
    last_shift = shift;
    
    int offset = 0;
    for (unsigned int y=0; y<height; y++) {
      for (unsigned int x=0; x<width; x++) {
        unsigned char a = (x + shift) % 256;
        unsigned char b = (y + (shift >> 1)) % 256;
        unsigned char c = (shift) % 256;

        if (gen == 0) {
          buffer[offset++] = a;
          buffer[offset++] = b;
          buffer[offset++] = c;
        } else if (gen == 1) {
          buffer[offset++] = b;
          buffer[offset++] = c;
          buffer[offset++] = a;
        } else if (gen == 2) {
          buffer[offset++] = c;
          buffer[offset++] = a;
          buffer[offset++] = b;
        } else if (gen == 3) {
          buffer[offset++] = c;
          buffer[offset++] = b;
          buffer[offset++] = a;
        } else {
          buffer[offset++] = a;
          buffer[offset++] = c;
          buffer[offset++] = b;
        }
      }
    }

    // mark buffer as complete
    if (mtx_lock(&instance->mutex) != thrd_success) {
      continue;
    }
    
    instance->complete_buffer = work_buffer;
    instance->frame_token++;
    
    mtx_unlock(&instance->mutex); // TODO: how to handle an error?
    
    // aim for ~30FPS
    thrd_sleep(&(struct timespec){.tv_nsec=30000000}, NULL); 
  }
}

static int dummy_create(framebuffet_t *framebuffet, void **data) {
  fbsource_dummy_t *instance = malloc(sizeof(fbsource_dummy_t));
  if (instance == NULL) {
    return FBERROR_MEMORY_ALLOCATION;
  }
  
  memset(instance, 0, sizeof(fbsource_dummy_t));

  if (mtx_init(&instance->mutex, mtx_plain) != thrd_success) {
    free(instance);
    return FBERROR_UNSPECIFIC;
  }

  *data = instance;
    
  return FBERROR_NONE;
}

static int stop_generator_thread(fbsource_dummy_t *instance) {
  int out_error = FBERROR_NONE;
  
  if (!instance->has_generator_thread) {
    return FBERROR_NONE;
  }
  
  instance->stop_generator = TRUE;
  int thread_res;
  if (thrd_join(instance->generator_thread, &thread_res) != thrd_success) {
    // TODO: what else can be done if joining the thread fails?
    out_error = FBERROR_UNSPECIFIC;
  }
  instance->has_generator_thread = FALSE;

  return out_error;
}

static int dummy_destroy(void *data) {
  fbsource_dummy_t *instance = data;
  
  if (instance == NULL) {
    return FBERROR_NONE;
  }

  int out_error = stop_generator_thread(instance);

  fbcollections_map_free_map_and_payloads(instance->source_config);

  if (instance->buffers[0]) {
    free(instance->buffers[0]);
  }
  
  if (instance->buffers[1]) {
    free(instance->buffers[1]);
  }

  // FIXME: make sure nothing is waiting for the mutex before it gets destroyed
  
  mtx_destroy(&instance->mutex);
  free(instance);
  
  return out_error;
}

static int get_config_int(fbsource_dummy_t *instance, char *key, int default_value) {
  int key_size = strlen(key) + 1;
  
  fbcollections_map_entry_t *entry = fbcollections_map_find_key(instance->source_config, key, key_size);
  if (entry == NULL) {
    return default_value;
  }

  return atoi(entry->value);
}

static int dummy_configure_module(void *data, fbcollections_map_entry_t *config) {
  fbsource_dummy_t *instance = data;

  stop_generator_thread(instance);
  
  if (mtx_lock(&instance->mutex) != thrd_success) {
    return FBERROR_UNSPECIFIC;
  }

  fbcollections_map_free_map_and_payloads(instance->source_config);
  instance->source_config = fbcollections_map_copy_with_payloads(config);
  if (instance->source_config == NULL) {
    mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error
    return FBERROR_UNSPECIFIC;
  }
  
  // release old buffers
  if (instance->buffer_size > 0) {
    free(instance->buffers[0]);
    free(instance->buffers[1]);
  }
  
  instance->buffers[0] = NULL;
  instance->buffers[1] = NULL;
  instance->complete_buffer = 0;

  // apply configuration
  instance->width = get_config_int(instance, "width", 0);
  instance->height = get_config_int(instance, "height", 0);
  instance->buffer_size = instance->width * instance->height * 3;

  // allocate buffers if parameters were acceptable
  if (instance->width > 0 && instance->height > 0 && instance->buffer_size > 0) {
    instance->buffers[0] = (unsigned char*) malloc(instance->buffer_size);
    instance->buffers[1] = (unsigned char*) malloc(instance->buffer_size);
  }

  // reset on error (buffers remain unallocated if input is wrong)
  if (!instance->buffers[0] || !instance->buffers[1]) {
    if (instance->buffers[0]) {
      free(instance->buffers[0]);
      instance->buffers[0] = NULL;
    }
    
    if (instance->buffers[1]) {
      free(instance->buffers[1]);
      instance->buffers[1] = NULL;
    }

    instance->width = 0;
    instance->height = 0;
    instance->buffer_size = 0;
  }

  bool success = (instance->buffer_size > 0);

  mtx_unlock(&instance->mutex); // TODO: handle mtx_unlock error

  if (success) {
    instance->stop_generator = FALSE;

    if (thrd_create(&instance->generator_thread, generate, instance) == thrd_success) {
      instance->has_generator_thread = TRUE;
    } else {
      return FBERROR_UNSPECIFIC;
    }
  }
  
  return success ? FBERROR_NONE : FBERROR_UNSPECIFIC;
}

static int dummy_get_num_buffers(void *data) {
  // this module only generates a single buffer
  return 1;
}

static int dummy_get_module_configuration(void *data, fbcollections_map_entry_t **config) {
  fbsource_dummy_t *instance = data;
  *config = fbcollections_map_copy_with_payloads(instance->source_config);
  return FBERROR_NONE;
}

static fbsource_buffer_config_t* dummy_get_buffer_configuration(void *data, int buffer_id) {
  fbsource_dummy_t *instance = data;
  
  if (buffer_id != 0) {
    // this module only generates a single buffer
    return NULL;
  }

  fbsource_buffer_config_t *out = malloc(sizeof(fbsource_buffer_config_t));
  if (out == NULL) {
    return NULL;
  }

  memset(out, 0, sizeof(fbsource_buffer_config_t));

  if (mtx_lock(&instance->mutex) != thrd_success) {
    free(out);
    return NULL;
  }
  
  out->width = instance->width;
  out->height = instance->height;
  out->size = instance->buffer_size;

  mtx_unlock(&instance->mutex); // TODO: handle error?
  
  return out;
}

static int dummy_copy_latest_frame(void *data, unsigned char *destination, unsigned long buffer_size, int buffer_id, unsigned int *token) {
  fbsource_dummy_t *instance = data;
  int out_error = FBERROR_NONE;
  
  if (buffer_id != 0) {
    return FBERROR_NO_SUCH_BUFFER;
  }

  if (mtx_lock(&instance->mutex) != thrd_success) {
    return FBERROR_UNSPECIFIC;
  }
  
  if (instance->buffer_size <= 0) {
    out_error = FBERROR_UNSPECIFIC;
  } else if (buffer_size < instance->buffer_size) {
    out_error = FBERROR_BUFFER_TOO_SMALL;
  } else {
    memcpy(destination, instance->buffers[instance->complete_buffer], instance->buffer_size);
    *token = instance->frame_token;
  }

  mtx_unlock(&instance->mutex); // TODO: handle error
  
  return out_error;
}

static int dummy_has_update(void *data, int buffer_id, unsigned int last_token) {
  if (buffer_id != 0) {
    return FALSE;
  }
  
  fbsource_dummy_t *instance = data;
  return (last_token != instance->frame_token);
}

static const fbsource_module_t dummy_module = {
    ._create = dummy_create,
    ._destroy = dummy_destroy,
    .configure_module = dummy_configure_module,
    .get_num_buffers = dummy_get_num_buffers,
    .get_module_configuration = dummy_get_module_configuration,
    .get_buffer_configuration = dummy_get_buffer_configuration,
    .copy_latest_frame = dummy_copy_latest_frame,
    .has_update = dummy_has_update
};

FBSOURCE_MODULE_DESCRIPTOR(dummy, "Dummy", FBMODULE_LICENSE_MIT, dummy_module)
