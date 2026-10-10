# Screen capture module for Windows using WinRT/DXGI

This [Frame Buffet](../../README.md) source module enables window-based screen capture on Windows using DirectX (WinRT/DXGI APIs).

## Availability

- **Module name:** `CaptureWinRTDXGI`
- **Operating systems:** Windows 10, Windows 11
- **Compiler:** Microsoft Visual Studio CLang with Windows SDK 10.0.26100.0 (as available via GitHub Actions); cross-compilation and MinGW are not supported
- **Artifacts:**
  - `fbsource_capture_winrt_dxgi.dll` is a dynamically loadable module
  - `fbsource_capture_winrt_dxgi.lib` contains import bindings for the DLL (not needed when loading via framework)
  - `fbsource_capture_winrt_dxgi_static.lib` can be imported to statically link the module into downstream projects

## Configuration

| Option                     | Type\*           | Default           | Description                                                                                                                   |
|----------------------------|------------------|-------------------|-------------------------------------------------------------------------------------------------------------------------------|
| `crop.left`                | Unsigned Integer | `0`               | extraction offset from left border of original window frame (reduces width)                                                   |
| `crop.right`               | Unsigned Integer | `0`               | extraction offset from right border of original window frame (reduces width)                                                  |
| `crop.top`                 | Unsigned Integer | `0`               | extraction offset from top border of original window frame (reduces height)                                                   |
| `crop.bottom`              | Unsigned Integer | `0`               | extraction offset from bottom border of original window frame (reduces height)                                                |
| `hide_cursor`              | Boolean          | `false`           | whether the mouse cursor should be hidden (`true`) or visible (`false`), may not have any effect                              |
| `hide_capture_border`      | Boolean          | `false`           | whether an indication border should be drawn around the captured window (`false`) or hidden (`true`), may not have any effect |
| `window.search.name.exact` | String           | *none, mandatory* | title of window to be captured; needs to match *exactly* (case- and whitespace-sensitive)                                     |

Note that Windows defaults to drawing an indicator (warning) border around captured windows. The mouse cursor is also captured by default. Although you can try to change that behaviour, it is up to Windows and your security policies whether those changes actually take effect or will be ignored.

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options.

## Buffers (output)

A single buffer (ID `0`) is available while screen capture is running, holding the last captured frame.

No buffers are available during reconfiguration or while waiting/searching for a matching window.

## License / Copyright

This module is released under MIT license, see the project's [main license file](../../LICENSE.md) for further information.

Windows-specific API access and core capture code has been adapted from Robert Mikhayelyan's projects [Win32CaptureSample](https://github.com/robmikh/Win32CaptureSample) and [robmikh.common](https://github.com/robmikh/robmikh.common), also released under MIT license, copyright year indicated 2019 but still maintained as of 2026. Many thanks to Robert! It would have been very hard (near impossible) to figure out how to capture on Windows without those reference projects.
