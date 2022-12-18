#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}/../.."
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

source ../../_build_target.sh || die "Failed to include build target script"

if [[ "${BUILD_TARGET}" == "windows" ]]; then
    echo "fbsourceviewer compilation to Windows is not supported yet; skipping build"
    exit 0
fi

if ! pkgconf sdl2; then
    echo "fbsourceviewer compilation currently is host-only but SDL2 does not seem to have been installed globally; skipping build"
    exit 0
fi

[[ -d build ]] && rm -Rf build
mkdir -p build

cd build

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

cmake .. || die "CMake failed"
make -j$num_jobs || die "make failed"

echo Build complete.
