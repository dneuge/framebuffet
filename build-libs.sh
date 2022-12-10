#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

[[ -d lib/_build ]] && rm -Rf lib/_build
mkdir -p lib/_build

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

echo "==== Building zlib ===="
cd "${script_dir}/lib/zlib"
[[ -d build ]] && (rm -Rf build || die "zlib clean failed")
mkdir build || die "zlib mkdir failed"
cd build
cmake .. || die "zlib CMake failed"
make -j$num_jobs || die "zlib make failed"
cp -a libz.so* "${script_dir}/lib/_build/" || die "zlib copy failed (1)"
cp -a libz.a "${script_dir}/lib/_build/" || die "zlib copy failed (2)"
cp -a zconf.h "${script_dir}/lib/_build/" || die "zlib copy failed (3)"
cp -a ../zlib.h "${script_dir}/lib/_build/" || die "zlib copy failed (4)"
echo

function build_libvncclient {
    cd "${script_dir}/lib/VNC"
    [[ -d build ]] && (rm -Rf build || die "VNC clean failed")
    mkdir build || die "VNC mkdir failed"
    cd build
    cmake \
        -DWITH_JPEG=OFF \
        -DWITH_SDL=OFF \
        -DWITH_GTK=OFF \
        -DWITH_LIBSSH2=OFF \
        -DWITH_GNUTLS=OFF \
        -DWITH_OPENSSL=OFF \
        -DWITH_SYSTEMD=OFF \
        -DWITH_GCRYPT=OFF \
        -DWITH_FFMPEG=OFF \
        -DWITH_TIGHTVNC_FILETRANSFER=OFF \
        -DWITH_WEBSOCKETS=OFF \
        -DWITH_SASL=OFF \
        -DWITH_EXAMPLES=OFF \
        -DBUILD_SHARED_LIBS=$1 \
        .. || die "VNC CMake failed"
    make -j$num_jobs || die "VNC make failed"
    
    if [[ "$1" == "ON" ]]; then
        # dynamically linked
        cp -a libvncclient.so* "${script_dir}/lib/_build/" || die "VNC copy failed (1)"
        mkdir -p "${script_dir}/lib/_build/rfb"
        cp -a ../rfb/*.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (2)"
        cp -a rfb/rfbconfig.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (3)"
    else
        # statically linked
        cp -a libvncclient.a* "${script_dir}/lib/_build/" || die "VNC copy failed"
    fi
}

echo "==== Building libvncclient (dynamically linked) ===="
build_libvncclient ON
echo

echo "==== Building libvncclient (statically linked) ===="
build_libvncclient OFF
echo

echo Lib build complete.
