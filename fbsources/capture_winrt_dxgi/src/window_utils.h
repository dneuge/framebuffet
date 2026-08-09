#ifndef FBSOURCE_CAPTURE_WINRT_DXGI_WINDOW_UTILS_H
#define FBSOURCE_CAPTURE_WINRT_DXGI_WINDOW_UTILS_H

// Based on API documentation published by Microsoft under CC-BY 4.0 and MIT licenses on GitHub.
//
// Earliest commit hash with license information that is early enough to only hold minimal (if any) AI-slop:
//   07512580a99bac226f8730c8f85344270f1beeff (1 March 2022)
//
// https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-enumwindows.md
// https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-enumchildwindows.md

#include <stdbool.h>

#ifndef TARGET_WINDOWS
#include "foreign_api.h"
#else
#include <windows.h>
#endif

typedef struct _window_entry_t window_entry_t;

typedef struct _window_entry_t {
    HWND handle;
    char *title;
    char *class_name;
    DWORD style;
    DWORD style_extended;

    window_entry_t *next;
} window_entry_t;

/**
 * Lists all top-level windows which may be eligible for capture.
 *
 * Note that on Windows practically all GUI widgets are a "window" so top-level window
 * has the same meaning as an application window on any other system/framework.
 *
 * @return first capturable top-level window, NULL on error or if no windows found
 */
window_entry_t* list_capturable_windows();

/**
 * Destroys all window entries, starting from the given root entry.
 *
 * Users must only call this function on whole first top-level entry as returned by list_windows,
 * otherwise the hierarchical structure will corrupt (references cannot be nulled).
 *
 * @param first_entry first top-most window entry to delete; may be NULL
 */
void destroy_window_entries(window_entry_t *first_entry);

typedef struct {
    unsigned int width;
    unsigned int height;
} window_dimensions_t;

/**
 * Retrieves the dimensions for the specified window.
 *
 * Window size excludes drop shadow but includes frame to match the expected result of capturing the window
 * through DXGI.
 *
 * @param window_handle handle of window to get dimensions of
 * @param dimensions will be updated to the window's dimensions, if successful
 * @return true if successful, false on error
 */
bool get_window_dimensions(HWND window_handle, window_dimensions_t *dimensions);

#endif //FBSOURCE_CAPTURE_WINRT_DXGI_WINDOW_UTILS_H
