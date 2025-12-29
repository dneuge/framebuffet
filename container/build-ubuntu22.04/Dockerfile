ARG BASE_PINNED_VERSION
FROM ubuntu:jammy-${BASE_PINNED_VERSION}

# including a build date in the BUILD_ID makes sure that we invalidate the cache
# at least once a day, otherwise we end up with a stale package database and
# no system updates
ARG DOCKER_BUILD_ID

## base system
RUN apt -y update \
    && apt -y upgrade
    
# we need Clang 17 which needs to be downloaded from LLVM as Ubuntu 22.04 only has version 15 at most
# ca-certificates has to be installed before we can connect to the LLVM repository
RUN apt -y install ca-certificates
COPY files/usr/share/keyrings/* /usr/share/keyrings/
COPY files/etc/apt/sources.list.d/* /etc/apt/sources.list.d/
RUN apt -y update

RUN apt -y install \
    7zip \
    autoconf \
    build-essential \
    clang-17 \
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

# symlink Clang 17 to be the default version
RUN ln -s "$(which clang-17)" /etc/alternatives/clang && \
    ln -s "$(which clang-cpp-17)" /etc/alternatives/clang-cpp && \
    ln -s "$(which 'clang++-17')" /etc/alternatives/'clang++' && \
    ln -s /etc/alternatives/clang /usr/bin/ && \
    ln -s /etc/alternatives/clang-cpp /usr/bin/ && \
    ln -s /etc/alternatives/'clang++' /usr/bin/

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
