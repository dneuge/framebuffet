ARG BASE_PINNED_VERSION
FROM ubuntu:jammy-${BASE_PINNED_VERSION}

# including a build date in the BUILD_ID makes sure that we invalidate the cache
# at least once a day, otherwise we end up with a stale package database and
# no system updates
ARG DOCKER_BUILD_ID

## base system
RUN apt -y update \
    && apt -y upgrade

RUN apt -y install \
    7zip \
    build-essential \
    cmake \
    curl \
    git \
    mingw-w64 \
    nano \
    nasm \
    pkgconf \
    unzip \
    wget

## directory structure
RUN mkdir /build

## build user
RUN groupadd -g 1000 build \
    && useradd -u 1000 -g build -d /home/build -m -s /bin/bash build \
    && chown -R build:build /build

VOLUME /build
CMD ["/bin/bash"]
