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

function cd_real_path {
    cd "$(realpath "$1")" || die "Failed to change into real path of $1"
}

source ./_build_target.sh || die "Failed to include build target script"

if [[ "${FBBUILDLIBS_KEEP_BUILD:-0}" == "1" ]]; then
    echo "Frame Buffet build-libs: keep build target directory"
else
    [[ -d lib/_build ]] && rm -Rf lib/_build
fi
mkdir -p lib/_build

num_jobs=$(( $NUM_CPUS + 1 ))

MULTIARCH_FLAGS=""
if [[ "${BUILD_TARGET}" == "macos" ]]; then
    MULTIARCH_FLAGS="-arch x86_64 -arch arm64"
fi


# MinGW misses some C11 compatibility, most notably threads.h which is extensively used throughout Frame Buffet
# Mesa seems to have the best maintained C11 compatibility wrapper available, so we borrow that but only if we
# compile for Windows - other systems/environments should hopefully support C11 by now...
if [[ "${BUILD_TARGET}" != "windows" ]]; then
    echo "==== Skipping Mesa (only C11 compatibility) ===="
else
    echo "==== Building Mesa (only C11 compatibility) ===="
    cd_real_path "${script_dir}/lib/mesa/_c11_only"
    [[ -d build ]] && (rm -Rf build || die "Mesa (only C11 compatibility) clean failed")
    mkdir build || die "Mesa (only C11 compatibility) mkdir failed"
    cd build
    cmake .. || die "Mesa (only C11 compatibility) CMake failed"
    make -j$num_jobs || die "Mesa (only C11 compatibility) make failed"
    mkdir -p "${script_dir}/lib/_build/c11" || die "Mesa (only C11 compatibility) mkdir failed"
    cp -a libmesa_c11*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "Mesa (only C11 compatibility) copy failed (1)"
    cp -a libmesa_c11*.a "${script_dir}/lib/_build/" || die "Mesa (only C11 compatibility) copy failed (2)"
    cp -a ../../src/c11/*.h "${script_dir}/lib/_build/c11" || die "Mesa (only C11 compatibility) copy failed (3)"
    
    if ! grep C_DEFINES CMakeFiles/mesa_c11.dir/flags.make | grep -- -DHAVE_STRUCT_TIMESPEC; then
        echo "- CMake appears NOT to have used -DHAVE_STRUCT_TIMESPEC, keeping headers unmodified"
    else
        echo "- CMake appears to have used -DHAVE_STRUCT_TIMESPEC, persisting in time.h"
        
        cd "${script_dir}/lib/_build/c11"
        patch -p1 <<'EOF'
--- a/time.h  2022-12-19 14:45:40.920933949 +0000
+++ b/time.h  2022-12-19 14:33:26.694721638 +0000
@@ -27,6 +27,7 @@
  * On MSVC `struct timespec` and `timespec_get` present at the same time;
  * So detecting `HAVE_STRUCT_TIMESPEC` in meson script dynamically.
  */
+#define HAVE_STRUCT_TIMESPEC /* set by Frame Buffet during build-libs.sh */
 #ifndef HAVE_STRUCT_TIMESPEC
 struct timespec
 {
EOF
    fi
fi
echo

echo "==== Building cJSON ===="
# ENABLE_PUBLIC_SYMBOLS and ENABLE_HIDDEN_SYMBOLS are undocumented extra options
# if they are not set, cJSON's symbols will override all other symbols in a statically linked DLL,
# regardless of other linker options used to link the including module, so cJSON's symbol export
# unfortunately has to be disabled here (MinGW 10.3.0)
cd_real_path "${script_dir}/lib/cjson"
[[ -d build ]] && (rm -Rf build || die "cJSON clean failed")
mkdir build || die "cJSON mkdir failed"
cd build
cmake \
    -D BUILD_SHARED_AND_STATIC_LIBS=On \
    -D ENABLE_CJSON_TEST=Off \
    -D ENABLE_PUBLIC_SYMBOLS=Off \
    -D ENABLE_HIDDEN_SYMBOLS=On \
    -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    .. || die "cJSON CMake failed"
make -j$num_jobs || die "cJSON make failed"
cp -a libcjson*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "cJSON copy failed (1)"
cp -a libcjson*.a "${script_dir}/lib/_build/" || die "cJSON copy failed (2)"
cp -a ../cJSON.h "${script_dir}/lib/_build/" || die "cJSON copy failed (3)"
echo

echo "==== Building zlib ===="
cd_real_path "${script_dir}/lib/zlib"
[[ -d build ]] && (rm -Rf build || die "zlib clean failed")
mkdir build || die "zlib mkdir failed"
cd build
cmake -D CMAKE_POSITION_INDEPENDENT_CODE=TRUE -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" .. || die "zlib CMake failed"
make -j$num_jobs || die "zlib make failed"
cp -a libz*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "zlib copy failed (1)"
cp -a libz*.a "${script_dir}/lib/_build/" || die "zlib copy failed (2)"
cp -a zconf.h "${script_dir}/lib/_build/" || die "zlib copy failed (3)"
cp -a ../zlib.h "${script_dir}/lib/_build/" || die "zlib copy failed (4)"
echo

echo "==== Building LZO ===="
cd_real_path "${script_dir}/lib/lzo"
[[ -d build ]] && (rm -Rf build || die "lzo clean failed")
mkdir build || die "lzo mkdir failed"
cd build
cmake -D ENABLE_SHARED=ON -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" .. || die "lzo CMake failed"
make -j$num_jobs || die "lzo make failed"
cp -a liblzo2*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "lzo copy failed (1)"
cp -a liblzo2.a "${script_dir}/lib/_build/" || die "lzo copy failed (2)"
mkdir -p "${script_dir}/lib/_build/lzo"
cp -a ../include/lzo/* "${script_dir}/lib/_build/lzo/" || die "lzo copy failed (3)"
echo

if [[ "${BUILD_TARGET}" == "macos" ]]; then
    echo "==== Skipping libjpeg-turbo (universal binaries cannot be built) ===="
    # TODO: issues hint at universal binaries being built by upstream project through some kind of post-processing (direct compilation is not possible due to the way assembler code is being integrated) but there is no documentation on how that should be done
else
    echo "==== Building libjpeg-turbo ===="
    cd_real_path "${script_dir}/lib/libjpeg_turbo"
    [[ -d build ]] && (rm -Rf build || die "libjpeg-turbo clean failed")
    mkdir build || die "libjpeg-turbo mkdir failed"
    cd build
    cmake -D ENABLE_SHARED=ON -D CMAKE_POSITION_INDEPENDENT_CODE=TRUE .. || die "libjpeg-turbo CMake failed"
    make -j$num_jobs || die "libjpeg-turbo make failed"
    cp -a libjpeg*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (1)"
    cp -a libjpeg*.a "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (2)"
    cp -a libturbojpeg*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (3)"
    cp -a libturbojpeg.a "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (4)"
    cp -a ../*.h "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (5)"
    cp -a *.h "${script_dir}/lib/_build/" || die "libjpeg-turbo copy failed (6)"
fi
echo

if [[ "${BUILD_TARGET}" != "macos" ]]; then
    echo "==== Skipping libjpeg (only needed on Mac) ===="
else
    echo "==== Building libjpeg ===="
    cd_real_path "${script_dir}/lib/libjpeg"
    
    # see install.txt; alternative to running ./configure
    cp -a makefile.xc Makefile
    cp -a jconfig.xc jconfig.h
    
    make clean || die "libjpeg clean failed"
    make || die "libjpeg make failed"
    cp -a {jpeglib,jconfig,jmorecfg,jpegint,jerror}.h "${script_dir}/lib/_build/" || die "libjpeg copy failed (1)"
    cp -a libjpeg.a "${script_dir}/lib/_build/" || die "libjpeg copy failed (2)"
fi
echo

function build_libvncclient {
    cd_real_path "${script_dir}/lib/VNC"
    [[ -d build ]] && (rm -Rf build || die "VNC clean failed")
    mkdir build || die "VNC mkdir failed"
    cd build
    cmake \
        -D WITH_PNG=OFF \
        -D WITH_SDL=OFF \
        -D WITH_GTK=OFF \
        -D WITH_LIBSSH2=OFF \
        -D WITH_GNUTLS=OFF \
        -D WITH_OPENSSL=OFF \
        -D WITH_SYSTEMD=OFF \
        -D WITH_GCRYPT=OFF \
        -D WITH_FFMPEG=OFF \
        -D WITH_TIGHTVNC_FILETRANSFER=OFF \
        -D WITH_WEBSOCKETS=OFF \
        -D WITH_SASL=OFF \
        -D WITH_EXAMPLES=OFF \
        -D WITH_TESTS=$2 \
        -D BUILD_SHARED_LIBS=$1 \
        -D CMAKE_POSITION_INDEPENDENT_CODE=TRUE \
        -D CMAKE_MODULE_PATH="${script_dir}/lib/CMakeModules" \
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
        .. || die "VNC CMake failed"
    make -j$num_jobs || die "VNC make failed"
    
    # to disable global find module database; see https://stackoverflow.com/questions/71613328/how-to-limit-find-package-search-to-cmake-module-path-by-default
    #   -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=TRUE \
    #   -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE \
    
    if [[ "$1" == "ON" ]]; then
        # dynamically linked
        cp -a libvncclient*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "VNC copy failed (1)"
    else
        # statically linked
        cp -a libvncclient.a* "${script_dir}/lib/_build/" || die "VNC copy failed (1)"
        mkdir -p "${script_dir}/lib/_build/rfb"
        cp -a ../rfb/*.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (2)"
        cp -a rfb/rfbconfig.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (3)"
    fi
}


if [[ "${BUILD_TARGET}" == "macos" ]]; then
    echo "==== Skipping libvncclient (dynamically linked; not available on Mac due to static-only libjpeg) ===="
else
    echo "==== Building libvncclient (dynamically linked) ===="
    build_libvncclient ON ON
fi
echo

echo "==== Building libvncclient (statically linked) ===="
if [[ "${BUILD_TARGET}" == "macos" ]]; then
    # tests require dynamically linked libjpeg which is unavailable on Mac
    build_libvncclient OFF OFF
else
    build_libvncclient OFF ON
fi
echo

if [[ "${BUILD_TARGET}" != "linux" ]]; then
    echo "==== Skipping dependencies related to X11 (Linux only) ===="
else
    echo "==== Building xcb/xcb-proto (Linux) ===="
    python_version=$(python3 <<<'import sys;print("%d.%d" % (sys.version_info.major, sys.version_info.minor))' || die "unable to determine Python 3 version")
    xcb_util_m4_revision="c617eee22ae5c285e79e81ec39ce96862fd3262f"
    xcb_util_image_m4_revision="${xcb_util_m4_revision}"

    echo "using Python ${python_version}"
    echo "xcb-util       uses m4 revision ${xcb_util_m4_revision}"
    echo "xcb-util-image uses m4 revision ${xcb_util_image_m4_revision}"
    echo
    
    echo "=== Preparing xcb-proto ==="
    cd_real_path "${script_dir}/lib/xcb_proto"
    autoreconf -v --install || die "xcb-proto autoreconf failed"
    echo

    echo "=== Building xcb-proto ==="
    cd_real_path "${script_dir}/lib/xcb_proto"
    [[ -d build ]] && (rm -Rf build || die "xcb-proto rm clean failed")
    mkdir build || die "xcb-proto mkdir failed"
    ./configure --prefix="$(realpath build)" || die "xcb-proto configure failed"
    make || die "xcb-proto make failed"
    make install || die "xcb-proto make install failed"
    [[ ! -e build/local ]] && (ln -s . build/local || die "xcb-proto failed to create 'local' directory symlink")
    if [[ ! -e "build/local/lib/python${python_version}/dist-packages" ]]; then
        if [[ -e "build/local/lib/python${python_version}/site-packages" ]]; then
            ln -s site-packages "build/local/lib/python${python_version}/dist-packages" || die "xcb-proto failed to create 'dist-packages' directory symlink"
        else
            die "xcb-proto failed due to unhandled directory layout for Python packages"
        fi
    fi
    echo

    echo "=== Building xcb ==="
    cd_real_path "${script_dir}/lib/xcb"
    [[ -d build ]] && (rm -Rf build || die "xcb rm clean failed")
    mkdir build || die "xcb mkdir failed"
    ./configure --prefix="$(realpath build)" || die "xcb configure failed"
    PYTHONPATH="$(realpath "../xcb_proto/build/local/lib/python${python_version}/dist-packages/"):${PYTHONPATH:-}" make -j$num_jobs || die "xcb make failed"
    make install || die "xcb make install failed"
    cp -a build/lib/*.a "${script_dir}/lib/_build/" || die "xcb copy failed (1)"
    mkdir -p "${script_dir}/lib/_build/xcb"
    cp -a build/include/xcb/* "${script_dir}/lib/_build/xcb/" || die "xcb copy failed (2)"
    echo
    
    echo "=== Preparing xcb-util ==="
    cd_real_path "${script_dir}/lib/xcb_util"
    [[ -d m4 ]] && (rm -Rf m4 || die "xcb-util rm m4 failed")
    [[ -f .gitmodules ]] && (rm .gitmodules || die "xcb-util rm .gitmodules failed")
    mkdir m4 || die "xcb-util failed to create m4 directory"
    cd m4
    git init . || die "xcb-util failed to initialize m4 git repo"
    git bundle unbundle "${script_dir}/lib/_downloads/freedesktop.libxcb-util-m4.master.bundle" || die "xcb-util failed to unbundle m4 repo"
    git checkout "${xcb_util_m4_revision}" || die "xcb-util failed to checkout m4 revision ${xcb_util_m4_revision}"
    cd ..
    autoreconf -v --install || die "xcb-util autoreconf failed"
    echo
    
    echo "=== Building xcb-util ==="
    cd_real_path "${script_dir}/lib/xcb_util"
    [[ -d build ]] && (rm -Rf build || die "xcb-util rm clean failed")
    mkdir build || die "xcb-util mkdir failed"
    ./configure --prefix="$(realpath build)" || die "xcb-util configure failed"
    PYTHONPATH="$(realpath "../xcb_proto/build/local/lib/python${python_version}/dist-packages/"):${PYTHONPATH:-}" make -j$num_jobs || die "xcb-util make failed"
    make install || die "xcb-util make install failed"
    cp -a build/lib/*.a "${script_dir}/lib/_build/" || die "xcb-util copy failed (1)"
    cp -a build/include/xcb/* "${script_dir}/lib/_build/xcb/" || die "xcb-util copy failed (2)"
    echo
    
    echo "=== Preparing xcb-util-image ==="
    cd_real_path "${script_dir}/lib/xcb_util_image"
    [[ -d m4 ]] && (rm -Rf m4 || die "xcb-util-image rm m4 failed")
    [[ -f .gitmodules ]] && (rm .gitmodules || die "xcb-util-image rm .gitmodules failed")
    mkdir m4 || die "xcb-util-image failed to create m4 directory"
    cd m4
    git init . || die "xcb-util-image failed to initialize m4 git repo"
    git bundle unbundle "${script_dir}/lib/_downloads/freedesktop.libxcb-util-m4.master.bundle" || die "xcb-util-image failed to unbundle m4 repo"
    git checkout "${xcb_util_image_m4_revision}" || die "xcb-util-image failed to checkout m4 revision ${xcb_util_image_m4_revision}"
    cd ..
    autoreconf -v --install || die "xcb-util-image autoreconf failed"
    echo
    
    echo "=== Building xcb-util-image ==="
    cd_real_path "${script_dir}/lib/xcb_util_image"
    [[ -d build ]] && (rm -Rf build || die "xcb-util-image rm clean failed")
    mkdir build || die "xcb-util-image mkdir failed"
    PKG_CONFIG_PATH="$(realpath "../xcb/build/lib/pkgconfig/"):$(realpath "../xcb_util/build/lib/pkgconfig/"):${PKG_CONFIG_PATH:-}" ./configure --prefix="$(realpath build)" || die "xcb-util-image configure failed"
    PYTHONPATH="$(realpath "../xcb_proto/build/local/lib/python${python_version}/dist-packages/"):${PYTHONPATH:-}" make -j$num_jobs || die "xcb-util-image make failed"
    make install || die "xcb-util-image make install failed"
    cp -a build/lib/*.a "${script_dir}/lib/_build/" || die "xcb-util-image copy failed (1)"
    cp -a build/include/xcb/* "${script_dir}/lib/_build/xcb/" || die "xcb-util-image copy failed (2)"
    echo
fi


echo Lib build complete.
