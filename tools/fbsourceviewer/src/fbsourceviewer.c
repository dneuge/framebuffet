#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <SDL.h>

#include <framebuffet.h>

static int num_digits(int value) {
  int digits = 0;

  if (value == 0) {
    return 1;
  }
  
  if (value < 0) {
    value = -value;
  }

  while (value > 0) {
    value /= 10;
    digits++;
  }

  return digits;
}

static void config_put_uint(fbcollections_map_entry_t *config, char *key, unsigned int value) {
  // TODO: error handling on failing malloc
  
  int key_size = strlen(key) + 1;
  char *key_copy = malloc(key_size);
  memcpy(key_copy, key, key_size);
  key_copy[key_size - 1] = 0;
  
  int value_size = num_digits(value) + 1;
  char *value_s = malloc(value_size);
  snprintf(value_s, value_size, "%d", value);
  value_s[value_size-1] = 0;

  fbcollections_map_put_and_free_old_entry_with_payloads(config, key_copy, strlen(key)+1, value_s, value_size);
}

static void sleep_millis(int milliseconds) {
  thrd_sleep(&(struct timespec){.tv_nsec=(milliseconds * 1000000)}, NULL);
}

int main(int argc, char *argv[]) {
  int res;
  
  framebuffet_t *framebuffet = framebuffet_init();

  if (argc < 5) {
    printf("Call with config file, source ID and buffer ID to display and module paths.");
    return 1;
  }

  char *config_path = argv[1];
  char *source_id = argv[2];
  int buffer_id = atoi(argv[3]);
  
  for (int i=4; i<argc; i++) {
    printf("loading %s\n", argv[i]);
    res = framebuffet_load_module(framebuffet, argv[i]);
    if (res != FBERROR_NONE) {
      printf("error loading module %s: %d\n", argv[i], res);
      return 1;
    }
  }

  /*
  res = framebuffet_load_configuration(config_path);
  if (res != FBERROR_NONE) {
    printf("error loading config %s: %d", config_path, res);
    return 1;
  }
  */

  printf("source ID: %s\n", source_id);
  printf("buffer ID: %d\n", buffer_id);
  
  int width = 2048;
  int height = 2048;

  printf("instantiating source module\n");
  fbsource_module_instance_entry_t *source;
  res = framebuffet_create_source_instance(framebuffet, &source, "Dummy", "myDummy");
  if (res != FBERROR_NONE) {
    printf("failed to instantiate module: %d\n", res);
    return 1;
  }

  printf("creating config map\n");
  fbcollections_map_entry_t *config = fbcollections_map_create();
  config_put_uint(config, "width", width);
  config_put_uint(config, "height", height);
  
  printf("configuring source instance\n");
  res = framebuffet_configure_source_instance(source, config);
  if (res != FBERROR_NONE) {
    printf("failed to configure module: %d\n", res);
    return 1;
  }

  printf("freeing config map\n");
  fbcollections_map_free_map_and_payloads(config);
  
  printf("querying FB config for buffer %d", buffer_id);
  fbsource_buffer_config_t *fb_config = NULL;
  while (1) {
    fb_config = framebuffet_get_buffer_configuration(source, buffer_id);
    if (fb_config) {
      break;
    }

    printf(".");
    fflush(stdout);

    sleep_millis(250);
  }
  printf("\n");
  
  unsigned long buffer_size = fb_config->size;
  
  printf("allocating buffer, size=%ld\n", buffer_size);
  unsigned char *buffer = (unsigned char*) malloc(buffer_size);
  if (!buffer) {
    printf("buffer allocation failed\n");
    return 1;
  }
  printf("allocated\n");

  // create SDL window
  printf("initializing SDL\n");
  SDL_Init(SDL_INIT_VIDEO);
  
  printf("SDL initialized, creating window\n");
  SDL_Window *window = SDL_CreateWindow("FBSourceViewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
  if (!window) {
    printf("Failed to open SDL window\n");
    return 1;
  }
  
  printf("window created, retrieving surface\n");
  SDL_Surface *window_surface = SDL_GetWindowSurface( window );
  if (!window_surface) {
    printf("no SDL window surface\n");
    return 1;
  }
  printf("surface retrieved\n");
  
  int frames_received = 0;
  unsigned int token = 0;
  while (1) {
    sleep_millis(10);
 
    // check if window has been closed
    int shutdown = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
        shutdown = 1;
        break;
      }
    }
    if (shutdown) {
      break;
    }

    // go back to sleep if there is no new frame
    if (!framebuffet_has_update(source, buffer_id, token)) {
      continue;
    }

    // get the new frame
    res = framebuffet_copy_latest_frame(source, buffer, buffer_size, buffer_id, &token);
    if (res) {
      printf("error %d\n", res);
      continue;
    }

    // update window
    SDL_Surface *frame_surface = SDL_CreateRGBSurfaceWithFormatFrom((void*) buffer, width, height, 24, 3*width, SDL_PIXELFORMAT_RGB24);
    if (!frame_surface) {
      printf("no SDL frame surface\n");
      break;
    }
    SDL_BlitSurface(frame_surface, NULL, window_surface, NULL);
    SDL_UpdateWindowSurface( window );
    SDL_FreeSurface(frame_surface);

    /*
    // dump to console
    printf("%04d ", frames_received);
    for (unsigned long i=0; i<bufferSize; i+=3) {
      printf("%02x%02x%02x ", buffer[i], buffer[i+1], buffer[i+2]);
    }
    printf("\n");
    */

    frames_received++;
  }

  printf("done\n");
  free(buffer);

  SDL_DestroyWindow(window);
  SDL_Quit();
};
