#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

echo "=== Building framework ==="
cd "${script_dir}/framework"
./build.sh $@ || die "Building framework failed"
echo

echo "=== Building dummy source ==="
cd "${script_dir}/fbsources/dummy"
./build.sh $@ || die "Building dummy source failed"
echo

echo "=== Building libvnc source ==="
cd "${script_dir}/fbsources/libvnc"
./build.sh $@ || die "Building libvnc source failed"
echo

echo "=== Building crop source ==="
cd "${script_dir}/fbsources/crop"
./build.sh $@ || die "Building crop source failed"
echo

echo "=== Building merge source ==="
cd "${script_dir}/fbsources/merge"
./build.sh $@ || die "Building merge source failed"
echo

echo "=== Building capture X11 source ==="
cd "${script_dir}/fbsources/capture_x11"
./build.sh $@ || die "Building capture X11 source failed"
echo

echo "=== Building source viewer ==="
cd "${script_dir}/tools/fbsourceviewer"
./build.sh $@ || die "Building source viewer failed"
