#ifndef FBSOURCE_CAPTURE_WINRT_DXGI_H
#define FBSOURCE_CAPTURE_WINRT_DXGI_H

#include "../../../support/threads/threads_compat.h"

#include "../../../framework/src/framebuffet.h"

#include "window_utils.h"
#include "winrt_dxgi.h"

#define BYTES_PER_PIXEL (3)

typedef struct {
    mtx_t mutex;
    win_d3d_device_t *d3d_device;

    framebuffet_t *framebuffet;
    void *ext_instance_ref;

    volatile bool stop_generator;
    thrd_t generator_thread;
    bool has_generator_thread;

    fbsource_capture_session_t *capture_session;
    bool has_capture_session;

    fbsource_token_t frame_token;
    fbcollections_map_entry_t* source_config;

    char *window_search_name_exact;
    winrt_dxgi_capture_flags_t capture_flags;
    int crop_left;
    int crop_right;
    int crop_top;
    int crop_bottom;

    unsigned int width;
    unsigned int height;
    unsigned long buffer_size;

    unsigned char *buffers[2];
    int complete_buffer;

    // only handled by capture session (accessed without mutex)
    unsigned char *internal_buffer;
    size_t internal_buffer_size;
    window_dimensions_t expected_window_dimensions;
    HWND window_handle;
    unsigned int offset_x;
    unsigned int offset_y;
} fbsource_capture_winrt_dxgi_t;


#endif //FBSOURCE_CAPTURE_WINRT_DXGI_H
