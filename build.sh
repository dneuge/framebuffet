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
./build.sh || die "Building framework failed"
echo

echo "=== Building dummy source ==="
cd "${script_dir}/fbsources/dummy"
./build.sh || die "Building dummy source failed"

echo "=== Building source viewer ==="
cd "${script_dir}/tools/fbsourceviewer"
./build.sh || die "Building source viewer failed"
