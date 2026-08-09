#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}/../.."
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

function die {
    echo $@
    exit 1
}

source ../../_build_target.sh || die "Failed to include build target script"

if [[ "${BUILD_TARGET}" != "windows" ]]; then
    echo "Capturing from WinRT/DXGI is only relevant for Windows targets; skipping build"
    exit 0
fi

num_jobs=$(( $NUM_CPUS + 1 ))

[[ -d "${build_dir}" ]] && rm -Rf "${build_dir}"
mkdir -p "${build_dir}"

[[ -d "${release_dir}" ]] && rm -Rf "${release_dir}"
mkdir -p "${release_dir}"

## BUILD
cd "${build_dir}"

cmake .. || die "CMake failed"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    MSYS_NO_PATHCONV=1 msbuild.exe fbsource_capture_winrt_dxgi.vcxproj /t:Build /p:Configuration=Release || die "msbuild dynamic artifact failed"
    MSYS_NO_PATHCONV=1 msbuild.exe fbsource_capture_winrt_dxgi_static.vcxproj /t:Build /p:Configuration=Release || die "msbuild static artifact failed"
else
    # FIXME: figure out if we can cross-compile WinRT stuff or at least compile with something else that works on Windows
    echo "!!! WARNING: Building with anything other than Visual Studio has not been tested and may fail. !!!"
    make -j$num_jobs || die "make failed"
fi

## COPY
mkdir -p "${release_dir}" || die "Failed to create release directory lib (C1)"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    cp -a Release/* "${release_dir}/" || die "Failed to copy modules to release directory (VS1)"
else
    cp -a fbsource_capture_winrt_dxgi*.${BUILD_TARGET_DYNLIB_EXT}* "${release_dir}/" || die "Failed to copy modules to release directory (STD1)"
    cp -a fbsource_capture_winrt_dxgi.a "${release_dir}/" || die "Failed to copy static library to release directory (STD2)"
fi

echo Build complete.
