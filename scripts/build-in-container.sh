#!/bin/sh
# Runs inside debian:trixie (arm64 or amd64), repo mounted at /w: a gamepad-only SDL3 + sdl3probe in dist/.
# Links against nothing newer than MAX_GLIBC (Raspberry Pi OS / Debian trixie ship 2.41).
set -eu

SDL_VERSION=${SDL_VERSION:-3.4.18}
SDL_SHA256=${SDL_SHA256:-9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3}
MAX_GLIBC=${MAX_GLIBC:-2.41}
ARCH=$(dpkg --print-architecture)
NAME=sdl3-$SDL_VERSION-trixie-$ARCH
P=/opt/sdl3-$SDL_VERSION

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config libudev-dev ca-certificates curl file >/dev/null

cd /tmp
curl -fsSL -o sdl.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
echo "$SDL_SHA256  sdl.tar.gz" | sha256sum -c -
tar xzf sdl.tar.gz

# Headless: no video/audio/X11/Wayland; only what a gamepad needs. HIDAPI goes through hidraw (no libusb), and
# libudev is dlopen'd at runtime for hotplug.
cmake -S "SDL3-$SDL_VERSION" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
    -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DSDL_AUDIO=OFF -DSDL_VIDEO=OFF -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
    -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_HIDAPI=ON -DSDL_HIDAPI_JOYSTICK=ON -DSDL_HIDAPI_LIBUSB=OFF \
    -DSDL_SENSOR=ON -DSDL_POWER=ON -DSDL_LIBUDEV=ON -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF \
    >cmake.log
cmake --build build -j"$(nproc)" >build.log
cmake --install build >/dev/null
rm -rf "$P/lib/cmake" "$P/lib/pkgconfig"

mkdir -p "$P/bin"
# $ORIGIN is for the linker, not the shell.
# shellcheck disable=SC2016
cc -O2 -Wall -Wextra -o "$P/bin/sdl3probe" /w/src/sdl3probe.c -I"$P/include" -L"$P/lib" -lSDL3 \
    -Wl,-rpath,'$ORIGIN/../lib'

file "$P/bin/sdl3probe" "$P"/lib/libSDL3.so.0.*
echo "== NEEDED"
objdump -p "$P/lib/libSDL3.so.0" "$P/bin/sdl3probe" | grep NEEDED | sort -u
glibc=$(objdump -T "$P/lib/libSDL3.so.0" "$P/bin/sdl3probe" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
echo "== newest glibc symbol: $glibc (limit $MAX_GLIBC)"
if [ "$(printf '%s\n%s\n' "$glibc" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
    echo "error: needs glibc $glibc, newer than $MAX_GLIBC" >&2
    exit 1
fi
"$P/bin/sdl3probe" --wait 0.2 | head -3

mkdir -p /w/dist
tar -C /opt -czf "/w/dist/$NAME.tar.gz" "sdl3-$SDL_VERSION"
(cd /w/dist && sha256sum "$NAME.tar.gz" >"$NAME.tar.gz.sha256")
ls -la /w/dist
