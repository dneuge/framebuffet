#ifndef FBSOURCE_CAPTURE_WINRT_DXGI_HELPER_H
#define FBSOURCE_CAPTURE_WINRT_DXGI_HELPER_H

/**
 * Wrapper between C and C++ to (hopefully) ease the pain accessing WinRT/DXGI interfaces.
 *
 * Useful resources:
 * https://github.com/robmikh/Win32CaptureSample (licensed MIT)
 * https://github.com/robmikh/robmikh.common (licensed MIT)
 */

#include <stdbool.h>
#include <stdint.h>

#ifndef TARGET_WINDOWS
#include "foreign_api.h"
#else
#include <Unknwn.h>
//#include <inspectable.h>
//#include <windows.h>
//#include <wil/cppwinrt.h>
#endif

#ifdef __cplusplus
#include <winrt/Windows.Foundation.h>
//#include <winrt/Windows.Foundation.Collections.h>
//#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>
//#include <winrt/windows.graphics.capture.h>
//#include <winrt/Windows.System.h>

// NOTE: these were originally only relevant for the implementation but apparently needed to get the correct definition
//       of IInspectable despite having been defined twice already
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

typedef winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool win_capture_frame_pool_t;
#else
typedef void* win_capture_frame_pool_t;
#endif

/**
 * Called when the frame pool received a FrameArrived event.
 *
 * @param ref reference pointer as provided during callback registration
 * @param frame_pool DirectX frame pool which raised the event
 */
typedef void (*on_frame_arrived_callback_f)(void *ref, win_capture_frame_pool_t frame_pool);

#ifdef __cplusplus
class FBSourceWinRTDXGIDirect3DDevice {
public:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{nullptr};

    // wild speculation: COM pointers have reference counts but the winrt::com_ptr does not have any explicit methods
    // to increment reference count so we probably need to keep that com_ptr around until we stop using its resource,
    // at least it shouldn't hurt to do that...
    winrt::com_ptr<IInspectable> _backing_reference;
};
typedef FBSourceWinRTDXGIDirect3DDevice win_d3d_device_t;

class FBSourceWinRTDXGICaptureItem {
public:
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {0,};
};
typedef FBSourceWinRTDXGICaptureItem win_graphics_capture_item_t;

class FBSourceWinRTDXGICaptureSession {
public:
    FBSourceWinRTDXGICaptureSession(on_frame_arrived_callback_f bound_callback, void *bound_callback_ref);
    ~FBSourceWinRTDXGICaptureSession();

    void on_frame_arrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &frame_pool, winrt::Windows::Foundation::IInspectable const &inspectable);
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession win_capture_session{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool win_frame_pool{nullptr};

    win_graphics_capture_item_t *capture_item = nullptr;
private:
    on_frame_arrived_callback_f bound_callback = nullptr;
    void *bound_callback_ref = nullptr;
};
typedef FBSourceWinRTDXGICaptureSession fbsource_capture_session_t;
#else
typedef void* win_d3d_device_t;
typedef void* win_graphics_capture_item_t;
typedef void* fbsource_capture_session_t;
#endif

#define WINRT_DXGI_CAPTURE_FLAG_HIDE_CURSOR (1 << 0)
#define WINRT_DXGI_CAPTURE_FLAG_HIDE_BORDER (1 << 1)
typedef uint8_t winrt_dxgi_capture_flags_t;

#include "capture_winrt_dxgi.h"

typedef struct {
    size_t bytes_per_pixel;
    uint8_t color_bitdepth;
    size_t num_bytes_per_row;

    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;

    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;

    uint16_t width;
    uint16_t height;
} winrt_dxgi_frame_info_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a wrapper holding a GraphicsCaptureItem for a window handle.
 *
 * @param ext_instance_ref Frame Buffet source instance for logging
 * @param window_handle window handle to create capture item for
 * @param capture_item will be set to the capture item if successful
 * @return true if successful, false on error
 */
bool create_window_capture_item(void *ext_instance_ref, HWND window_handle, win_graphics_capture_item_t **capture_item);

void destroy_capture_item(void *ext_instance_ref, win_graphics_capture_item_t *capture_item);

/**
 * Creates a wrapper holding a Direct3D/DXGI device reference suitable for capturing.
 *
 * @param ext_instance_ref Frame Buffet source instance for logging
 * @param device will be set to the Direct3D device if successful
 * @return true if successful, false on error
 */
bool create_d3d_device(void *ext_instance_ref, win_d3d_device_t **device);

void destroy_d3d_device(void *ext_instance_ref, win_d3d_device_t *device);

/**
 * Starts a DXGI capture session for the specified capture item.
 *
 * If successful (and only then), the capture session takes ownership
 * of the capture item. The item must not be used again as it will be
 * released when capture stops.
 *
 * If unsuccessful, the capture item remains managed by the caller.
 *
 * @param ext_instance_ref Frame Buffet source instance for logging
 * @param session will be set to the capture session; only valid on success
 * @param device Direct3D device to use
 * @param capture_item description of what should be captured; ownership transferred on success
 * @param flags see WINRT_DXGI_CAPTURE_FLAG_* defines
 * @param on_frame_arrived callback to handle captured frames
 * @param on_frame_arrived_ref reference to be passed to the callback
 * @return true if successful, false on error
 */
bool start_capture_session(void *ext_instance_ref, fbsource_capture_session_t **session, win_d3d_device_t *device, win_graphics_capture_item_t *capture_item, winrt_dxgi_capture_flags_t flags, on_frame_arrived_callback_f on_frame_arrived, void *on_frame_arrived_ref);

/**
 * Stops and destroys the given capture session.
 *
 * @param ext_instance_ref Frame Buffet source instance for logging
 * @param session session to stop and destroy
 */
void stop_capture_session(void *ext_instance_ref, fbsource_capture_session_t *session);

/**
 * Copies the next frame from given frame pool to the specified buffer.
 *
 * @param ext_instance_ref Frame Buffet source instance for logging
 * @param frame_pool DXGI frame pool to copy from
 * @param buffer buffer to copy raw image data to
 * @param buffer_size buffer size
 * @param frame_info will be updated with information about buffer interpretation; invalid on error
 * @return true on success, false on error
 */
bool copy_dxgi_frame(void *ext_instance_ref, win_capture_frame_pool_t frame_pool, unsigned char *buffer, size_t buffer_size, winrt_dxgi_frame_info_t *frame_info);

/**
 * Estimates the size of internal image buffer needed to transport raw frames.
 * Due to uncertainties in capture handling, the buffer will be indicated somewhat
 * larger than it should actually be necessary.
 *
 * @param window_dimensions dimensions of window to be captured
 * @return estimated size of required image buffer
 */
size_t estimate_dxgi_buffer_size(window_dimensions_t window_dimensions);
#ifdef __cplusplus
}
#endif

#endif //FBSOURCE_CAPTURE_WINRT_DXGI_HELPER_H
