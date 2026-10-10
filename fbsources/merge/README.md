# Merge module

This [Frame Buffet](../../README.md) source module copies areas from multiple sources onto a single output frame at configurable coordinates, enabling simple composition and rearrangement.

## Availability

- **Module name:** `Merge`
- **platform-independent**
- **Compiler:** CLang, GCC, MinGW, AppleClang, Microsoft Visual Studio CLang

## Artifacts

|                             | Linux               | Windows                     | the macOS® operating system |
|-----------------------------|---------------------|-----------------------------|-----------------------------| 
| dynamically loadable module | `fbsource_merge.so` | `fbsource_merge.dll`        | `fbsource_merge.dylib`      |
| import bindings\*           | n/a                 | `fbsource_merge.lib`        | n/a                         |
| static library\*\*          | `fbsource_merge.a`  | `fbsource_merge_static.lib` | `fbsource_merge.a`          |

\*) Windows compilation requires import bindings to link applications to DLLs. However, only the DLL is required when loading the module via the framework (no .lib required).\
\*\*) used to statically link the module into downstream projects

## Configuration

### General Options / Output Image

| Option      | Type\*           | Default           | Description                                                                                              |
|-------------|------------------|-------------------|----------------------------------------------------------------------------------------------------------|
| `baseColor` | String Enum      | `black`           | color outside copied areas; available: `black` (RGB 0/0/0), `white` (255/255/255), `magenta` (255/0/255) |
| `flipY`     | Boolean          | `false`           | if set to `true`, the output image gets flipped vertically (upside down)                                 |
| `height`    | Unsigned Integer | *none, mandatory* | height of output image (in pixels)                                                                       |
| `numAreas`  | Unsigned Integer | *none, mandatory* | number of areas (individual sources) to merge from, at least `1`                                         |
| `width`     | Unsigned Integer | *none, mandatory* | width of output image (in pixels)                                                                        |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options.

### Areas

Areas are configured with a **`areas[i].` prefix** to the following options, where `i` is the area index starting from `0`; e.g. the option `source.name` for the second area would be named `areas[1].source.name`. At least `numAreas` need to be defined, area definitions beyond `numAreas - 1` are ignored. Areas are copied in order of definition. Same sources can be specified for multiple areas.

| Sub-Option        | Type\*           | Default           | Description                                                                         |
|-------------------|------------------|-------------------|-------------------------------------------------------------------------------------|
| `destination.x`   | Unsigned Integer | *none, mandatory* | offset from left border to paste copied area onto output (X coordinate, in pixels)  |
| `destination.y`   | Unsigned Integer | *none, mandatory* | offset from top border to paste copied area onto output (Y coordinate, in pixels)   |
| `source.bufferId` | Unsigned Integer | `0`               | ID of buffer to retrieve from source                                                |
| `source.height`   | Unsigned Integer | *whole source*    | height of area to copy (in pixels)                                                  |
| `source.name`     | String           | *none, mandatory* | name of source instance to retrieve frames from                                     |
| `source.width`    | Unsigned Integer | *whole source*    | width of area to copy (in pixels)                                                   |
| `source.x`        | Unsigned Integer | `0`               | offset from left border to copy source image from (X coordinate of mask, in pixels) |
| `source.y`        | Unsigned Integer | `0`               | offset from top border to copy source image from (Y coordinate of mask, in pixels)  |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options. 

## Buffers (output)

A single buffer (ID `0`) is available while properly configured.

## License / Copyright

This module is released under MIT license and has been developed from scratch for this project. The module is subject to the exact same license as the overall project, see the project's [main license file](../../LICENSE.md) for further information.
