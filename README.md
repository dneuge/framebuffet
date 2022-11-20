# Frame Buffet - all you can treat!

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Frame Buffet is a basic framework implementing an externally configurable modular 2D scene graph for frame buffer compositing. That was a lot of buzz words in one sentence, so what is it meant to do?

For another project I needed a way to merge a frame buffer image from several sources which again needed to be split up and potentially transformed somehow. This all should happen multi-threaded (one of the sources being a network stream via VNC) with as low latency as possible. The end consumer (a rendering phase callback) should not have to wait for the image to be composed but just grab whatever the compositing had previously completed to keep the callback runtime as short as possible (and thus not negatively influence the application's framerate). The sources I was about to grab from had to be configurable and there had to be user-configurable variables to do system-specific image manipulation (splitting). I also wasn't sure about the exact details of the source images either. In the end I figured that if I already have a framework allowing me to configure such modular scene graphs, then I might even want to add some image generation as frame buffer sources instead of post-processing the composited frames with an overlay afterwards. Oh, and everything had to be statically linkable. Since an Internet search didn't turn up any such library I decided to implement it separate from the original project I needed it for as it might be useful for other projects.

The framework uses interfaces in C to be as universally reusable as possible.

## Current state

The framework is in early development. The API is *not stable* yet and may change at any time without notice.

## How to build

Builds are currently only supported for Linux on Linux. Cross-compilation support for Windows will be added shortly.

There are currently no plans for Mac builds as the target platform is unavailable to the developer(s). If you would like to volunteer for taking care of maintaining and testing Mac builds please contact the project.

### Prerequisites

Building is currently only supported from a Linux environment. If you're not on Linux, you will probably want to use a VM, WSL, Docker container or similar.

Some modules require external dependencies which can be downloaded and built (confined to the project) automatically as described below.

### Build scripts

To build the plugin execute the following scripts in sequence, as needed:

* [`fetch-libs.sh`](fetch-libs.sh) downloads (and verifies) external dependencies from the Internet into the `lib` directory
* [`build-libs.sh`](build-libs.sh) builds the external dependencies
* [`build.sh`](build.sh) builds the framework incl. all modules

You may also find [`do_all.sh`](do_all.sh) useful which runs all these commands in sequence.

## How to develop

Frame buffer source modules are separate dynamically linkable libraries (.so/.dll). Please refer to [`fbsources`](fbsources) for the modules currently included in this repository. 

You may find [`tools/fbsourceviewer`](tools/fbsourceviewer) useful for manual testing.

## License

The source code and accompanying files are released under [MIT license](LICENSE.md) unless stated otherwise. The framework does not depend on any dependencies that would restrict the license.

**Note that modules can use external dependencies which may have different licenses.** The MIT license is flexible enough to allow interfacing with different licenses but as a result the **compiled binaries may require distribution under multiple/different licenses** and **at runtime licenses may change depending on loaded modules**.

Please check modules for all applicable licenses before use or redistribution.
