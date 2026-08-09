#include "winrt_dxgi.h"

//#include <d3d11_4.h>
//#include <dxgi1_2.h>

//#include <windows.graphics.capture.interop.h>
//#include <windows.graphics.directx.direct3d11.interop.h>

//#include <wil/cppwinrt_helpers.h>

#include <stdlib.h>
#include <string.h>

#include "../../../framework/src/framebuffet.h"

#define FBLOG_CPP_TRACE(ext_instance_ref, msg, ...) FBLOG_TRACE(ext_instance_ref, (char*) (const char*) msg __VA_OPT__(,) __VA_ARGS__)
#define FBLOG_CPP_DEBUG(ext_instance_ref, msg, ...) FBLOG_DEBUG(ext_instance_ref, (char*) (const char*) msg __VA_OPT__(,) __VA_ARGS__)
#define FBLOG_CPP_INFO(ext_instance_ref, msg, ...) FBLOG_INFO(ext_instance_ref, (char*) (const char*) msg __VA_OPT__(,) __VA_ARGS__)
#define FBLOG_CPP_WARN(ext_instance_ref, msg, ...) FBLOG_WARN(ext_instance_ref, (char*) (const char*) msg __VA_OPT__(,) __VA_ARGS__)

#define WINDOWS_BYTES_PER_PIXEL 4

// FIXME: Do not hardcode - "overscan" is a drop shadow effect that may vary by OS version or HW install (Win 10 has 14,7; Win 11 has 0,0).
//        Documentation and online posts indicate GetWindowRect may provide outer bounds (needed for capturing). Effect with DPI scaling is
//        unknown ("virtualized" for GetWindowRect with capturing probably seeing it, window size not?) and needs to be tested.
//        To avoid frustration if we get anything totally wrong (which we will) users should be given some config options to manually adjust
//        offsets/overscan or scaling.
//        It may also be a good idea to detect window size when setting up the capture session (i.e. not just abstracting/wrapping the
//        "plain" Windows API here but already enhancing/securing the implementation).
//        https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winuser/nf-winuser-getwindowrect.md
// NOTE: size reported by DWMWA_EXTENDED_FRAME_BOUNDS seems correct but actual capture area has additional 14px blank pixels in width, 7px in height
#define OVERSCAN_WIDTH 14
#define OVERSCAN_HEIGHT 7
//#define OVERSCAN_WIDTH 0
//#define OVERSCAN_HEIGHT 0

bool create_window_capture_item(void *ext_instance_ref, HWND window_handle, win_graphics_capture_item_t **capture_item) {
    // closely based on:
    // https://github.com/robmikh/robmikh.common/blob/main/robmikh.common/include/robmikh.common/capture.desktop.interop.h

    FBLOG_TRACE(ext_instance_ref, "create_window_capture_item initializing item");
    *capture_item = new FBSourceWinRTDXGICaptureItem();
    //winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {0,};
    try {
        FBLOG_TRACE(ext_instance_ref, "create_window_capture_item calling API");
        HRESULT res = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>()
            ->CreateForWindow(
                window_handle,
                winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                winrt::put_abi((*capture_item)->item)
            );

        if (res != S_OK) {
            FBLOG_CPP_WARN(ext_instance_ref, "create_window_capture_item failed with NOK HRESULT");
            return false;
        }
    } catch (winrt::hresult_error const& error) {
        FBLOG_CPP_WARN(ext_instance_ref, "create_window_capture_item failed with WinRT HRESULT exception");
        return false;
    }

    //FBLOG_TRACE(ext_instance_ref, "create_window_capture_item setting output");
    //*capture_item = &item;
    FBLOG_TRACE(ext_instance_ref, "create_window_capture_item done");
    return true;
}

void destroy_capture_item(void *ext_instance_ref, win_graphics_capture_item_t *item) {
    if (!item) {
        return;
    }

    FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_capture_item setting nullptr");
    item->item = nullptr; // cargo cult: this seems to be how the reference project "releases" the resource (might be overloaded operator or not relevant at all)

    FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_capture_item deleting item");
    delete item;

    FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_capture_item done");
}

bool create_d3d_device(void *ext_instance_ref, win_d3d_device_t **device) {
    // closely based on:
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/App.cpp
    // https://github.com/robmikh/robmikh.common/blob/main/robmikh.common/include/robmikh.common/d3d11Helpers.h
    // https://github.com/robmikh/robmikh.common/blob/main/robmikh.common/include/robmikh.common/direct3d11.interop.h

    if (!device) {
        FBLOG_CPP_WARN(ext_instance_ref, "create_d3d_device called without device reference");
        return false;
    }

    // TODO: it's weird that we seem to create a D3D device, cast it and create it (or another one?) again from that cast - is that really necessary?
    //       (current implementation is cargo cult which could be necessary but also could just be neglected overhead on the reference project)

    // expansion of: auto d3dDevice = util::CreateD3D11Device();
    // according to: https://github.com/robmikh/robmikh.common/blob/13369170cc2ef33ed1a178e24188677a71e33c46/robmikh.common/include/robmikh.common/d3d11Helpers.h
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    winrt::com_ptr<ID3D11Device> d3d_device;
    HRESULT res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, d3d_device.put(), nullptr, nullptr);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        FBLOG_CPP_INFO(ext_instance_ref, "Direct3D hardware mode is unavailable, trying to fall back to \"warp\"");
        res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, d3d_device.put(), nullptr, nullptr);
    }
    if (res != S_OK) {
        FBLOG_CPP_WARN(ext_instance_ref, "create_d3d_device failed with WinRT NOK HRESULT from D3D11CreateDevice");
        return false;
    }

    // expansion of: auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    // needs to be unwrapped from com_ptr, see https://github.com/MicrosoftDocs/winrt-related/blob/e0fd67bc241608a30202187c61417f1dee896a41/winrt-related-src/cpp-ref-for-winrt/com-ptr.md
    IDXGIDevice* dxgi_device = d3d_device.as<IDXGIDevice>().get();

    // expansion of: m_device = CreateDirect3DDevice(dxgiDevice.get());
    // according to: https://github.com/robmikh/robmikh.common/blob/13369170cc2ef33ed1a178e24188677a71e33c46/robmikh.common/include/robmikh.common/direct3d11.interop.h
    *device = new FBSourceWinRTDXGIDirect3DDevice();
    res = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device, (*device)->_backing_reference.put());
    if (res != S_OK) {
        FBLOG_CPP_WARN(ext_instance_ref, "create_d3d_device failed with WinRT NOK HRESULT from CreateDirect3D11DeviceFromDXGIDevice");
        return false;
    }

    (*device)->device = (*device)->_backing_reference.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    return true;
}

void destroy_d3d_device(void *ext_instance_ref, win_d3d_device_t *device) {
    if (!device) {
        return;
    }

    FBLOG_CPP_TRACE(ext_instance_ref, "destroy_d3d_device");

    // IDirect3DDevice seems to implement IClosable, implying that we should close it before deletion unless that's
    // "automated" in C++? Try manually, just to be sure.
    // https://github.com/MicrosoftDocs/winrt-api/blob/24ea4776b8ec0ff87588785ee4a99bb8c3604fbc/windows.graphics.directx.direct3d11/idirect3ddevice.md
    if (device->device != nullptr) {
        FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_d3d_device closing IDirect3DDevice");
        device->device.Close();
    }

    // ... and now we assume that C++ will call destructors if Microsoft defined any. This includes the com_ptr
    // reference count we can only speculate about should probably exist, fingers crossed.
    FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_d3d_device deleting wrapper");
    delete device;

    FBLOG_CPP_DEBUG(ext_instance_ref, "destroy_d3d_device done");
}

bool copy_dxgi_frame(void *ext_instance_ref, win_capture_frame_pool_t frame_pool, unsigned char *buffer, size_t buffer_size, winrt_dxgi_frame_info_t *frame_info) {
    // closely based on:
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/CaptureSnapshot.cpp
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/App.cpp (TakeSnapshotAsync)

    if (!(buffer && frame_info)) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame missing required parameters: buffer=%p, frame_info=%p", buffer, frame_info);
        return false;
    }

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = frame_pool.TryGetNextFrame();

    // expansion of: auto texture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    // according to: https://github.com/robmikh/robmikh.common/blob/13369170cc2ef33ed1a178e24188677a71e33c46/robmikh.common/include/robmikh.common/direct3d11.interop.h
    winrt::com_ptr<ID3D11Texture2D> texture;
    HRESULT res = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()
        ->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (res != S_OK) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame failed to get texture interface from DXGI-cast frame");
        return false;
    }

    // get the D3D device a bit earlier so we can save an argument
    winrt::com_ptr<ID3D11Device> device;
    texture->GetDevice(device.put());

    // expansion of: auto result = util::CopyD3DTexture(d3dDevice, texture, true);
    //               (original result is named textureCopy in expanded code)
    // according to: https://github.com/robmikh/robmikh.common/blob/main/robmikh.common/include/robmikh.common/d3d11Helpers.h
    winrt::com_ptr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.put());

    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);

    // === Frame Buffet custom copy code follows ===
    // for D3D11_TEXTURE2D_DESC see:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/d3d11/ns-d3d11-d3d11_texture2d_desc.md
    frame_info->width = desc.Width - OVERSCAN_WIDTH;
    frame_info->height = desc.Height - OVERSCAN_HEIGHT;
    // TODO: verify image format (assuming BGRA8)
    frame_info->bytes_per_pixel = 4;
    frame_info->color_bitdepth = 8;
    // NOTE: format name and mask memory layout differ in endianness, BGRA is actually ARGB
    frame_info->red_mask    = 0x00FF0000;
    frame_info->green_mask  = 0x0000FF00;
    frame_info->blue_mask   = 0x000000FF;
    frame_info->red_shift   = 16;
    frame_info->green_shift = 8;
    frame_info->blue_shift  = 0;
    // === end of Frame Buffet custom code ===
    // continuing:   util::CopyD3DTexture

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> textureCopy;
    res = device->CreateTexture2D(&desc, nullptr, textureCopy.put());
    if (res != S_OK) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame failed to allocate texture for copy");
        return false;
    }
    context->CopyResource(textureCopy.get(), texture.get());

    // expansion of: auto bytes = util::CopyBytesFromTexture(texture); (called from App::TakeSnapshotAsync after we got the texture)
    // according to: https://github.com/robmikh/robmikh.common/blob/13369170cc2ef33ed1a178e24188677a71e33c46/robmikh.common/include/robmikh.common/d3d11Helpers.h
    uint32_t subresource = 0;
    //winrt::com_ptr<ID3D11Device> device;
    //textureCopy->GetDevice(device.put());
    //winrt::com_ptr<ID3D11DeviceContext> context;
    //device->GetImmediateContext(context.put());

    /*
    // safe guard from: auto stagingTexture = PrepareStagingTexture(device, textureCopy);
    // TODO: do we actually need this?
    //D3D11_TEXTURE2D_DESC desc = {};
    //textureCopy->GetDesc(&desc);
    if (desc.Usage != D3D11_USAGE_STAGING || (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) == 0) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame failed because copy texture is not a staging/CPU texture");
        return false;
    }
    // continuing:   util::CopyBytesFromTexture
    */
    // stagingTexture is now the same as textureCopy
    // bytesPerPixel is hard-coded WINDOWS_BYTES_PER_PIXEL

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    res = context->Map(textureCopy.get(), subresource, D3D11_MAP_READ, 0, &mapped);
    if (res != S_OK) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame failed because copy texture is not a staging/CPU texture");
        return false;
    }

    // === Frame Buffet custom copy code follows ===
    // for D3D11_MAPPED_SUBRESOURCE see:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/d3d11/ns-d3d11-d3d11_mapped_subresource.md
    frame_info->num_bytes_per_row = mapped.RowPitch;
    long long image_size = mapped.RowPitch * (desc.Height - OVERSCAN_HEIGHT);
    bool success = false;
    if (image_size < 0) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame calculated negative image size, RowPitch=%d, Height=%d, OVERSCAN_HEIGHT=%d", mapped.RowPitch, desc.Height, OVERSCAN_HEIGHT);
    } else if (image_size > buffer_size) {
        FBLOG_CPP_WARN(ext_instance_ref, "copy_dxgi_frame got buffer of insufficient size; needed %z, got %z", image_size, buffer_size);
    } else {
        memcpy(buffer, mapped.pData, image_size);
        success = true;
    }
    // === end of Frame Buffet custom code ===
    // continuing:   util::CopyBytesFromTexture

    context->Unmap(textureCopy.get(), 0);

    return success;
}

FBSourceWinRTDXGICaptureSession::FBSourceWinRTDXGICaptureSession(on_frame_arrived_callback_f bound_callback, void *bound_callback_ref) {
    this->bound_callback = bound_callback;
    this->bound_callback_ref = bound_callback_ref;
}

FBSourceWinRTDXGICaptureSession::~FBSourceWinRTDXGICaptureSession() {
    this->bound_callback = nullptr;
    this->bound_callback_ref = nullptr;

    // closely based on:
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/SimpleCapture.cpp (Close)
    // NOTE: this seems unnecessary but may be needed due to invisible operator overloading or similar; keep as-is
    this->win_capture_session = nullptr;
    this->win_frame_pool = nullptr;
}

void FBSourceWinRTDXGICaptureSession::on_frame_arrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &frame_pool, winrt::Windows::Foundation::IInspectable const &inspectable) {
    if (!this->bound_callback) {
        return;
    }

    this->bound_callback(this->bound_callback_ref, frame_pool);
}

bool start_capture_session(void *ext_instance_ref, fbsource_capture_session_t **session, win_d3d_device_t *device, win_graphics_capture_item_t *capture_item, winrt_dxgi_capture_flags_t flags, on_frame_arrived_callback_f on_frame_arrived, void *on_frame_arrived_ref) {
    // closely based on:
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/SimpleCapture.cpp (constructor and StartCapture)

    if (!session || !device || !capture_item || !on_frame_arrived) {
        FBLOG_CPP_WARN(
            ext_instance_ref,
            "start_capture_session missing mandatory parameters: session=%p, device=%p, capture_item=%p, on_frame_arrived=%p",
            session, device, capture_item, on_frame_arrived
        );
        return false;
    }

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session instantiating wrapper");
    *session = new FBSourceWinRTDXGICaptureSession(on_frame_arrived, on_frame_arrived_ref);

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session calling CreateFreeThreaded");
    (*session)->win_frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        device->device,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2, // numberOfBuffers
        capture_item->item.Size() // memory size of each buffer
    );

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session calling CreateCaptureSession");
    (*session)->win_capture_session = (*session)->win_frame_pool.CreateCaptureSession(capture_item->item);

    // refer to https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/SimpleCapture.h for flags
    bool require_border = (flags & WINRT_DXGI_CAPTURE_FLAG_HIDE_BORDER) == 0;
    FBLOG_CPP_DEBUG(ext_instance_ref, "start_capture_session conditionally calling IsBorderRequired(%d)", require_border);
    try {
        if ((*session)->win_capture_session.IsBorderRequired() != require_border) {
            (*session)->win_capture_session.IsBorderRequired(require_border);
        }
    } catch (winrt::hresult_error const& error) {
        FBLOG_CPP_WARN(ext_instance_ref, "start_capture_session failed to set IsBorderRequired(%d) - this setting may be forbidden to interact with by security policies on your system", require_border);
    }

    bool enable_cursor_capture = (flags & WINRT_DXGI_CAPTURE_FLAG_HIDE_CURSOR) == 0;
    FBLOG_CPP_DEBUG(ext_instance_ref, "start_capture_session conditionally calling IsCursorCaptureEnabled(%d)", enable_cursor_capture);
    try {
        if ((*session)->win_capture_session.IsCursorCaptureEnabled() != enable_cursor_capture) {
            (*session)->win_capture_session.IsCursorCaptureEnabled(enable_cursor_capture);
        }
    } catch (winrt::hresult_error const& error) {
        FBLOG_CPP_WARN(ext_instance_ref, "start_capture_session failed to set IsCursorCaptureEnabled(%d) - this setting may be forbidden to interact with by security policies on your system", enable_cursor_capture);
    }

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session calling FrameArrived");
    (*session)->win_frame_pool.FrameArrived({ *session, &FBSourceWinRTDXGICaptureSession::on_frame_arrived });

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session calling StartCapture");
    (*session)->win_capture_session.StartCapture();

    FBLOG_CPP_TRACE(ext_instance_ref, "start_capture_session done");
    return true;
}

void stop_capture_session(void *ext_instance_ref, fbsource_capture_session_t *session) {
    if (!session) {
        return;
    }

    // closely based on:
    // https://github.com/robmikh/Win32CaptureSample/blob/main/Win32CaptureSample/SimpleCapture.cpp (Close)
    FBLOG_CPP_DEBUG(ext_instance_ref, "stop_capture_session closing winrt capture session");
    session->win_capture_session.Close();
    FBLOG_CPP_DEBUG(ext_instance_ref, "stop_capture_session closing winrt frame pool");
    session->win_frame_pool.Close();

    destroy_capture_item(ext_instance_ref, session->capture_item);
    session->capture_item = nullptr;

    FBLOG_CPP_DEBUG(ext_instance_ref, "stop_capture_session deleting session");
    delete session;

    FBLOG_CPP_DEBUG(ext_instance_ref, "stop_capture_session done");
}

size_t estimate_dxgi_buffer_size(window_dimensions_t window_dimensions) {
    size_t minimum_size = (window_dimensions.height + OVERSCAN_HEIGHT) * ((window_dimensions.width + OVERSCAN_WIDTH) * WINDOWS_BYTES_PER_PIXEL);
    return minimum_size + (minimum_size * 5 / 100);
}
