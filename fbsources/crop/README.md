# Crop module

This [Frame Buffet](../../README.md) source module retrieves frames from a single upstream module and crops them at the specified coordinates.

## Availability

- **Module name:** `Crop`
- **platform-independent**
- **Compiler:** CLang, GCC, MinGW, AppleClang, Microsoft Visual Studio CLang

## Artifacts

|                             | Linux              | Windows                    | the macOS® operating system |
|-----------------------------|--------------------|----------------------------|-----------------------------| 
| dynamically loadable module | `fbsource_crop.so` | `fbsource_crop.dll`        | `fbsource_crop.dylib`       |
| import bindings\*           | n/a                | `fbsource_crop.lib`        | n/a                         |
| static library\*\*          | `fbsource_crop.a`  | `fbsource_crop_static.lib` | `fbsource_crop.a`           |

\*) Windows compilation requires import bindings to link applications to DLLs. However, only the DLL is required when loading the module via the framework (no .lib required).\
\*\*) used to statically link the module into downstream projects

## Configuration

| Option            | Type\*           | Default           | Description                                                                       |
|-------------------|------------------|-------------------|-----------------------------------------------------------------------------------|
| `height`          | Unsigned Integer | *none, mandatory* | height of cropped area (in pixels)                                                |
| `source.bufferId` | Unsigned Integer | `0`               | ID of buffer to retrieve from source                                              |
| `source.name`     | String           | *none, mandatory* | name of source instance to retrieve frames from                                   |
| `width`           | Unsigned Integer | *none, mandatory* | width of cropped area (in pixels)                                                 |
| `x`               | Unsigned Integer | `0`               | offset from left border to start inclusion from (X coordinate of mask, in pixels) |
| `y`               | Unsigned Integer | `0`               | offset from top border to start inclusion from (Y coordinate of mask, in pixels)  |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options. 

## Buffers (output)

A single buffer (ID `0`) is available while cropped frames are available.

No buffers are available while the configuration is incomplete/invalid or the specified source does not exist or has insufficient dimensions.

## License / Copyright

This module is released under MIT license and has been developed from scratch for this project. The module is subject to the exact same license as the overall project, see the project's [main license file](../../LICENSE.md) for further information.
