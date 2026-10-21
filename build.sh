#!/bin/sh
# Build dist/sdl3-<version>-trixie-<arch>.tar.gz in a debian:trixie container (podman, or docker).
#   ./build.sh [arm64|amd64]     default: arm64 (Raspberry Pi 3/4/5 on 64-bit Raspberry Pi OS)
set -eu
ARCH=${1:-arm64}
ENGINE=${ENGINE:-$(command -v podman >/dev/null && echo podman || echo docker)}
cd "$(dirname "$0")"
exec "$ENGINE" run --rm --platform "linux/$ARCH" -v "$PWD:/w" \
    -e SDL_VERSION -e SDL_SHA256 -e MAX_GLIBC \
    docker.io/library/debian:trixie sh /w/scripts/build-in-container.sh
