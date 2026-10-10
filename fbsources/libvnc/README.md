# LibVNC module

This [Frame Buffet](../../README.md) source module connects to a VNC server to receive remotely generated frames.

## Availability

- **Module name:** `LibVNC`
- **platform-independent**
- **Compiler:** CLang, GCC, MinGW, AppleClang, Microsoft Visual Studio CLang

## Artifacts

|                                                   | Linux                       | Windows                      | the macOS® operating system    |
|---------------------------------------------------|-----------------------------|------------------------------|--------------------------------| 
| dynamically loadable module, dynamic dependencies | `fbsource_libvnc.so`        | n/a                          | n/a                            |
| dynamically loadable module, static dependencies  | `fbsource_libvnc_static.so` | `fbsource_libvnc_static.dll` | `fbsource_libvnc_static.dylib` |
| ... import bindings\*                             | n/a                         | `fbsource_libvnc_static.lib` | n/a                            |
| fully static library\*\*                          | `fbsource_libvnc.a`         | `fbsource_libvnc.lib`        | `fbsource_libvnc.a`            |

\*) Windows compilation requires import bindings to link applications to DLLs. However, only the DLL is required when loading the module via the framework (no .lib required).\
\*\*) used to statically link the module into downstream projects

## Configuration

| Option                  | Type\*           | Default           | Description                                   |
|-------------------------|------------------|-------------------|-----------------------------------------------|
| `connectTimeoutSeconds` | Unsigned Integer | `5`               | number of seconds for each connection attempt |
| `hostname`              | String           | *none, mandatory* | hostname or IP address to connect to          |
| `password`              | String           | *none, mandatory* | password to authenticate with                 |
| `port`                  | Unsigned Integer | `5900`            | port number to connect to                     |
| `readTimeoutSeconds`    | Unsigned Integer | `1`               | number of seconds to before reconnecting      |

\*) JSON encoding may require strings; documented are "inner types" as expected from string-encoded options.

## Buffers (output)

A single buffer (ID `0`) is available while configured.

## License / Copyright

[LibVNC](https://github.com/LibVNC/libvncserver) is an externally developed library, **released under GPL version 2 or later**.

Further transitive dependencies are [zlib](https://github.com/madler/zlib) (released under its own license), [lzo](https://www.oberhumer.com/opensource/lzo/) (**licensed GPL version 2 or later**), [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) (subject to multiple licenses) and [libjpeg](https://www.ijg.org/) (released under its own license).

Where permitted in source code, this module's original code is developed and shared under MIT license, following the [overall project's license](../../LICENSE.md). MIT licensed code can be sub-licensed and thus can be re-released under more restrictive licenses, hence implicitly adapting to e.g. GPL version 2 during build/link time, as needed by dependencies. Parts of the source code directly interfacing with LibVNC or transitive dependencies may be restricted to interface-bound licenses.

> [!caution]
> **When built, the most-restrictive license takes effect which is GPL version 2 or later**, complying with LibVNC's direct license and lzo. Inclusion of this module requires your work to be compliant to all dependency licenses, in particular GPL. You are highly advised to perform a full license compatibility check before spreading binary builds, carefully evaluating all effective transitive dependencies as well. When in doubt, you are recommended to omit this module from your project to relax & simplify license conditions.
