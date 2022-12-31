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
 - X11 development resources (X.org server, Xlib, Xext, Xcomposite, Xdamage)

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

For compatibility with default Gitlab CI `/builds` has been prepared as the recommended location to perform builds in. By default, an unprivileged user `build` (UID/GID 1000) with `/builds` as the initial working directory will be used unless deployment options are changed for the image (`--user` on `docker create`/`docker run`).

### Speed up local compilation

When using the container locally without CI or other tools, you may want to add some aliases to `~build/.bashrc` and preserve previous dependency builds so you can much more rapidly deploy code for rebuilds.

You can easily copy a local workspace into the docker container by running:

```
docker cp framebuffet mycontainer:/builds/
```

Enter the container and run `/builds/framebuffet/do_all.sh` with the target platform (`linux` or `windows`) as parameter once. The easiest way to copy changes into the docker container is by deleting `/builds/framebuffet` and rerunning the `docker cp` command. Usually this would cause a full rebuild (and redownload) of all depenedencies as those would be deleted as well. To avoid that you can simply move the `lib` directory out of `framebuffet` by one level:

`mv /builds/framebuffet/lib /builds/`

Assuming we want to use the container for repeated Windows builds, we could then add the following lines to `~build/.bashrc` (`nano` is available by default):

```
alias build='rm -Rf /builds/framebuffet/lib && ln -s /builds/lib /builds/framebuffet/lib && /builds/framebuffet/build.sh windows'
alias cleanup='cd /builds && rm -Rf framebuffet'
alias rebuild='cleanup && read && build'
```

After activating the aliases (`source ~/.bashrc` or container relogin) all you would now need to do is in sequence:

1. inside the interactively running container (`docker run -ti`/`docker start -i`) enter `rebuild`
   - the `cleanup` alias will delete `/builds/framebuffet` (very fast)
   - `read` causes the shell to wait for input; don't do anything yet
2. outside the container run the `docker cp` command again (this may take a moment on first call but should be significantly faster when repeated on Linux)
3. once the copy has finished, continue by pressing *enter* inside the container
   - the `build` alias replaces `/builds/framebuffet/lib` by a symlink to our backup copy at `/builds/lib`,
   - then `build.sh windows` gets invoked
4. check the build result, copy build artifacts from the container out to the host system if needed (`docker cp mycontainer:/builds/framebuffet/... whereever/on/the/host/`) and simply repeat from step 1 to `rebuild` again (just press *arrow up* in the container's shell and confirm)
