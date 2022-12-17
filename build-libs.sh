#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

source ./_build_target.sh || die "Failed to include build target script"

[[ -d lib/_build ]] && rm -Rf lib/_build
mkdir -p lib/_build

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

echo "==== Building cJSON ===="
cd "${script_dir}/lib/cjson"
[[ -d build ]] && (rm -Rf build || die "cJSON clean failed")
mkdir build || die "cJSON mkdir failed"
cd build
cmake -DBUILD_SHARED_AND_STATIC_LIBS=On .. || die "cJSON CMake failed"
make -j$num_jobs || die "cJSON make failed"
cp -a libcjson.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "cJSON copy failed (1)"
cp -a libcjson*.a "${script_dir}/lib/_build/" || die "cJSON copy failed (2)"
cp -a ../cJSON.h "${script_dir}/lib/_build/" || die "cJSON copy failed (3)"
echo

echo "==== Building zlib ===="
cd "${script_dir}/lib/zlib"
[[ -d build ]] && (rm -Rf build || die "zlib clean failed")
mkdir build || die "zlib mkdir failed"
cd build
cmake .. || die "zlib CMake failed"
make -j$num_jobs || die "zlib make failed"
cp -a libz*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "zlib copy failed (1)"
cp -a libz*.a "${script_dir}/lib/_build/" || die "zlib copy failed (2)"
cp -a zconf.h "${script_dir}/lib/_build/" || die "zlib copy failed (3)"
cp -a ../zlib.h "${script_dir}/lib/_build/" || die "zlib copy failed (4)"
echo

echo "==== Building LZO ===="
cd "${script_dir}/lib/lzo"
[[ -d build ]] && (rm -Rf build || die "lzo clean failed")
mkdir build || die "lzo mkdir failed"
cd build
cmake -DENABLE_SHARED=ON .. || die "lzo CMake failed"
make -j$num_jobs || die "lzo make failed"
cp -a liblzo2.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "lzo copy failed (1)"
cp -a liblzo2.a "${script_dir}/lib/_build/" || die "lzo copy failed (2)"
mkdir -p "${script_dir}/lib/_build/lzo"
cp -a ../include/lzo/* "${script_dir}/lib/_build/lzo/" || die "lzo copy failed (3)"
echo

echo "==== Building libjpeg-turbo ===="
cd "${script_dir}/lib/libjpeg-turbo"
[[ -d build ]] && (rm -Rf build || die "libjpeg-turbo clean failed")
mkdir build || die "libjpeg-turbo mkdir failed"
cd build
cmake -DENABLE_SHARED=ON .. || die "libjpeg-turbo CMake failed"
make -j$num_jobs || die "libjpeg-turbo make failed"
cp -a libjpeg*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (1)"
cp -a libjpeg*.a "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (2)"
cp -a libturbojpeg.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (3)"
cp -a libturbojpeg.a "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (4)"
cp -a ../*.h "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (5)"
cp -a *.h "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (6)"
echo

function build_libvncclient {
    cd "${script_dir}/lib/VNC"
    [[ -d build ]] && (rm -Rf build || die "VNC clean failed")
    mkdir build || die "VNC mkdir failed"
    cd build
    cmake \
        -DWITH_PNG=OFF \
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
        -DCMAKE_MODULE_PATH="${script_dir}/lib/CMakeModules" \
        .. || die "VNC CMake failed"
    make -j$num_jobs || die "VNC make failed"
    
    # to disable global find module database; see https://stackoverflow.com/questions/71613328/how-to-limit-find-package-search-to-cmake-module-path-by-default
    #   -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=TRUE \
    #   -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE \
    
    if [[ "$1" == "ON" ]]; then
        # dynamically linked
        cp -a libvncclient.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "VNC copy failed (1)"
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
