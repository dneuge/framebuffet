#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}/.."
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

function die {
    echo $@
    exit 1
}

source ../_build_target.sh || die "Failed to include build target script"

num_jobs=$(( $NUM_CPUS + 1 ))

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
cp -a *framebuffet*.${BUILD_TARGET_DYNLIB_EXT}* "${release_dir}/lib/" || die "Failed to copy shared library to release directory"
cp -a libframebuffet.a "${release_dir}/lib/" || die "Failed to copy static library to release directory"
[[ -f libframebuffet_dll_shim.a ]] && (cp -a libframebuffet_dll_shim.a "${release_dir}/lib/" || die "Failed to copy plugin-side DLL shim to release directory")

mkdir -p "${release_dir}/include" || die "Failed to create release directory include"
cp -a "${src_dir}"/*.h "${release_dir}/include/" || die "Failed to copy headers to release directory"

echo Build complete.
