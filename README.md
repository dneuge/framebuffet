# Frame Buffet - all you can treat!

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Frame Buffet is a basic framework implementing an externally configurable modular 2D scene graph for frame buffer compositing. That was a lot of buzz words in one sentence, so what is it meant to do?

For another project I needed a way to merge a frame buffer image from several sources which again needed to be split up and potentially transformed somehow. This all should happen multi-threaded (one of the sources being a network stream via VNC) with as low latency as possible. The end consumer (a rendering phase callback) should not have to wait for the image to be composed but just grab whatever the compositing had previously completed to keep the callback runtime as short as possible (and thus not negatively influence the application's framerate). The sources I was about to grab from had to be configurable and there had to be user-configurable variables to do system-specific image manipulation (splitting). I also wasn't sure about the exact details of the source images either. In the end I figured that if I already have a framework allowing me to configure such modular scene graphs, then I might even want to add some image generation as frame buffer sources instead of post-processing the composited frames with an overlay afterwards. Oh, and everything had to be statically linkable. Since an Internet search didn't turn up any such library I decided to implement it separate from the original project I needed it for as it might be useful for other projects.

The framework uses interfaces in C to be as universally reusable as possible.

## Current state

The framework is in early development. The API is *not stable* yet and may change at any time without notice.

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

## License

The source code and accompanying files are released under [MIT license](LICENSE.md) unless stated otherwise. The framework does not depend on any dependencies that would restrict the license.

**Note that modules can use external dependencies which may have different licenses.** The MIT license is flexible enough to allow interfacing with different licenses but as a result the **compiled binaries may require distribution under multiple/different licenses** and **at runtime licenses may change depending on loaded modules**.

Please check modules for all applicable licenses before use or redistribution.

### Note on the use of/for AI

Usage for AI training is subject to individual source licenses, there is no exception. This generally means that proper
attribution must be given and disclaimers may need to be retained when reproducing relevant portions of training data.
When incorporating source code, AI models generally become derived projects. As such, they remain subject to the
requirements set out by individual licenses associated with the input used during training. When in doubt, all files
shall be regarded as proprietary until clarified.

Unless you can comply with the licenses of this project you obviously are not permitted to use it for your AI training
set. Although it may not be required by those licenses, you are additionally asked to make your AI model publicly
available under an open license and for free, to play fair and contribute back to the open community you take from.

AI tools are not permitted to be used for contributions to this project. The main reason is that, as of time of writing,
no tool/model offers traceability nor can today's AI models understand and reason about what they are actually doing.
Apart from potential copyright/license violations the quality of AI output is doubtful and generally requires more
effort to be reviewed and cleaned/fixed than actually contributing original work. Contributors will be asked to confirm
and permanently record compliance with these guidelines.

## Acknowledgements

Mac, macOS and Xcode are trademarks of Apple Inc., registered in the U.S. and other countries and regions.
