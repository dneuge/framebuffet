#include <stdbool.h>
#include <stdlib.h>

#include <windows.h>
#include <winuser.h>
#include <Dwmapi.h>

#include "utils.h"

#include "window_utils.h"

#include <string.h>
#include <limits.h>

/**
 * Documentation for callback EnumWindowsProc is not documented under a compatible license (see developer documentation
 * on learn.microsoft.com licensing issues) so the APIs have instead been based on signatures and behaviour as inferred
 * from reference code.
 *
 * Since the windows listed by Windows are all kinds of loose GUI elements in addition to the few actual application
 * windows we are interested in, the filter criteria present on the reference project had to be reproduced.
 *
 * Source: https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/WindowList.cpp (licensed MIT)
 */


#define STOP_ENUMERATION FALSE
#define CONTINUE_ENUMERATION TRUE

#define CLASS_NAME_BUFFER_SIZE 4096

typedef struct {
    window_entry_t *first_entry;
    window_entry_t *last_entry;
    HWND shell_window;
    bool failed;
} collector_state_t;

/**
 * Returns a copy of the referenced window's title text.
 *
 * The returned copy is to be managed by the caller and must be freed at given time.
 *
 * @param window_handle handle of window to retrieve title for
 * @return copy of window title, NULL on error or if window has no title
 */
static char *copy_window_title(HWND window_handle) {
    // Documentation:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-getwindowtextlengtha.md
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-getwindowtexta.md

    int length = GetWindowTextLengthA(window_handle);
    if (length <= 0) {
        return NULL;
    }

    int size = length + 1;
    if (size < 0) {
        return NULL;
    }

    char *title = zmalloc(size);
    if (!title) {
        return NULL;
    }

    int res = GetWindowTextA(window_handle, title, size);
    if (res <= 0) {
        free(title);
        return NULL;
    }

    // make sure the title is still terminated
    title[length] = 0;

    return title;
}

static char *copy_window_class(HWND window_handle) {
    // Documentation:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-getclassnamea.md

    char buf[CLASS_NAME_BUFFER_SIZE] = {0,};
    int res = GetClassNameA(window_handle, buf, CLASS_NAME_BUFFER_SIZE);
    if (res < 1 || res >= CLASS_NAME_BUFFER_SIZE) {
        return NULL;
    }

    return copy_string(buf, res+1);
}

static window_entry_t* create_window_entry(HWND handle) {
    window_entry_t *out = zmalloc(sizeof(window_entry_t));
    if (!out) {
        return NULL;
    }

    out->handle = handle;
    out->title = copy_window_title(handle);
    out->class_name = copy_window_class(handle);
    out->style = GetWindowLongW(handle, GWL_STYLE);
    out->style_extended = GetWindowLongW(handle, GWL_EXSTYLE);

    return out;
}

static bool is_capturable_window(window_entry_t *entry, collector_state_t *state) {
    // all criteria copied from https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/WindowList.cpp
    // extended by defensive checks

    if (
           !entry || !state
        || !entry->title || !entry->class_name
        || entry->handle == state->shell_window
        || (entry->style & WS_VISIBLE) == 0
        || (entry->style & WS_DISABLED) != 0
        || (entry->style_extended & WS_EX_TOOLWINDOW) != 0
    ) {
        return false;
    }

    if (GetAncestor(entry->handle, GA_ROOT) != entry->handle) {
        return false;
    }

    if (
           strcmp("Windows.UI.Core.CoreWindow", entry->class_name) == 0
        || strcmp("ApplicationFrameWindow",     entry->class_name) == 0
    ) {
        DWORD cloaked = 0;
        int res = DwmGetWindowAttribute(entry->handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (res == S_OK && cloaked == DWM_CLOAKED_SHELL) {
            return false;
        }
    }

    if (
           (strcmp("Task View",               entry->title) == 0 && strcmp("Windows.UI.Core.CoreWindow", entry->class_name) == 0)
        || (strcmp("DesktopWindowXamlSource", entry->title) == 0 && strcmp("Windows.UI.Core.CoreWindow", entry->class_name) == 0)
        || (strcmp("PopupHost",               entry->title) == 0 && strcmp("Xaml_WindowedPopupClass",    entry->class_name) == 0)
    ) {
        return false;
    }

    return true;
}

static BOOL list_windows_root_callback(HWND hwnd, LPARAM lParam) {
    collector_state_t *state = (collector_state_t*) lParam;
    if (!state) {
        return STOP_ENUMERATION;
    }

    window_entry_t *entry = create_window_entry(hwnd);
    if (!entry) {
        state->failed = true;
        return STOP_ENUMERATION;
    }

    if (!is_capturable_window(entry, state)) {
        destroy_window_entries(entry);
        return CONTINUE_ENUMERATION;
    }

    if (!state->first_entry) {
        state->first_entry = entry;
    }

    if (state->last_entry) {
        state->last_entry->next = entry;
    }

    state->last_entry = entry;

    return state->failed ? STOP_ENUMERATION : CONTINUE_ENUMERATION;
}

window_entry_t* list_capturable_windows() {
    collector_state_t state = {0,};
    state.shell_window = GetShellWindow();

    BOOL success = EnumWindows(list_windows_root_callback, (LPARAM) &state);
    if (!success || state.failed) {
        destroy_window_entries(state.first_entry);
        return NULL;
    }

    return state.first_entry;
}

void destroy_window_entries(window_entry_t *first_entry) {
    if (!first_entry) {
        return;
    }

    window_entry_t *entry = first_entry;
    while (entry) {
        window_entry_t *next_entry = entry->next;
        entry->next = NULL;

        if (entry->title) {
            free(entry->title);
            entry->title = NULL;
        }

        if (entry->class_name) {
            free(entry->class_name);
            entry->class_name = NULL;
        }

        free(entry);

        entry = next_entry;
    }
}

bool get_window_dimensions(HWND window_handle, window_dimensions_t *dimensions) {
    // NOTE: overscan of capture area appears to be 14x7, this method reports actual window size

    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/dwmapi/nf-dwmapi-dwmgetwindowattribute.md
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/dwmapi/ne-dwmapi-dwmwindowattribute.md
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/windef/ns-windef-rect.md

    if (!dimensions) {
        return false;
    }

    RECT rect = {0,};
    HRESULT res = DwmGetWindowAttribute(window_handle, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(RECT));
    if (res != S_OK) {
        return false;
    }

    long height = rect.bottom - rect.top;
    long width = rect.right - rect.left;
    if (width < 0 || height < 0 || width > UINT_MAX || height > UINT_MAX) {
        return false;
    }

    dimensions->height = height;
    dimensions->width = width;

    return true;
}

/*
bool get_window_dimensions_clientrect(HWND window_handle, window_dimensions_t *dimensions) {
    // NOTE: overscan of capture area appears to be 16x8, reported size may not match actual window size - this method is said to be outdated?

    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-getclientrect.md
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/windef/ns-windef-rect.md

    if (!dimensions) {
        return false;
    }

    RECT rect = {0,};
    BOOL res = GetClientRect(window_handle, &rect);
    if (res == 0) {
        return false;
    }

    long height = rect.bottom - rect.top;
    long width = rect.right - rect.left;
    if (width < 0 || height < 0 || width > UINT_MAX || height > UINT_MAX) {
        return false;
    }

    dimensions->height = height;
    dimensions->width = width;

    return true;
}
*/