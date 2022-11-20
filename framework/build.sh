#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

function die {
    echo $@
    exit 1
}

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

[[ -d "${build_dir}" ]] && rm -Rf "${build_dir}"
mkdir -p "${build_dir}"

[[ -d "${release_dir}" ]] && rm -Rf "${release_dir}"
mkdir -p "${release_dir}"

## BUILD
cd "${build_dir}"

cmake .. || die "CMake failed"
make -j$num_jobs || die "make failed"

## COPY
mkdir -p "${release_dir}/lib" || die "Failed to create release directory lib"
cp -a libframebuffet.so "${release_dir}/lib/" || die "Failed to copy Linux shared library to release directory"
cp -a libframebuffet.a "${release_dir}/lib/" || die "Failed to copy Linux static library to release directory"

mkdir -p "${release_dir}/include" || die "Failed to create release directory include"
cp -a "${src_dir}"/*.h "${release_dir}/include/" || die "Failed to copy Linux headers to release directory"

echo Build complete.
