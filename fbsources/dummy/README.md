# Dummy test pattern module

This [Frame Buffet](../../README.md) source module generates simple animated color gradients. While its main use is to test the framework, general configuration and integration to downstream projects, it can also serve as a sample project for writing new modules.

## Availability

- **Module name:** `Dummy`
- **platform-independent**
- **Compiler:** CLang, GCC, MinGW, AppleClang, Microsoft Visual Studio CLang

## Artifacts

|                             | Linux               | Windows                     | the macOS® operating system |
|-----------------------------|---------------------|-----------------------------|-----------------------------| 
| dynamically loadable module | `fbsource_dummy.so` | `fbsource_dummy.dll`        | `fbsource_dummy.dylib`      |
| import bindings\*           | n/a                 | `fbsource_dummy.lib`        | n/a                         |
| static library\*\*          | `fbsource_dummy.a`  | `fbsource_dummy_static.lib` | `fbsource_dummy.a`          |

\*) Windows compilation requires import bindings to link applications to DLLs. However, only the DLL is required when loading the module via the framework (no .lib required).\
\*\*) used to statically link the module into downstream projects

## Configuration

| Option   | Type\*           | Default           | Description                             |
|----------|------------------|-------------------|-----------------------------------------|
| `height` | Unsigned Integer | *none, mandatory* | height of image to generate (in pixels) |
| `width`  | Unsigned Integer | *none, mandatory* | width of image to generate (in pixels)  |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options. 

## Buffers (output)

A single buffer (ID `0`) is available when the configuration has been accepted.

No buffers are available while the configuration is incomplete or invalid.

## License / Copyright

This module is released under MIT license and has been developed from scratch for this project. The module is subject to the exact same license as the overall project, see the project's [main license file](../../LICENSE.md) for further information.
