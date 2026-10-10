# Screen capture module for X11/X.org

This [Frame Buffet](../../README.md) source module enables window-based screen capture via X11/X.org APIs on Linux and possibly other platforms.

X11 capture still works in compatible Wayland environments, however performance may be degraded/inconsistent and off-screen capture may be limited. Use a Wayland capture module instead, when available.

## Availability

- **Module name:** `CaptureX11`
- **Operating systems:** tested on Linux
- **Compiler:** CLang or GCC
- **Artifacts:**
  - `fbsource_capture_x11.so` is a dynamically loadable module including statically linked dependencies (XCB)
  - `fbsource_capture_x11.a` can be imported to statically link the module into downstream projects

## Configuration

| Option                     | Type\*  | Default               | Description                                                                                                                                                      |
|----------------------------|---------|-----------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `allowSHM`                 | Boolean | `true`                | whether shared memory is allowed to be used (`true`) if SHM extension is available, or not (`false`); recommended for performance                                |
| `allowBackBuffer`          | Boolean | `true`                | whether Composite backbuffer is allowed to be used (`true`) by redirecting the window, if available, or not (`false`); required for off-screen rendering/capture |
| `allowDamage`              | Boolean | `true`                | whether partial updates and notifications are allowed to be used (`true`) if Damage extension is available, or not (`false`)                                     |
| `allowSyncAlarms`          | Boolean | `true`                | whether X11 alarms should be used for periodic wakeup (`true`) if Sync extension is available, or not (`false`); recommended for performance                     |
| `display`                  | String  | *environment default* | name of X11 display to connect to                                                                                                                                |
| `window.search.name.exact` | String  | *none, mandatory*     | title of window to be captured; needs to match *exactly* (case- and whitespace-sensitive)                                                                        |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options. 

## Buffers (output)

A single buffer (ID `0`) is available while screen capture is running, holding the last captured frame.

No buffers are available during reconfiguration or while waiting/searching for a matching window.

## License / Copyright

This module is released under MIT license and has been developed from scratch for this project. Interfaced APIs/libraries are also released under MIT or compatible licenses. The module is subject to the exact same license as the overall project, see the project's [main license file](../../LICENSE.md) for further information.
