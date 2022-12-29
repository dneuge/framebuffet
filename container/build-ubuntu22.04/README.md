# Build container image for Frame Buffet compilation

This is a container image for the officially recommended build environment for Frame Buffet.

## Components

Base:
 - Ubuntu 22.04 LTS (Jammy Jellyfish: `jammy`)

Primary:
 - "build-essential"
 - GCC 10
 - MinGW
 - NASM
 - X11 development resources (X.org server, Xlib, Xext, Xcomposite)

Supplemental:
 - 7zip
 - curl
 - git
 - nano
 - pkgconf
 - unzip
 - wget

## Building the image

### Automated

Automated building is possible by simply running `./autobuild.sh`.

Dependencies:

 - [Skopeo](https://github.com/containers/skopeo)
 - Python 3

autobuild will search for the latest pinned version of the required base image and start a build resulting in a tag `framebuffet-build-ubuntu22.04:YYMMDD`. Using environment variables `TAG_PREFIX` and `TAG_SUFFIX` tag can be amended as required. For example:

`TAG_PREFIX="my.preferred.repository/" ./autobuild.sh`

autobuild only builds but does *not* push the resulting image. Additional arguments will be passed on to `build.sh` (see below).

### Manual control

As usual, `BASE_PINNED_VERSION` must be provided with the tag date code for an `ubuntu:jammy` (22.04 LTS) base image. Although `build.sh` does not do anything special for this image, let's use it as a wrapper in case this changes later:

`./build.sh -t framebuffet-build-ubuntu22.04:221216 --build-arg BASE_PINNED_VERSION=20221130 .`

Arguments at build-time:

| Argument              | Default value                | Description                                                       |
| --------------------- | ---------------------------- | ----------------------------------------------------------------- |
| `BASE_PINNED_VERSION` | *none*                       | `ubuntu:jammy` base image date code                               |
| `BUILD_ID`            | *none*                       | build ID, preferably date-based; required to invalidate cache     |

## Using the container

The container does not define an entrypoint, the default command is simply a bash shell. You either need to enter it interactively or run your own scripts for build automation.

For compatibility with default Gitlab CI `/builds` has been prepared as the recommended location to perform builds in. By default, an unprivileged user `build` (UID/GID 1000) with `/builds` as the initial working directory will be used unless deployment options are changed for the image.
