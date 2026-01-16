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
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    MSYS_NO_PATHCONV=1 msbuild.exe framebuffet.vcxproj /t:Build /p:Configuration=Release || die "msbuild framebuffet (fully dynamic) failed"
    MSYS_NO_PATHCONV=1 msbuild.exe framebuffet_static_lib.vcxproj /t:Build /p:Configuration=Release || die "msbuild framebuffet_static_lib (dynamic with static dependencies) failed"
    MSYS_NO_PATHCONV=1 msbuild.exe framebuffet_static.vcxproj /t:Build /p:Configuration=Release || die "msbuild framebuffet_static (only framework) failed"

    # experimental and MinGW command does not translate to MSVC
    #MSYS_NO_PATHCONV=1 msbuild.exe framebuffet_static_complete.vcxproj /t:Build /p:Configuration=Release || die "msbuild framebuffet_static_complete (all-in-one static artifact) failed"

    MSYS_NO_PATHCONV=1 msbuild.exe framebuffet_dll_shim.vcxproj /t:Build /p:Configuration=Release || die "msbuild framebuffet_dll_shim (source module DLL-side shim) failed"
else
    make -j$num_jobs || die "make failed"
fi

## COPY
mkdir -p "${release_dir}/lib" || die "Failed to create release directory lib (C1)"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    cp -a Release/* "${release_dir}/lib/" || die "Failed to copy main artifacts to release directory (VS1)"
else
    cp -a *framebuffet*.${BUILD_TARGET_DYNLIB_EXT}* "${release_dir}/lib/" || die "Failed to copy shared library to release directory (STD1)"
    cp -a libframebuffet.a "${release_dir}/lib/" || die "Failed to copy static library to release directory (STD2)"
    [[ -f libframebuffet_dll_shim.a ]] && (cp -a libframebuffet_dll_shim.a "${release_dir}/lib/" || die "Failed to copy plugin-side DLL shim to release directory (STD3)")
fi

mkdir -p "${release_dir}/include" || die "Failed to create release directory include (C2)"
cp -a "${src_dir}"/*.h "${release_dir}/include/" || die "Failed to copy headers to release directory (C3)"

echo Build complete.
