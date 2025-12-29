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
    autoconf \
    build-essential \
    clang \
    cmake \
    curl \
    git \
    libsdl2-dev \
    libtool \
    libxcomposite-dev \
    libxdamage-dev \
    libxext-dev \
    libxml2-utils \
    mingw-w64 \
    nano \
    nasm \
    pkgconf \
    python3 \
    unzip \
    xserver-xorg-dev \
    xutils-dev \
    wget

## directory structure
RUN mkdir /builds

## build user
RUN groupadd -g 1000 build \
    && useradd -u 1000 -g build -d /home/build -m -s /bin/bash build \
    && chown -R build:build /builds

VOLUME /builds
USER build:build
WORKDIR /builds
CMD ["/bin/bash"]
