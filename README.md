# Frame Buffet - all you can treat!

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
[![Issue Tracking: Codeberg](https://img.shields.io/badge/issue%20tracking-codeberg-2684cf)](https://codeberg.org/dneuge/framebuffet/issues)
[![AI Policy: restrictive](https://img.shields.io/badge/AI_policy-restrictive-yellow)](AI_USAGE_POLICY.md)

<img src="docs/logo/frontpage.svg" alt="frontpage logo" width="100%" style="max-width:800px;display:block;margin-left:auto;margin-right:auto;" />

Frame Buffet is a library implementing an externally configurable modular 2D scene graph for frame buffer composition. The project's goals are:

* a light-weight and easy-to-use framework, written in C to be as universally reusable as possible
* image sources/generation as well as processing should be fully modular for easy customization/extension
  * all modules should be optional
* use as little runtime dependencies as possible (unless required by a specific source module)
* suitable both for dynamic and static linking
* the library should be as platform-independent as possible, supporting all major operating systems

Official repositories are hosted on [Codeberg](https://codeberg.org/dneuge/framebuffet) and [GitHub](https://github.com/dneuge/framebuffet). Both locations are kept in sync and can be used to submit pull requests but issues are only tracked on [Codeberg](https://codeberg.org/dneuge/framebuffet/issues) to gather them in a single place. Please note that [all contributions](CONTRIBUTING.md) incl. issue reports are subject to a restrictive [AI Usage Policy](AI_USAGE_POLICY.md).

## Current state

The framework is in early development. The API is *not stable* yet and may change at any time without notice.

### Included modules

The following modules are included with the current version of Frame Buffet:

* platform-independent:
  * [`crop`](fbsources/crop) extracts a rectangular part from another source
  * [`dummy`](fbsources/dummy) generates a simple animated color pattern for debugging, also useful as a reference when writing custom modules
  * [`libvnc`](fbsources/libvnc) uses [LibVNC](https://github.com/LibVNC/libvncserver) to connect to a VNC server (note that LibVNC is [licensed GPL 2](https://github.com/LibVNC/libvncserver/blob/master/COPYING))
  * [`merge`](fbsources/merge) combines rectangular parts from other sources
* platform-dependent:
  * [`capture_macos_sck`](fbsources/capture_macos_sck) implements screen capture on the macOS operating system via ScreenCaptureKit
  * [`capture_winrt_dxgi`](fbsources/capture_winrt_dxgi) implements screen capture on Windows via DirectX (WinRT/DXGI)
  * [`capture_x11`](fbsources/capture_x11) implements screen capture on Linux for X11

An additional screen capture module for the macOS® operating system is expected to be added H2/2026. A screen capture module for Wayland will eventually become necessary as well but has lower priority as [the X11 module](fbsources/capture_x11) can be used (with some reliability issues) in the mean-time.

### Framework integration

You have multiple options to integrate and use Frame Buffet in your own application. The following artifacts are available from `framework/release/lib` upon compilation:

|                                                   | Linux                      | Windows (MSVC, recommended)  | Windows (MinGW, not recommended)  | the macOS operating system        |
|---------------------------------------------------|----------------------------|------------------------------|-----------------------------------|-----------------------------------|
| dynamically loadable module, dynamic dependencies | `libframebuffet.so`        | `framebuffet.dll`            | `framebuffet.dll`                 | `libframebuffet.dylib`            |
| ... import bindings\*                             | n/a                        | `framebuffet.lib`            | `libframebuffet.dll.a`            | n/a                               |
| dynamically loadable module, static dependencies  | `libframebuffet_static.so` | `framebuffet_static_lib.dll` | `framebuffet_static_lib.dll`      | `libframebuffet_static_lib.dylib` |
| ... import bindings\*                             | n/a                        | `framebuffet_static_lib.lib` | `libframebuffet_static_lib.dll.a` | n/a                               |
| static library\*\*                                | `libframebuffet.a`         | `framebuffet_static.lib`     | `libframebuffet.a`                | `libframebuffet.a`                |
| source module DLL shim\*\*\*                      | n/a                        | `framebuffet_dll_shim.lib`   | `libframebuffet_dll_shim.a`       | n/a                               |

\*) Windows compilation requires import bindings to link applications to DLLs. However, only the DLL is required when loading the module via the framework (no .lib required).\
\*\*) used to statically link the module into downstream projects\
\*\*\*) needed when building a dynamically loadable source module on Windows (see below)

Note that there are two variants of **dynamically loadable modules**:

- using dynamic dependencies requires additional modules from `lib/_build` to be shipped alongside Frame Buffet
- the static dependency artifact already includes all framework dependencies that are not commonly provided by the operating system (framework can be loaded without extra artifacts from `lib/_build`)

The **static library** is intended to statically (inseparably) integrate the framework to its host. Any framework dependencies need to be linked as well (probably statically), as available from `lib/_build`.

When developing a **source module**, Windows compilation of dynamically loadable modules requires static linking of the **DLL shim**. The shim allows the framework to inject references to its functions to modules to make them callable from the source module, essentially reverse-linking upstream framework functions from downstream. This is not necessary on \*NIX operating systems as all upstream functions are automatically available to downstream modules.

#### Dependencies

In addition to a C standard library, Frame Buffet's [framework](framework) depends on the following libraries whose artifacts from `lib/_build` must be provided/linked unless the "static" dynamically loadable module is being used: (see [`fetch-libs.sh`](fetch-libs.sh) for versions/download sources)

- [cJSON](https://github.com/DaveGamble/cJSON) - note that the static Windows artifact compiled by MSVC is `cJSON.obj`, not `cjson.lib` (which is the import binding for `cjson.dll`)
- a patched extract of [Mesa's C11 adapter](https://gitlab.freedesktop.org/mesa/mesa/-/tree/c36706142be9828271183104db0cbcd243b9ded1/src/c11) compiled to `libmesa_c11`, preferably statically linked - only required if compiled for Windows without native C11 threads support (e.g. MinGW, not needed if compiled using MSVC)
- POSIX threads need to be linked on Linux if available (to supplement C11 threads) and are *strictly required* when targeting the macOS operating system due to missing C11 threads support on that platform

C11 thread workarounds require may require define `NEED_C11_THREADS_WRAPPER` to be set for activation, in addition to linking the required libraries. Example for CMake:

```cmake
# check if C11 compatibility wrappers are needed for threads.h and subsequently time.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(NOT HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DNEED_C11_THREADS_WRAPPER)
    IF(WIN32)
        SET(LINK_C11_COMPAT_STATIC "${LIB_BUILD}/libmesa_c11${CMAKE_STATIC_LIBRARY_SUFFIX}")
    ENDIF()
ENDIF()
```

Note that source modules may have additional dependencies, as documented in their individual README files.

#### Statically linking source modules

While source modules can be kept separate and loaded dynamically through `framebuffet_load_module`, they can also be directly integrated to a host.

Each source module should use the `FBSOURCE_MODULE_DESCRIPTOR` macro to register itself for downstream applications, usually at the end of its main source code file. The macro exports 2 symbols:

- the module's `fbsource_module_descriptor_t` with a (supposedly) unique name based on the macro's first argument
- a well-known C string `_fbsource_module_descriptor_symbol` containing that name

Dynamic loading works looks up the well-known string and registers the looked-up descriptor symbol to the framework. To link a module statically, the host just needs to be statically linked to the source module and do the same registration using a static reference at compile time:

```c
fberror_t res = framebuffet_register_module(framebuffet, _fbsource_module_descriptor__dummy);
if (res != FBERROR_NONE) {
    // some error handling
    return false;
}
```

The easiest way to find the symbol name (in this example `_fbsource_module_descriptor__dummy` for the [Dummy](fbsources/dummy) module) is by expanding the macro in an IDE.

### Framework bindings

The following projects provide bindings for the framework:

* Java: [JNA Java bindings for Frame Buffet](https://codeberg.org/dneuge/framebuffet-jna) (available on [Codeberg](https://codeberg.org/dneuge/framebuffet-jna) and [GitHub](https://github.com/dneuge/framebuffet-jna))

## History

For another project I needed a way to merge a frame buffer image from several sources which again needed to be split up and potentially transformed somehow. This all should happen multi-threaded (one of the sources being a network stream via VNC) with as low latency as possible. The end consumer (a rendering phase callback) should not have to wait for the image to be composed but just grab whatever the compositing had previously completed to keep the callback runtime as short as possible (and thus not negatively influence the application's framerate). The sources I was about to grab from had to be configurable and there had to be user-configurable variables to do system-specific image manipulation (splitting). I also wasn't sure about the exact details of the source images either. In the end I figured that if I already have a framework allowing me to configure such modular scene graphs, then I might even want to add some image generation as frame buffer sources instead of post-processing the composited frames with an overlay afterwards. Oh, and everything had to be statically linkable. Since an Internet search didn't turn up any such library I decided to implement it separate from the original project I needed it for as it might be useful for other projects.

## How to build

Builds are supported on Linux, the macOS® operating system and Windows (only using Visual Studio on CLI with Git bash, see below), with cross-compilation support from Linux for Windows using MinGW.

Environment variables `CC` (to select a C compiler) and `CPP_COMPILER` (to select a C++ compiler) are honored and can be used to override automated compiler selection. If not set, Linux builds will be compiled using Clang or GCC, if Clang is unavailable. When targeting Windows, MinGW will be used when compiling on Linux.

By default, build scripts will attempt to detect the number of CPUs and use all + 1 for compilation (except if anything needs to be built single-threaded). Environment variable `NUM_CPUS` can be used to override auto-detection - setting it to `0` will force single-thread compilation which works around Clang compiler messages being garbled up when printed concurrently.

Visual Studio is only supported for compilation of Windows binaries on CLI, but (at least currently) not for development. The main purpose is to enable builds on CI platforms which may offer more trouble-free license conditions than MinGW, although native C11 thread support and much smaller binaries are also a nice side-effect. Builds using Visual Studio are, as unconventional as it may be, performed using the same build scripts written for Bash. The easiest way is to install the official Git distribution for Windows which includes "Git Bash". Most CI providers usually have such environments already included in their standard images.

### Prerequisites

Build scripts require a Bash shell and assume a Linux/Unix-like environment. If you are on Windows, you will probably want to use a VM, WSL, Docker container or similar. Visual Studio builds can be performed using Git Bash or similar (see above).

A recommended [`Dockerfile`](container/build-ubuntu22.04) based on Ubuntu 22.04 is included within this repository and can be used as a base for customized images.

Compilation with Clang (17 or later) is recommended but GCC and MinGW (to target Windows) should work as well. Note that the compiler and its environment may have an impact on licensing terms when distributing binaries. Proper licensing is the sole responsibility of developers using this library in their projects; this project cannot (and will not) give any advice in that regard.

The macOS® operating system can only be targeted using the Xcode® development suite on a genuine Mac® computer. Cross-compilation support from other platforms will not be supported and shall not be discussed unless officially approved by Apple Inc.

Some modules require external dependencies which can be downloaded and built (confined to the project) automatically as described below. Linux modules also require X11 dependencies to be globally available on the build environment.

#### Old Clang incompatibility

Clang before version 17 cannot be used to build source modules as it does not regard constant function references as compile-time constants.
Compilation will abort with the following error which appears to have been a compiler bug which is fixed in Clang 17 and later:

```
error: initializer element is not a compile-time constant
FBSOURCE_MODULE_DESCRIPTOR(dummy, "Dummy", FBMODULE_LICENSE_MIT, dummy_module)
                                                                 ^~~~~~~~~~~~
note: expanded from macro 'FBSOURCE_MODULE_DESCRIPTOR'
        .ops = moduleops                                                \
               ^~~~~~~~~
```

#### C11 Threads support

Frame Buffet relies on C11 Threads. Unfortunately, as of late 2025, some environments still do not support those, so wrappers are used if necessary:

- C11 Threads are natively supported (no wrapper required) when targeting Linux (either with GCC or Clang) or when using a recent Visual Studio to target Windows 10 or later.
- The macOS® operating system only supports POSIX Threads without wrapping C11 Threads on its own, so a [wrapper](https://github.com/dneuge/c11-threads-compat-for-macos-operating-system) included within this project is being used to supplement the missing translations.
- MinGW currently does not have the bindings necessary to access C11 Thread support in Windows (e.g. for cross-compilation from Linux). By default, a wrapper extracted from [Mesa](https://mesa3d.org) is used instead.

The wrappers included with Frame Buffet may cause issues when integrating the library into projects that also rely on C11 Threads but use different wrappers. It may be necessary to unify all modules to use a single common wrapper implementation instead when encountering ambiguous symbol references/confusion of wrapper implementations. This also means that, although generally supported, compilation with **MinGW is not recommended** to avoid compatibility issues and overly complex build setups.

### Build scripts

To build the plugin execute the following scripts in sequence, as needed:

* [`fetch-libs.sh`](fetch-libs.sh) downloads (and verifies) external dependencies from the Internet into the `lib` directory
* [`build-libs.sh`](build-libs.sh) builds the external dependencies
* [`build.sh`](build.sh) builds the framework incl. all modules
  * [`tools/fbsourceviewer`](tools/fbsourceviewer) will only get built if SDL2 has been installed globally on the host system; only tested on Linux

You may also find [`do_all.sh`](do_all.sh) useful which runs all these commands in sequence.

#### Environment variables

The build scripts support some optional environment variables:

| Script | Variable | Effect |
| ------ | -------- | ------ |
| `fetch-libs.sh` | `FBFETCHLIBS_DOWNLOAD_ONLY=1` | files will only be downloaded, not extracted (useful for CI cache updates) |
| `build-libs.sh` | `FBBUILDLIBS_KEEP_BUILD=1` | dependency build target directory `lib/_build` will not be deleted (useful for inclusion to other projects) |
| `build-libs.sh`, `build.sh` | `NUM_CPUS` | overrides automatically detected number of CPUs to use, set to `0` to force single-threaded compilation |

## How to develop

Frame buffer source modules are separate dynamically linkable libraries (.so/.dll). Please refer to [`fbsources`](fbsources) for the modules currently included in this repository. 

You may find [`tools/fbsourceviewer`](tools/fbsourceviewer) useful for manual testing.

## Security

Please refer to the [security policy](SECURITY.md) for details on how to report security issues and an overview of security considerations.

## License

The source code and accompanying files are released under [MIT license](LICENSE.md) unless stated otherwise. The framework does not depend on any dependencies that would restrict the license.

**Note that modules can use external dependencies which may have different licenses.** The MIT license is flexible enough to allow interfacing with different licenses but as a result the **compiled binaries may require distribution under multiple/different licenses** and **at runtime licenses may change depending on loaded modules**.

Please check modules for all applicable licenses before use or redistribution.

### Major inclusions

Due to a lack of knowledge (and documentation) about Windows programming, capturing on Windows (`capture_winrt_dxgi`)
would not have been possible to implement without *closely* following/translating the following projects by
Robert Mikhayelyan, with the Windows-binding parts of the Frame Buffet source module effectively being derivatives
of those projects:

- https://github.com/robmikh/Win32CaptureSample, licensed MIT, Copyright (c) 2019 Robert Mikhayelyan
- https://github.com/robmikh/robmikh.common, licensed MIT, Copyright (c) 2019 Robert Mikhayelyan

### Note on the use of/for AI

Usage for AI training is subject to individual source licenses, there is no exception. This generally means that proper
attribution must be given and disclaimers may need to be retained when reproducing relevant portions of training data.
When incorporating source code, AI models generally become derived projects. As such, they remain subject to the
requirements set out by individual licenses associated with the input used during training. When in doubt, all files
shall be regarded as proprietary until clarified.

Unless you can comply with the licenses of this project you obviously are not permitted to use it for your AI training
set. Although it may not be required by those licenses, you are additionally asked to make your AI model publicly
available under an open license and for free, to play fair and contribute back to the open community you take from.

For contributions to this project, **AI tools and services may only be used as detailed in the
[Contribution Guidelines](CONTRIBUTING.md) and separate [AI Usage Policy](AI_USAGE_POLICY.md)**. Contributors will be
asked to confirm and permanently record compliance with that policy. Violations may lead to immediate and permanent
removal from this project, depending on severity.

## Acknowledgements

Mac, macOS and Xcode are trademarks of Apple Inc., registered in the U.S. and other countries and regions.
