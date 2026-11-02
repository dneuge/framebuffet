#include <stdlib.h>

#include <ScreenCaptureKit/ScreenCaptureKit.h>

#include "threads_compat.h"

#include "utils.h"
#include "utils_objc.h"

#include "sck_content.h"

static window_list_item_t* create_window_list_item(SCWindow *sck_window) {
    if (!sck_window) {
        return NULL;
    }

    window_list_item_t *out = zmalloc(sizeof(window_list_item_t));
    if (!out) {
        return NULL;
    }

    out->params.id = [sck_window windowID];
    out->title = [Utils createCString:[sck_window title]];

    CGRect frame = [sck_window frame];
    out->params.width = frame.size.width;
    out->params.height = frame.size.height;

    return out;
}

@interface ListShareableWindowsResult : NSObject {
    bool done;
    SCShareableContent *shareableContent;
    NSError *error;
}

- (void)complete:(SCShareableContent*)shareableContent
        withError:(NSError*)error;

- (bool)waitWithTimeout:(uint32_t)timeout_millis;

- (SCShareableContent*)getShareableContent;
- (NSError*)getError;
@end

@implementation ListShareableWindowsResult
- (void)complete:(SCShareableContent*)newShareableContent withError:(NSError*)newError {
    if (self->done) {
        return;
    }

    [newShareableContent retain];
    [error retain];

    self->shareableContent = newShareableContent;
    self->error = newError;
    self->done = true;
}

- (bool)waitWithTimeout:(uint32_t)timeout_millis {
    [Utils
        waitWithTimeout:timeout_millis
        completionCheck:^() {
            return self->done ? 1 : 0;
        }
    ];

    return self->done;
}

- (SCShareableContent*)getShareableContent {
    return self->shareableContent;
}

- (NSError*)getError {
    return self->error;
}

- (void)dealloc {
    [self->shareableContent release];
    [self->error release];
    self->done = false;

    [super dealloc];
}

@end

window_list_item_t* list_shareable_windows(uint32_t timeout_millis) {
    window_list_item_t *out = NULL;

    @autoreleasepool {
        ListShareableWindowsResult *res = [ListShareableWindowsResult new];

        [SCShareableContent
            getShareableContentExcludingDesktopWindows:FALSE
            onScreenWindowsOnly:TRUE
            completionHandler:^(SCShareableContent *shareableContent, NSError *error) {
                [res complete:shareableContent withError:error];
            }
        ];

        if (![res waitWithTimeout:timeout_millis]) {
            return NULL;
        }

        if ([res getError]) {
            return NULL;
        }

        NSEnumerator<SCWindow*> *scWindowsEnumerator = [[[res getShareableContent] windows] objectEnumerator];
        SCWindow *scWindow = nil;
        window_list_item_t *out_tail = NULL;
        while (scWindow = [scWindowsEnumerator nextObject]) {
            window_list_item_t *out_item = create_window_list_item(scWindow);
            if (!out_item) {
                free_window_list(out);
                return NULL;
            }

            if (!out_tail) {
                out = out_item;
                out_tail = out;
            } else {
                out_tail->next = out_item;
                out_tail = out_item;
            }
        }
    }

    return out;
}

SCWindow* findWindow(window_id_t id, uint32_t timeoutMillis) {
    @autoreleasepool {
        ListShareableWindowsResult *res = [ListShareableWindowsResult new];

        [SCShareableContent
            getShareableContentExcludingDesktopWindows:FALSE
            onScreenWindowsOnly:TRUE
            completionHandler:^(SCShareableContent *shareableContent, NSError *error) {
                [res complete:shareableContent withError:error];
            }
        ];

        if (![res waitWithTimeout:timeoutMillis]) {
            return nil;
        }

        if ([res getError]) {
            return nil;
        }

        NSEnumerator<SCWindow*> *scWindowsEnumerator = [[[res getShareableContent] windows] objectEnumerator];
        SCWindow *scWindow = nil;
        while (scWindow = [scWindowsEnumerator nextObject]) {
            if ([scWindow windowID] == id) {
                [scWindow retain];
                return scWindow;
            }
        }

        return nil;
    }
}

void free_window_list(window_list_item_t *list) {
    if (!list) {
        return;
    }

    window_list_item_t *item = list;
    while (item) {
        item->params.id = 0;
        item->params.width = NAN;
        item->params.height = NAN;

        if (item->title) {
            free(item->title);
            item->title = NULL;
        }

        window_list_item_t *next_item = item->next;
        item->next = NULL;

        free(item);

        item = next_item;
    }
}
