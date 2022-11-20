#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

[[ -d build ]] && rm -Rf build
mkdir -p build

cd build

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

cmake .. || die "CMake failed"
make -j$num_jobs || die "make failed"

echo Build complete.
