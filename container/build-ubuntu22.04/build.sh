#!/bin/bash

set -Eeo pipefail

if which podman >/dev/null 2>&1; then
    podman build $@
elif which docker >/dev/null 2>&1; then 
    docker build $@
else
    echo "No supported build tool found."
    exit 1
fi
