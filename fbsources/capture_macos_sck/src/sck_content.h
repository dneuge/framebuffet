#ifndef FBSOURCE_CAPTURE_SCK_CONTENT_H
#define FBSOURCE_CAPTURE_SCK_CONTENT_H

#include <stdint.h>

typedef uint32_t window_id_t; // see CGWindowID

typedef struct {
    window_id_t id;
    double width;
    double height;
} window_parameters_t;

#define INVALID_WINDOW_PARAMETERS ((window_parameters_t){.id=0, .width=NAN, .height=NAN})

typedef struct window_list_item_s window_list_item_t;
typedef struct window_list_item_s {
    window_parameters_t params;
    char *title;

    window_list_item_t *next;
} window_list_item_t;

window_list_item_t* list_shareable_windows(uint32_t timeout_millis);
void free_window_list(window_list_item_t *list);

#ifdef FBSOURCE_CAPTURE_SCK_CONTENT_EXPORT_OBJC
// export extra methods that require OS/Objective-C to be used
// these functions generally rely on an auto-release pool being provided by the caller

#include <ScreenCaptureKit/ScreenCaptureKit.h>

SCWindow* findWindow(window_id_t id, uint32_t timeoutMillis);
#endif

#endif //FBSOURCE_CAPTURE_SCK_CONTENT_H
