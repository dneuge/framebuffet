#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

./fetch-libs.sh || die "Fetching dependencies failed"
./build-libs.sh $@ || die "Building dependencies failed"
./build.sh $@ || die "Build failed"
