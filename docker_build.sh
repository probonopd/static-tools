#!/bin/sh

set -ex

# Build using Docker Alpine container with proper networking
# This avoids chroot network isolation issues

ALPINE_VERSION=3.18

docker run --rm \
    --network=host \
    -v "$(pwd)/src:/src:ro" \
    -v "$(pwd)/patches:/patches:ro" \
    -v "$(pwd)/build.sh:/build.sh:ro" \
    -v "$(pwd)/out:/out" \
    --platform "linux/${ARCHITECTURE}" \
    "alpine:${ALPINE_VERSION}" \
    /bin/sh -ex /build.sh
