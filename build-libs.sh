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

# TODO: reduce the amount of dependencies of libvncclient
# TODO: link libvncclient statically

echo "==== Building libvncclient ===="
cd "${script_dir}/lib/VNC"
[[ -d build ]] && (rm -Rf build || die "VNC clean failed")
mkdir build || die "VNC mkdir failed"
cd build
cmake \
  -DWITH_JPEG=0 \
  -DWITH_SDL=0 \
  -DWITH_GTK=0 \
  -DWITH_LIBSSH2=0 \
  -DWITH_GNUTLS=0 \
  -DWITH_OPENSSL=0 \
  -DWITH_SYSTEMD=0 \
  -DWITH_GCRYPT=0 \
  -DWITH_FFMPEG=0 \
  -DWITH_TIGHTVNC_FILETRANSFER=0 \
  -DWITH_WEBSOCKETS=0 \
  -DWITH_SASL=0 \
  -DWITH_EXAMPLES=0 \
  .. || die "VNC CMake failed"
make -j$num_jobs || die "VNC make failed"
cp -a libvncclient.so* "${script_dir}/lib/_build/" || die "VNC copy failed (1)"
mkdir "${script_dir}/lib/_build/rfb"
cp -a ../rfb/*.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (2)"
cp -a rfb/rfbconfig.h "${script_dir}/lib/_build/rfb/" || die "VNC copy failed (3)"
echo

echo Lib build complete.
