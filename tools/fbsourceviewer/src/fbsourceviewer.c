#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <SDL.h>

#include <framebuffet.h>

#define LOG_BUFFER_SIZE 8192

#define NUM_IMAGE_BUFFERS 3
#define DIFF_VIEW_BUFFER_ID 2
#define BYTES_PER_PIXEL 3

#define DIFF_CHANGED_RED 127
#define DIFF_CHANGED_GREEN 0
#define DIFF_CHANGED_BLUE 0
#define DIFF_UNCHANGED_RED 0
#define DIFF_UNCHANGED_GREEN 127
#define DIFF_UNCHANGED_BLUE 0

#define DIFF_CHANGED_RED_FRACTION 1.0f
#define DIFF_CHANGED_GREEN_FRACTION 0.0f
#define DIFF_CHANGED_BLUE_FRACTION 0.0f
#define DIFF_UNCHANGED_RED_FRACTION 0.0f
#define DIFF_UNCHANGED_GREEN_FRACTION 1.0f
#define DIFF_UNCHANGED_BLUE_FRACTION 0.0f

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

static const char* log_level_tag(fblog_level_t level) {
    switch (level) {
        case FBLOG_LEVEL_ERROR: return "[ERROR]";
        case FBLOG_LEVEL_WARN:  return "[WARN] ";
        case FBLOG_LEVEL_INFO:  return "[INFO] ";
        case FBLOG_LEVEL_DEBUG: return "[DEBUG]";
        case FBLOG_LEVEL_TRACE: return "[TRACE]";
        default:                return "[???]  ";
    }
}

static mtx_t log_mutex;

static void log_msg(char *message, ...) {
    bool holds_mutex = (mtx_lock(&log_mutex) == thrd_success);

    va_list args;
    va_start(args, message);
    vprintf(message, args);
    va_end(args);
    
    printf("\n");

    if (holds_mutex) {
        mtx_unlock(&log_mutex);
    }
}

static void log_receiver(fblog_level_t level, char *message) {
    char buffer[LOG_BUFFER_SIZE] = { 0 };
    if (!snprintf(buffer, LOG_BUFFER_SIZE, "[FrameBuffet] %s %s", log_level_tag(level), message)) {
        log_msg("dropped FrameBuffet log message");
        return;
    }

    log_msg(buffer);
}

static void flip_y(uint8_t *buffer, uint16_t line_length, uint16_t rows) {
    if (!buffer) {
        return;
    }

    const size_t total_size = line_length * rows;
    uint8_t *tmp_buffer = malloc(total_size);

    uint8_t *out = tmp_buffer;
    uint8_t *in = buffer + total_size - line_length;
    const uint8_t *in_min = buffer;
    while (in >= in_min) {
        memcpy(out, in, line_length);
        out += line_length;
        in -= line_length;
    }

    memcpy(buffer, tmp_buffer, total_size);
    free(tmp_buffer);
}

static inline uint8_t max2b(const uint8_t a, const uint8_t b) {
    return a > b ? a : b;
}

static inline uint8_t max3b(const uint8_t a, const uint8_t b, const uint8_t c) {
    return max2b(max2b(a, b), c);
}

int main(int argc, char *argv[]) {
    int res;

    if (mtx_init(&log_mutex, mtx_plain) != thrd_success) {
        printf("failed to create log mutex\n");
        return 1;
    }

    framebuffet_t *framebuffet = framebuffet_init();
    framebuffet_provide_log_receiver(framebuffet, log_receiver);
    //framebuffet_set_minimum_log_level(framebuffet, FBLOG_LEVEL_TRACE);
    framebuffet_set_minimum_log_level(framebuffet, FBLOG_LEVEL_DEBUG);
    //framebuffet_set_minimum_log_level(framebuffet, FBLOG_LEVEL_INFO);

    if (argc < 6) {
        printf("Call with config file, source ID and buffer ID to display, flipY flag (0/1) and module paths.\n");
        return 1;
    }

    char *config_path = argv[1];
    char *source_id = argv[2];
    int framebuffet_buffer_id = atoi(argv[3]);
    bool should_flip_y = (atoi(argv[4]) == 1);

    for (int i=5; i<argc; i++) {
        printf("loading %s\n", argv[i]);
        res = framebuffet_load_module(framebuffet, argv[i]);
        if (res != FBERROR_NONE) {
            printf("error loading module %s: %d\n", argv[i], res);
            return 1;
        }
    }

    res = framebuffet_configure_from_actions_json_file(framebuffet, config_path);
    if (res != FBERROR_NONE) {
        printf("error loading config %s: %d", config_path, res);
        return 1;
    }

    printf("source ID: %s\n", source_id);
    printf("buffer ID: %d\n", framebuffet_buffer_id);

    int width = 2048;
    int height = 2048;
    const int bytes_per_line = width * BYTES_PER_PIXEL;

    /*
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
    */

    fbsource_module_instance_entry_t *source = framebuffet_get_source_instance(framebuffet, source_id);
    if (!source) {
        printf("no source named %s", source_id);
        return 1;
    }

    printf("querying FB config for buffer %d", framebuffet_buffer_id);
    fbsource_buffer_config_t fb_config;
    while (1) {
        res = framebuffet_get_buffer_configuration(source, framebuffet_buffer_id, &fb_config);
        if (res == FBERROR_NONE) {
            break;
        }

        printf(".");
        fflush(stdout);

        sleep_millis(250);
    }
    printf("\n");

    unsigned long buffer_size = fb_config.size;
    void *buffer_mem = NULL;
    size_t total_buffer_size = buffer_size * NUM_IMAGE_BUFFERS;
    printf("allocating %d image buffers, size=%lu, total=%lu\n", NUM_IMAGE_BUFFERS, buffer_size, total_buffer_size);
    buffer_mem = malloc(total_buffer_size); // consecutive allocation used for easier implementation of diff view
    if (!buffer_mem) {
        printf("buffer allocation failed\n");
        return 1;
    }
    memset(buffer_mem, 0, total_buffer_size);
    uint8_t *buffers[NUM_IMAGE_BUFFERS] = {0};
    for (int i=0; i<NUM_IMAGE_BUFFERS; i++) {
        buffers[i] = (uint8_t*)(buffer_mem + (i * buffer_size));
    }
    printf("allocated\n");

    // create SDL window
    printf("initializing SDL\n");
    SDL_Init(SDL_INIT_VIDEO);

    printf("SDL initialized, creating window\n");
    const char *window_title = should_flip_y ? "FBSourceViewer [flipY]" : "FBSourceViewer";
    SDL_Window *window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
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
    fbsource_token_t token = {0};
    int current_frame_buffer = 0;
    while (1) {
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

        framebuffet_wait_for_update_micros(source, framebuffet_buffer_id, token, 100000);
        //framebuffet_wait_for_update(source, buffer_id, token);

        /*
        // go back to sleep if there is no new frame
        if (!framebuffet_has_update(source, buffer_id, token)) {
            continue;
        }
        */

        // get the new frame
        current_frame_buffer = (current_frame_buffer + 1) % 2;
        uint8_t *current_frame = buffers[current_frame_buffer];
        res = framebuffet_copy_latest_frame(source, current_frame, buffer_size, framebuffet_buffer_id, &token);
        if (res) {
            printf("error %d\n", res);
            continue;
        }

        // flip?
        if (should_flip_y) {
            flip_y(current_frame, bytes_per_line, height);
        }

        // show difference between frames?
        int diff_view_granularity = -1; // exponent 2^x, negative number disables, 0 compares exact pixels
        bool diff_view_overlay = true; // blends with image if true; false shows only map of differences
        bool enable_diff_view = (diff_view_granularity >= 0);
        if (enable_diff_view) {
            // diff view only works with tiles that end exactly on boundaries
            const int diff_view_size = 1 << diff_view_granularity;
            enable_diff_view = (width % diff_view_size == 0) && (height % diff_view_size == 0);
            if (!enable_diff_view) {
                printf("diff view disabled, image cannot be split into exact tiles\n");
            }
        }
        if (enable_diff_view) {
            uint32_t num_tiles_changed = 0;
            int diff_view_size = 1 << diff_view_granularity;
            int diff_view_line_num_bytes = diff_view_size * BYTES_PER_PIXEL;
            uint8_t *diff_frame = buffers[DIFF_VIEW_BUFFER_ID];
            uint8_t *previous_frame = buffers[(current_frame_buffer+1) % 2];
            int tiles_horizontal = width / diff_view_size;
            if (width % diff_view_size != 0) {
                tiles_horizontal++;
            }
            int tiles_vertical = height / diff_view_size;
            if (height % diff_view_size != 0) {
                tiles_vertical++;
            }
            for (int tile_y=0; tile_y<tiles_vertical; tile_y++) {
                for (int tile_x=0; tile_x<tiles_horizontal; tile_x++) {
                    bool tile_changed = false;
                    const size_t start_offset = (tile_y * diff_view_size * bytes_per_line) + (tile_x * diff_view_line_num_bytes);
                    uint8_t *current_pixel_start = current_frame + start_offset;
                    uint8_t *previous_pixel_start = previous_frame + start_offset;

                    const uint8_t *current_pixel_last_line_start = current_pixel_start + ((diff_view_size-1) * bytes_per_line);
                    while (!tile_changed && (current_pixel_start <= current_pixel_last_line_start)) {
                        const uint8_t *current_pixel_start_of_line = current_pixel_start;
                        const uint8_t *previous_pixel_start_of_line = previous_pixel_start;
                        const uint8_t *current_pixel_past_eol = current_pixel_start + diff_view_line_num_bytes;
                        while (current_pixel_start < current_pixel_past_eol) {
                            // NOTE: this actually runs over to the next buffer on last pixel - that's okay because we still own that memory (consecutive buffer allocation)
                            // memory RGBr => register rBGR (Little Endian), remove "r" byte
                            //const uint32_t current_pixel_combined = (*(uint32_t*)current_pixel_start) & (uint32_t) 0x00FFFFFF;
                            //const uint32_t previous_pixel_combined = (*(uint32_t*)previous_pixel_start) & (uint32_t) 0x00FFFFFF;
                            const uint32_t current_pixel_combined = (current_pixel_start[0] << 16) | (current_pixel_start[1] << 8) | current_pixel_start[2];
                            const uint32_t previous_pixel_combined = (previous_pixel_start[0] << 16) | (previous_pixel_start[1] << 8) | previous_pixel_start[2];
                            if (current_pixel_combined != previous_pixel_combined) {
                                tile_changed = true;
                                break;
                            }
                            current_pixel_start += BYTES_PER_PIXEL;
                            previous_pixel_start += BYTES_PER_PIXEL;
                        }
                        current_pixel_start = (uint8_t*)current_pixel_start_of_line + bytes_per_line;
                        previous_pixel_start = (uint8_t*)previous_pixel_start_of_line + bytes_per_line;
                    }

                    if (tile_changed) {
                        num_tiles_changed++;
                    }

                    // fill diff frame
                    uint8_t *diff_pos = diff_frame + start_offset;
                    uint8_t *current_frame_pos = current_frame + start_offset;
                    const uint8_t *diff_last_line_start = diff_pos + ((diff_view_size-1) * bytes_per_line);
                    while (diff_pos <= diff_last_line_start) {
                        const uint8_t *diff_start_of_line = diff_pos;
                        const uint8_t *current_frame_start_of_line = current_frame_pos;
                        const uint8_t *diff_pos_past_eol = diff_pos + diff_view_line_num_bytes;
                        while (diff_pos < diff_pos_past_eol) {
                            if (diff_view_overlay) {
                                //const float current_frame_brightness = (float)(current_frame_pos[0] + current_frame_pos[1] + current_frame_pos[2]) / 3.0f;
                                const float current_frame_brightness = max3b(current_frame_pos[0], current_frame_pos[1], current_frame_pos[2]);
                                const float structure_frac = 0.5f + (current_frame_brightness / 256.0f / 2.0f);
                                if (tile_changed) {
                                    *(diff_pos++) = (uint8_t) ((DIFF_CHANGED_RED_FRACTION * structure_frac) * 256.0f);
                                    *(diff_pos++) = (uint8_t) ((DIFF_CHANGED_GREEN_FRACTION * structure_frac) * 256.0f);
                                    *(diff_pos++) = (uint8_t) ((DIFF_CHANGED_BLUE_FRACTION * structure_frac) * 256.0f);
                                } else {
                                    *(diff_pos++) = (uint8_t) ((DIFF_UNCHANGED_RED_FRACTION * structure_frac) * 256.0f);
                                    *(diff_pos++) = (uint8_t) ((DIFF_UNCHANGED_GREEN_FRACTION * structure_frac) * 256.0f);
                                    *(diff_pos++) = (uint8_t) ((DIFF_UNCHANGED_BLUE_FRACTION * structure_frac) * 256.0f);
                                }
                            } else {
                                if (tile_changed) {
                                    *(diff_pos++) = DIFF_CHANGED_RED;
                                    *(diff_pos++) = DIFF_CHANGED_GREEN;
                                    *(diff_pos++) = DIFF_CHANGED_BLUE;
                                } else {
                                    *(diff_pos++) = DIFF_UNCHANGED_RED;
                                    *(diff_pos++) = DIFF_UNCHANGED_GREEN;
                                    *(diff_pos++) = DIFF_UNCHANGED_BLUE;
                                }
                            }
                            current_frame_pos += BYTES_PER_PIXEL;
                        }
                        diff_pos = (uint8_t*)diff_start_of_line + bytes_per_line;
                        current_frame_pos = (uint8_t*)current_frame_start_of_line + bytes_per_line;
                    }
                }
            }

            const uint32_t num_tiles = tiles_horizontal * tiles_vertical;
            if (diff_view_granularity > 1) {
                printf("%u tiles of %upx: %u changed (%.1f%%)\n", num_tiles, diff_view_size, num_tiles_changed, ((double)num_tiles_changed/num_tiles * 100.0));
            } else {
                // tiles are actually pixels
                printf("%u pixels: %u changed (%.1f%%)\n", num_tiles, num_tiles_changed, ((double)num_tiles_changed/num_tiles * 100.0));
            }
        }

        // update window
        SDL_Surface *frame_surface = SDL_CreateRGBSurfaceWithFormatFrom(enable_diff_view ? buffers[DIFF_VIEW_BUFFER_ID] : current_frame, width, height, 24, BYTES_PER_PIXEL*width, SDL_PIXELFORMAT_RGB24);
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

    printf("destroying window\n");
    SDL_DestroyWindow(window);

    printf("shutting down FrameBuffet\n");
    if (source) {
      	printf("viewer releases source instance\n");
        res = framebuffet_release_source_instance(source);
        source = NULL;
        if (res == FBERROR_NONE) {
      		printf("viewer source instance released\n");
        } else {
      		printf("viewer source instance failed to release: %d\n", res);
        }
    }
    int num_sources_destroyed = 0;
    uint8_t module_destructor_failed = 0;
    res = framebuffet_destroy_retrying(framebuffet, &num_sources_destroyed, 30, 1000, &module_destructor_failed);
    if (res != FBERROR_NONE) {
    	printf("FrameBuffet failed to terminate: %d\n", res);
    }
    if (module_destructor_failed != 0) {
      	printf("Number of failed FrameBuffet module destructors: %hu\n", module_destructor_failed);
    }

    printf("done\n");
    free(buffer_mem);

    SDL_Quit();

    mtx_destroy(&log_mutex);
};
