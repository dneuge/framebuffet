# Screen capture module for the macOS operating system using ScreenCaptureKit

This [Frame Buffet](../../README.md) source module enables window-based screen capture on the macOS operating system
version 15 and later.

## Availability

- **Module name:** `CaptureMacOSSCK`
- **Operating systems:** macOS 15 or later
- **Compiler:** XCode CLang
- **Artifacts:**
  - `fbsource_capture_macos_sck.dylib` is a dynamically loadable module
  - `fbsource_capture_macos_sck.a` can be imported to statically link the module into downstream projects

## Linking

The static module `fbsource_capture_macos_sck.a` requires integrating applications/libraries to link against the `ScreenCaptureKit` framework, example for CMake:

```cmake
SET_PROPERTY(TARGET yourArtifact APPEND_STRING PROPERTY LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -framework ScreenCaptureKit")
```

## Runtime Permissions

As soon as the module is configured **for the first time** within an application, the macOS operating system will raise a popup informing the user that explicit permission must be granted to unlock screen capturing. This module will be unable to list and find windows until confirmed. Note that the application is not the same as the process this module runs in. For example, using this module from an OS-independent tool on the Terminal application will make the operating system raise a permission request for Terminal, not the specific tool. When using this module for a standalone application (not as part of a library) you should ensure to integrate it into a proper application as seen by the operating system to prevent confusion and overly generic permission grants.

Note that the permission hint only appears once, until the permission record gets deleted (see troubleshooting section below). If previously ignored or disabled on the hosting application (which may not be the one actually requesting permission - see above), no popup will appear. In order to unlock screen capturing, the user has to dive into System Settings/General/Privacy & Security/Screen & System Audio Recording.

Another popup will appear as capture is actually being started, informing the user about the application bypassing capture source selection to directly choose a window.

Additional complications may arise on applications or executables that have not been blessed with a developer certificate.

## Runtime Troubleshooting

### Permission granted but still refused

Occasionally, applications may not be able to gain screen capture permissions although those permissions have already been granted.

Under System Settings/General/Privacy & Security/Screen & System Audio Recording:

1. confirm that the application (bundle, not process) is still authorized (listed and "checked"/"on")
2. restart the application
3. if not solved, toggle the permission and restart the application again
4. if still not solved, quit the application, remove its permission by selecting it in the list and clicking the "minus" button below
   * on next application restart/permission request the initial authorization popup should appear again
5. if that doesn't work either, try to reboot (possibly removing the permission again before restart)

### Capture started but not receiving frames

If permissions have been granted, macOS will notify the module that capture has been started. Occasionally, it may happen that no frames arrive despite the whole application-side chain (window selection, setup, start) completed without errors and the application having been authorized and restarted several times.

To resolve that issue:

1. double-check that the permission is still granted (see section above)
2. reboot the system

## Configuration

| Option                     | Type\*  | Default               | Description                                                                                                                                                      |
|----------------------------|---------|-----------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `window.search.name.exact` | String  | *none, mandatory*     | title of window to be captured; needs to match *exactly* (case- and whitespace-sensitive)                                                                        |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options. 

## Buffers (output)

A single buffer (ID `0`) is available while screen capture is running, holding the last captured frame.

No buffers are available during reconfiguration or while waiting/searching for a matching window.

## License / Copyright

This module is released under MIT license. The module is subject to the exact same license as the overall project, see
the project's [main license file](../../LICENSE.md) for further information.

OS-specific handling would not have been possible to implement without close reference to official documentation and the
ScreenCaptureKit example project provided by Apple as part of the XCode development suite. The reference project was
kindly provided under MIT license as well:

> Copyright © 2024 Apple Inc.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
