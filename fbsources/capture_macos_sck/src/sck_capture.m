#include <inttypes.h>

#include <ScreenCaptureKit/ScreenCaptureKit.h>

#include "../../../framework/src/framebuffet.h"

#include "utils.h"
#include "utils_objc.h"

#define FBSOURCE_CAPTURE_SCK_CONTENT_EXPORT_OBJC
#include "sck_capture.h"

#define ABS_DIMENSION_ERROR_TOLERANCE (0.1)

@implementation CaptureHandler
-(bool)setupFor:(void*)new_ext_instance_ref captureWindow:(window_parameters_t)new_window_params withTimeout:(uint32_t)timeout_millis withCallback:(sck_frame_callback_f)new_frame_callback withCallbackReference:(void*)new_frame_callback_ref {
    // NOTE: Only to be handled through setup_capture, do not call directly! On failure, memory needs to be released
    //       through deallocation (not done by this method).

    if (!new_ext_instance_ref) {
        // we are unable to log...
        return false;
    }

    if (!new_frame_callback) {
        FBLOG_WARN(new_ext_instance_ref, "setup_capture: missing required parameter frame_callback");
        return false;
    }

    if (self->state != FBSOURCE_SCK_STATE_UNINITIALIZED) {
        FBLOG_WARN(new_ext_instance_ref, "setup_capture: unexpected state %" PRIu8, self->state);
        return false;
    }

    self->ext_instance_ref = new_ext_instance_ref;
    self->window_params = new_window_params;
    self->frame_callback = new_frame_callback;
    self->frame_callback_ref = new_frame_callback_ref;

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: searching window");
    self->window = findWindow(self->window_params.id, timeout_millis);
    if (!self->window) {
        FBLOG_WARN(self->ext_instance_ref, "setup_capture: failed to find window %" PRIu32 " within %" PRIu32 "ms", window_params.id, timeout_millis);
        return false;
    }

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: checking window frame");
    CGRect windowFrame = [window frame];
    if (windowFrame.size.width != self->window_params.width || windowFrame.size.height != self->window_params.height) {
        FBLOG_WARN(
            self->ext_instance_ref, "setup_capture: window %" PRIu32 " changed dimensions; expected [%f, %f], got [%f, %f]",
            self->window_params.id, self->window_params.width, self->window_params.height, windowFrame.size.width, windowFrame.size.height
        );
        return false;
    }

    id nsnull = [NSNull null];

    // workaround for content filter failing to initialize sporadically
    // https://developer.apple.com/forums/thread/743615
    CGMainDisplayID();

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: initializing content filter");
    self->contentFilter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: setting up background color");
    self->bgColorRef = CGColorCreateGenericRGB(0.0, 0.0, 0.0, 1.0);

    // NOTE: apparently we need an at least 2x oversized buffer, otherwise we end up with oddly scaled-down content
    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: creating stream configuration");
    self->streamConfig = [SCStreamConfiguration new];
    self->streamConfig.width = lround(windowFrame.size.width);
    self->streamConfig.height = lround(windowFrame.size.height);
    self->streamConfig.scalesToFit = FALSE;
    self->streamConfig.preservesAspectRatio = TRUE;
    self->streamConfig.pixelFormat = kCVPixelFormatType_32BGRA;
    //self->streamConfig.pixelFormat = 'BGRA';
    //self->streamConfig.pixelFormat = *((OSType*) (char*) "ARGB"); // FIXME: use constant
    self->streamConfig.colorSpaceName = kCGColorSpaceSRGB;
    self->streamConfig.backgroundColor = bgColorRef; // TODO: make configurable?
    self->streamConfig.showsCursor = FALSE; // TODO: make configurable?
    self->streamConfig.shouldBeOpaque = TRUE; // TODO: documented as solid white - but is it even if we configured the background to black?!
    self->streamConfig.ignoreShadowsSingleWindow = TRUE;
    self->streamConfig.ignoreGlobalClipSingleWindow = TRUE;
    // TODO: make minimumFrameInterval configurable (max. FPS to be captured at)
    //self->streamConfig.captureResolution = SCCaptureResolutionNominal; // TODO: confirm "nominal" is the correct mode; this may refer to scaling back high DPI/Retina displays?
    self->streamConfig.capturesAudio = FALSE;
    self->streamConfig.streamName = @"FrameBuffet"; // TODO: include module instance name
    // TODO: make presenterOverlayPrivacyAlertSetting configurable?
    self->streamConfig.captureMicrophone = FALSE;
    self->streamConfig.captureDynamicRange = SCCaptureDynamicRangeSDR;
    self->streamConfig.includeChildWindows = TRUE;
    self->streamConfig.showMouseClicks = FALSE; // TODO: make configurable?

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: allocating stream");
    self->stream = [SCStream alloc];
    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: initializing stream");
    [self->stream initWithFilter:self->contentFilter configuration:self->streamConfig delegate:self];

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: creating dispatch queue");
    self->queue = dispatch_queue_create("framebuffet.VideoQueue", DISPATCH_QUEUE_SERIAL); // FIXME: use instance-specific name + random ID
    if (self->queue == nil || self->queue == nsnull) {
        FBLOG_WARN(self->ext_instance_ref, "setup_capture: failed to create dispatch queue");
        return false;
    }
    self->has_queue = true;

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: adding stream output");
    NSError *error = nil;
    [self->stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self->queue error:&error];
    if (error != nil && (id)error != nsnull) {
        char *msg = [Utils createCString:[error localizedDescription]];
        FBLOG_WARN(self->ext_instance_ref, "setup_capture: failed to add output stream: %s", msg);
        if (msg) {
            free(msg);
        }
        [error release]; // FIXME: this will crash the application if we're not supposed to release the error; should we, or not?
        return false;
    }

    FBLOG_DEBUG(self->ext_instance_ref, "setup_capture: complete");
    self->state = FBSOURCE_SCK_STATE_SETUP;

    return true;
}

-(bool)startCaptureWithTimeout:(uint32_t)timeout_millis {
    if (self->state < FBSOURCE_SCK_STATE_SETUP || self->state > FBSOURCE_SCK_STATE_STARTED) {
        FBLOG_WARN(self->ext_instance_ref, "start_capture: unexpected state %" PRIu8, self->state);
        return false;
    }

    id nsnull = [NSNull null];

    if (self->state == FBSOURCE_SCK_STATE_SETUP) {
        self->state = FBSOURCE_SCK_STATE_STARTING;

        [self->stream startCaptureWithCompletionHandler:^(NSError *error){
            if (error == nil || (id)error == nsnull) {
                FBLOG_DEBUG(self->ext_instance_ref, "start_capture: started without error");
                self->state = FBSOURCE_SCK_STATE_STARTED;
            } else {
                char *msg = [Utils createCString:[error localizedDescription]];
                FBLOG_WARN(self->ext_instance_ref, "start_capture: failed with error: %s", msg);
                if (msg) {
                    free(msg);
                }
                self->state = FBSOURCE_SCK_STATE_START_FAILED;
                //[error release];
            }
        }];
    }

    [Utils
        waitWithTimeout:timeout_millis
        completionCheck:^() {
            return self->state > FBSOURCE_SCK_STATE_STARTING;
        }
    ];

    return self->state == FBSOURCE_SCK_STATE_STARTED;
}

-(bool)stopCaptureWithTimeout:(uint32_t)timeout_millis {
    id nsnull = [NSNull null];

    if (self->state < FBSOURCE_SCK_STATE_STARTING || self->state == FBSOURCE_SCK_STATE_START_FAILED) {
        self->state = FBSOURCE_SCK_STATE_STOPPED;
    }

    if (self->state == FBSOURCE_SCK_STATE_STOP_FAILED) {
        // eeeh, I guess we just retry then?
        self->state = FBSOURCE_SCK_STATE_STARTED;
    }

    if (self->state == FBSOURCE_SCK_STATE_STARTING || self->state == FBSOURCE_SCK_STATE_STARTED) {
        self->state = FBSOURCE_SCK_STATE_STOPPING;

        [self->stream stopCaptureWithCompletionHandler:^(NSError *error){
            if (error == nil || (id)error == nsnull) {
                self->state = FBSOURCE_SCK_STATE_STOPPED;
            } else {
                char *msg = [Utils createCString:[error localizedDescription]];
                FBLOG_WARN(self->ext_instance_ref, "stop_capture: failed with error: %s", msg);
                if (msg) {
                    free(msg);
                }
                self->state = FBSOURCE_SCK_STATE_STOP_FAILED;
                //[error release];
            }
        }];
    }

    [Utils
        waitWithTimeout:timeout_millis
        completionCheck:^() {
            return self->state > FBSOURCE_SCK_STATE_STOPPING;
        }
    ];

    return self->state == FBSOURCE_SCK_STATE_STOPPED;
}

-(void)dealloc {
    // NOTE: must be stopped before; do not use directly, instead call stop_capture

    id nsnull = [NSNull null];

    if (self->stream != nil && (id)self->stream != nsnull) {
        [self->stream release];
    }
    self->stream = nil;

    if (self->has_queue && self->queue != nil && (id)self->queue != nsnull) {
        [self->queue release];
    }
    self->has_queue = false;
    self->queue = nil;

    if (self->streamConfig != nil && (id)self->streamConfig != nsnull) {
        [self->streamConfig release];
    }
    self->streamConfig = nil;

    if (bgColorRef) {
        CGColorRelease(bgColorRef);
    }
    bgColorRef = NULL;

    if (self->contentFilter != nil && (id)self->contentFilter != nsnull) {
        [self->contentFilter release];
    }
    self->contentFilter = nil;

    if (self->window != nil && (id)self->window != nsnull) {
        [self->window release];
    }
    self->window = nil;

    [super dealloc];
}

-(void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    // NOTE: There seems to be only one attachment and that one is a dictionary which cannot be fully inspected, neither
    //       through CFDictionary... functions nor via the XCode debugger (some entries exist but are "hidden").
    //       While all values are typed as pointers, they apparently are not if the value is a number. Instead,
    //       CFNumberRef must be accessed which is a boxing type. "Ref" may be misleading as directly accessing those
    //       memory locations crashes with SIGSEGV. That behaviour is undocumented and also not apparent from Swift
    //       code examples, probably due to auto-unboxing.

    if (!CMSampleBufferIsValid(sampleBuffer)) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: skipping invalid sample buffer");
        return;
    }

    if (type != SCStreamOutputTypeScreen) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: skipping unwanted frame type %ld", type);
        return;
    }

    id nsnull = [NSNull null]; // singleton reference used where actual NULL/nil is not allowed

    CFArrayRef sampleAttachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, FALSE);
    if (sampleAttachments == nil || (id)sampleAttachments == nsnull) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: received sample without attachments (nil, not zero count); skipping");
        return;
    }

    CFIndex numAttachments = CFArrayGetCount(sampleAttachments);
    if (numAttachments < 1) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: received sample without attachments (zero count); skipping");
        return;
    }
    CFMutableDictionaryRef sampleDictionary = (CFMutableDictionaryRef) CFArrayGetValueAtIndex(sampleAttachments, 0);

    CFNumberRef statusNumRef = (CFNumberRef) CFDictionaryGetValue(sampleDictionary, SCStreamFrameInfoStatus);
    SCFrameStatus status = SCFrameStatusBlank;
    if (!CFNumberGetValue(statusNumRef, kCFNumberIntType, &status)) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: received sample without status; skipping");
        return;
    }
    if (status != SCFrameStatusComplete) {
        FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample with unexpected status %ld; skipping", status);
        return;
    }

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (imageBuffer == nil || (id)imageBuffer == nsnull) {
        FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample without image buffer; skipping");
        return;
    }

    IOSurfaceRef ioSurfaceRef = CVPixelBufferGetIOSurface(imageBuffer);
    if (ioSurfaceRef == nil || (id)ioSurfaceRef == nsnull) {
        FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample whose buffer is not backed by an IOSurface; skipping");
        return;
    }

    void *ioSurfaceBaseAddr = IOSurfaceGetBaseAddress(ioSurfaceRef);
    if (ioSurfaceBaseAddr == nil || (id)ioSurfaceBaseAddr == nsnull) {
        FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample whose IOSurface has no base address; skipping");
        return;
    }

    // According to Swift documentation in XCode, the scale factor indicates the display's scaling factor.
    // It seems that this directly affects the content box, if the screen is being scaled back down (e.g. 4K TV showing
    // Full HD; undocumented behaviour).
    CFNumberRef scaleFactorRef = (CFNumberRef) CFDictionaryGetValue(sampleDictionary, SCStreamFrameInfoScaleFactor);
    CGFloat scaleFactor = NAN;
    if (!CFNumberGetValue(scaleFactorRef, kCFNumberCGFloatType, &scaleFactor)) {
        // Apple's example strictly requires this value but it shouldn't be important for us so we continue regardless
        // TODO: fail capture
        FBLOG_WARN(self->ext_instance_ref, "stream callback: received sample without scale factor");
        return;
    }

    CFDictionaryRef contentRectDictionary = (CFDictionaryRef) CFDictionaryGetValue(sampleDictionary, SCStreamFrameInfoContentRect);
    CGRect contentRect = CGRectNull;
    if (contentRectDictionary == nil || (id)contentRectDictionary == nsnull) {
        FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample has no contentRect");
    } else {
        if (!CGRectMakeWithDictionaryRepresentation(contentRectDictionary, &contentRect)) {
            FBLOG_DEBUG(self->ext_instance_ref, "stream callback: received sample has unparseable contentRect");
            contentRect = CGRectNull;
        }
    }

    // According to Swift documentation in XCode, this is the scaling factor of the content we capture.
    // This appears to change with "scalesToFit" but it fails to indicate backscaled content (undocumented behaviour).
    CFNumberRef contentScaleRef = (CFNumberRef) CFDictionaryGetValue(sampleDictionary, SCStreamFrameInfoContentScale);
    CGFloat contentScale = NAN;
    if (!CFNumberGetValue(contentScaleRef, kCFNumberCGFloatType, &contentScale)) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: received sample has no contentScale");
        return;
    }

    size_t numBytesTotal = IOSurfaceGetAllocSize(ioSurfaceRef);
    size_t numPlanes = IOSurfaceGetPlaneCount(ioSurfaceRef);
    OSType pixelFormat = IOSurfaceGetPixelFormat(ioSurfaceRef);
    size_t numBytesPerPixel = IOSurfaceGetBytesPerElement(ioSurfaceRef);
    size_t numBytesPerRow = IOSurfaceGetBytesPerRow(ioSurfaceRef);
    size_t elementWidth = IOSurfaceGetElementWidth(ioSurfaceRef);
    size_t elementHeight = IOSurfaceGetElementHeight(ioSurfaceRef);
    size_t bufferWidth = IOSurfaceGetWidth(ioSurfaceRef);
    size_t bufferHeight = IOSurfaceGetHeight(ioSurfaceRef);

    FBLOG_DEBUG(
        self->ext_instance_ref, "stream callback: received numBytesTotal=%ld, numPlanes=%ld, pixelFormat=%s, "
        "numBytesPerPixel=%ld, numBytesPerRow=%ld, elementWidth=%ld, elementHeight=%ld, "
        "bufferWidth=%ld, bufferHeight=%ld; "
        "scaleFactor=%f, contentScale=%f, contentRect=(%.1f x %.1f)@[%.1f, %.1f], "
        "ioSurfaceBaseAddr=%p",
        numBytesTotal, numPlanes, OSTYPE_STRING(pixelFormat),
        numBytesPerPixel, numBytesPerRow, elementWidth, elementHeight,
        bufferWidth, bufferHeight,
        scaleFactor, contentScale, contentRect.size.width, contentRect.size.height, contentRect.origin.x, contentRect.origin.y,
        ioSurfaceBaseAddr
    );

    // Check that we actually get 1:1 scaled content. If the display output is being over- and backscaled, the actual
    // scale is only apparent by multiplying scale factor and content scale (e.g. 2.0 * 0.5 = 1.0).
    double actualScale = scaleFactor * contentScale;
    if (IS_APPROXIMATELY(scaleFactor, 1.0, 0.005)) {
        // TODO: fail capture (unlikely to resolve on its own)
        FBLOG_WARN(
            self->ext_instance_ref,
            "stream callback: calculated actualScale=%.3f which is out of tolerance (expected 1.0); scaleFactor=%.1f, contentScale=%.1f",
            actualScale, scaleFactor, contentScale
        );
        return;
    }

    // On a backscaled display the content box we receive is scaled down, so we need to correct it using the display's
    // scale factor again.
    double content_height = contentRect.size.height * scaleFactor;
    double content_width = contentRect.size.width * scaleFactor;
    double content_offset_x = contentRect.origin.x * scaleFactor;
    double content_offset_y = contentRect.origin.y * scaleFactor;

    if (elementWidth != 1 || elementHeight != 1) {
        // TODO: fail capture (unlikely to resolve on its own)
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: invalid element dimensions, elementWidth=%ld, elementHeight=%ld",
            elementWidth, elementHeight
        );
        return;
    }

    if (numPlanes > 1) {
        // TODO: fail capture
        FBLOG_WARN(self->ext_instance_ref, "stream callback: unexpected numPlanes=%ld", numPlanes);
        return;
    }

    fbsource_sck_pixel_format_t callback_pixel_format = FBSOURCE_SCK_PIXEL_FORMAT_UNKNOWN;
    if (pixelFormat == kCVPixelFormatType_32BGRA) {
        callback_pixel_format = FBSOURCE_SCK_PIXEL_FORMAT_32BGRA;
    } else {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: unhandled pixel format %s (0x%08x)",
            OSTYPE_STRING(pixelFormat), pixelFormat
        );
        return;
    }

    if (numBytesPerPixel < 4) {
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: unexpected numBytesPerPixel=%ld for pixel format %s (0x%08x)",
            numBytesPerPixel, OSTYPE_STRING(pixelFormat), pixelFormat
        );
        return;
    }

    size_t minBytesPerRow = bufferWidth * numBytesPerPixel;
    if (numBytesPerRow < minBytesPerRow) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: insufficient buffer dimensions, got numBytesPerRow=%ld, expected at least %ld (bufferWidth=%ld, numBytesPerPixel=%ld)",
            numBytesPerRow, minBytesPerRow, bufferWidth, numBytesPerPixel
        );
        return;
    }

    size_t minBytesTotal = bufferHeight * numBytesPerRow;
    if (numBytesTotal < minBytesTotal) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: insufficient buffer dimensions, got numBytesTotal=%ld, expected at least %ld (bufferHeight=%ld, numBytesPerRow=%ld)",
            numBytesTotal, minBytesTotal, bufferHeight, numBytesPerRow
        );
        return;
    }

    if (content_width < 1.0-ABS_DIMENSION_ERROR_TOLERANCE || content_height < 1.0-ABS_DIMENSION_ERROR_TOLERANCE) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: undersize content dimensions [%.4f, %.4f], contentRect.size=[%.4f, %.4f], scaleFactor=%.1f",
            content_width, content_height, contentRect.size.width, contentRect.size.height, scaleFactor
        );
        return;
    }

    double widthError = content_width - round(content_width);
    double heightError = content_height - round(content_height);
    if (fabs(widthError) > ABS_DIMENSION_ERROR_TOLERANCE || fabs(heightError) > ABS_DIMENSION_ERROR_TOLERANCE) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref,
            "stream callback: content dimensions [%.4f, %.4f] have excessive error; contentRect.size=[%.4f, %.4f], "
            "scaleFactor=%.1f => errors %.4f, %.4f; requiring at most %.4f",
            content_width, content_height, contentRect.size.width, contentRect.size.height,
            scaleFactor, widthError, heightError, ABS_DIMENSION_ERROR_TOLERANCE
        );
        return;
    }

    double xError = content_offset_x - round(content_offset_x);
    double yError = content_offset_y - round(content_offset_y);
    if (fabs(xError) > ABS_DIMENSION_ERROR_TOLERANCE || fabs(yError) > ABS_DIMENSION_ERROR_TOLERANCE) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref,
            "stream callback: content origin [%.4f, %.4f] has excessive error; contentRect.origin=[%.4f, %.4f], "
            "scaleFactor=%.1f => errors %.4f, %.4f; requiring at most %.4f",
            content_offset_x, content_offset_y, contentRect.origin.x, contentRect.origin.y,
            scaleFactor, xError, yError, ABS_DIMENSION_ERROR_TOLERANCE
        );
        return;
    }

    if (content_offset_x < 0.0 || content_offset_y < 0.0) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref,
            "stream callback: negative content origin is not supported, got [%.4f, %.4f]; "
            "contentRect.origin=[%.4f, %.4f], scaleFactor=%.1f",
            content_offset_x, content_offset_y,
            contentRect.origin.x, contentRect.origin.y, scaleFactor
        );
        return;
    }

    long minWidth = lround(content_offset_x) + lround(content_width);
    long minHeight = lround(content_offset_y) + lround(content_height);
    if (bufferWidth < minWidth || bufferHeight < minHeight) {
        // TODO: fail capture
        FBLOG_WARN(
            self->ext_instance_ref, "stream callback: insufficient buffer dimensions, got "
            "minWidth=%ld (%.1f + %.1f | (%.1f + %.1f) * %.1f), bufferWidth=%ld, "
            "minHeight=%ld (%.1f + %.1f | (%.1f + %.1f) * %.1f), bufferHeight=%ld",
            minWidth, content_offset_x, content_width, contentRect.origin.x, contentRect.size.width, scaleFactor, bufferWidth,
            minHeight, content_offset_y, content_height, contentRect.origin.y, contentRect.size.height, scaleFactor, bufferHeight
        );
        return;
    }

    fbsource_sck_frame_t callback_frame = {
        .buffer = ioSurfaceBaseAddr,
        .buffer_size = numBytesTotal,
        .bytes_per_pixel = numBytesPerPixel,
        .bytes_per_row = numBytesPerRow,
        .content_offset_x = content_offset_x,
        .content_offset_y = content_offset_y,
        .content_width = content_width,
        .content_height = content_height,
        .content_scale = contentScale,
        .pixel_format = callback_pixel_format,
    };

    sck_frame_callback_f checked_callback = self->frame_callback;
    if (!checked_callback) {
        FBLOG_WARN(self->ext_instance_ref, "stream callback: downstream frame callback is NULL");
        return;
    }

    FBLOG_DEBUG(self->ext_instance_ref, "stream callback: calling downstream");
    checked_callback(self->frame_callback_ref, &callback_frame);
    FBLOG_DEBUG(self->ext_instance_ref, "stream callback: done");
}

-(void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    char *msg = [Utils createCString:[error localizedDescription]];
    FBLOG_WARN(self->ext_instance_ref, "stream stopped with error: %s", msg);
    if (msg) {
        free(msg);
    }

    // FIXME: signal and handle failure
}

-(void)streamDidBecomeActive:(SCStream*)stream {
    FBLOG_DEBUG(self->ext_instance_ref, "streamDidBecomeActive got called");
    // TODO: signal through state?
}

-(void)streamDidBecomeInactive:(SCStream*)stream {
    FBLOG_DEBUG(self->ext_instance_ref, "streamDidBecomeInactive got called");
    // TODO: signal through state?
}

-(void)outputVideoEffectDidStartForStream:(SCStream*)stream {
    // ignored, nothing to do
    FBLOG_DEBUG(self->ext_instance_ref, "outputVideoEffectDidStartForStream got called");
}

-(void)outputVideoEffectDidStopForStream:(SCStream*)stream {
    // ignored, nothing to do
    FBLOG_DEBUG(self->ext_instance_ref, "outputVideoEffectDidStopForStream got called");
}

-(void)outputVideoEffectDidFailForStream:(SCStream*)stream withError:(NSError*)error {
    // ignored, nothing to do
    FBLOG_DEBUG(self->ext_instance_ref, "outputVideoEffectDidFailForStream got called");
}

@end

fbsource_sck_capture_handler_t setup_capture(void *ext_instance_ref, window_parameters_t window_params, uint32_t timeout_millis, sck_frame_callback_f frame_callback, void *frame_callback_ref) {
    fbsource_sck_capture_handler_t handler = [CaptureHandler new];
    if (![handler setupFor:ext_instance_ref captureWindow:window_params withTimeout:timeout_millis withCallback:frame_callback withCallbackReference:frame_callback_ref]) {
        [handler release];
        return NULL;
    }

    return handler;
}

bool start_capture(fbsource_sck_capture_handler_t handler, uint32_t timeout_millis) {
    if (![handler startCaptureWithTimeout:timeout_millis]) {
        return false;
    }

    return true;
}

bool stop_capture(fbsource_sck_capture_handler_t *handler, uint32_t timeout_millis) {
    if (!handler) {
        return true;
    }

    fbsource_sck_capture_handler_t actual_handler = *handler;
    if (!actual_handler) {
        return true;
    }

    if (![actual_handler stopCaptureWithTimeout:timeout_millis]) {
        return false;
    }

    [actual_handler release];
    *handler = NULL;

    return true;
}