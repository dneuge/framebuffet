#ifndef FBSOURCE_CAPTURE_SCK_CAPTURE_H
#define FBSOURCE_CAPTURE_SCK_CAPTURE_H

#include <stdbool.h>

#ifdef FBSOURCE_CAPTURE_SCK_CONTENT_EXPORT_OBJC
#define FBSOURCE_CAPTURE_SCK_CAPTURE_EXPORT_OBJC
#endif
#include "sck_content.h"

#define FBSOURCE_SCK_PIXEL_FORMAT_UNKNOWN (0)
#define FBSOURCE_SCK_PIXEL_FORMAT_32BGRA (1)
typedef uint8_t fbsource_sck_pixel_format_t;

typedef struct {
       void *buffer;
       size_t buffer_size;
       size_t bytes_per_pixel;
       size_t bytes_per_row;
       double content_offset_x;
       double content_offset_y;
       double content_width;
       double content_height;
       double content_scale;
       fbsource_sck_pixel_format_t pixel_format;
} fbsource_sck_frame_t;

typedef void(*sck_frame_callback_f)(void *ref, fbsource_sck_frame_t *frame);

typedef uint8_t fbsource_sck_state_t;
#define FBSOURCE_SCK_STATE_UNINITIALIZED (0)
#define FBSOURCE_SCK_STATE_SETUP         (1)
#define FBSOURCE_SCK_STATE_STARTING      (2)
#define FBSOURCE_SCK_STATE_STARTED       (3)
#define FBSOURCE_SCK_STATE_START_FAILED  (4)
#define FBSOURCE_SCK_STATE_STOPPING      (5)
#define FBSOURCE_SCK_STATE_STOP_FAILED   (6)
#define FBSOURCE_SCK_STATE_STOPPED       (7)

#ifdef FBSOURCE_CAPTURE_SCK_CONTENT_EXPORT_OBJC
@interface CaptureHandler : NSObject<SCStreamDelegate, SCStreamOutput> {
    void *ext_instance_ref;
    fbsource_sck_state_t state;

    sck_frame_callback_f frame_callback;
    void* frame_callback_ref;

    window_parameters_t window_params;

    SCWindow *window;
    SCContentFilter *contentFilter;
    CGColorRef bgColorRef; // this is actually a pointer that needs to be freed
    SCStreamConfiguration *streamConfig;
    dispatch_queue_t queue; // this may be a pointer, depending on OS version; release only through API
    bool has_queue; // because we cannot be sure if dispatch_queue_t actually stays a pointer in future releases
    SCStream *stream;
};

// TODO: implement all callback methods

-(bool)setupFor:(void*)ext_instance_ref
       captureWindow:(window_parameters_t)window_params
       withTimeout:(uint32_t)timeout_millis
       withCallback:(sck_frame_callback_f)frame_callback
       withCallbackReference:(void*)frame_callback_ref;

-(bool)startCaptureWithTimeout:(uint32_t)timeout_millis;

-(bool)stopCaptureWithTimeout:(uint32_t)timeout_millis;

-(void)stream:(SCStream*)stream
       didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                      ofType:(SCStreamOutputType)type;

-(void)stream:(SCStream*)stream
       didStopWithError:(NSError*)error;

-(void)streamDidBecomeActive:(SCStream*)stream;

-(void)streamDidBecomeInactive:(SCStream*)stream;

-(void)outputVideoEffectDidStartForStream:(SCStream*)stream;

-(void)outputVideoEffectDidStopForStream:(SCStream*)stream;

-(void)outputVideoEffectDidFailForStream:(SCStream*)stream
                               withError:(NSError*)error;

@end

typedef CaptureHandler* fbsource_sck_capture_handler_t;
#else
typedef void* fbsource_sck_capture_handler_t;
#endif

fbsource_sck_capture_handler_t setup_capture(void *ext_instance_ref, window_parameters_t window_params, uint32_t timeout_millis, sck_frame_callback_f frame_callback, void *frame_callback_ref);
bool start_capture(fbsource_sck_capture_handler_t handler, uint32_t timeout_millis);
bool stop_capture(fbsource_sck_capture_handler_t *handler, uint32_t timeout_millis);

#endif //FBSOURCE_CAPTURE_SCK_CAPTURE_H
