#!/bin/bash

set -Eeo pipefail

cd "${0%/*}"

pinned_version=$(../find_latest_fixed_tag.py --print-only-variable-part --prefix jammy- ubuntu)
build_date=$(date +'%y%m%d')
build_name="framebuffet-build-ubuntu22.04"

tag_name="${TAG_PREFIX}${build_name}:${build_date}${TAG_SUFFIX}"

#              1         2         3         4         5         6         7
#     123456789012345678901234567890123456789012345678901234567890123456789012
echo "------------------------ STARTING DOCKER BUILD -------------------------"
echo "pinned_version:  ${pinned_version}"
echo "Docker tag name: ${tag_name}"
echo "Additional args: $@"
echo ""
echo "build_date:      ${build_date}"
echo "build_name:      ${build_name}"
echo "TAG_PREFIX:      ${TAG_PREFIX}"
echo "TAG_SUFFIX:      ${TAG_SUFFIX}"
echo "------------------------------------------------------------------------"

echo ./build.sh --build-arg BASE_PINNED_VERSION=${pinned_version} --build-arg DOCKER_BUILD_ID=${build_date} -t ${tag_name} $@ .
./build.sh --build-arg BASE_PINNED_VERSION=${pinned_version} --build-arg DOCKER_BUILD_ID=${build_date} -t ${tag_name} $@ .
