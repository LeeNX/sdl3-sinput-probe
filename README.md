# sdl3-sinput-probe

A headless, gamepad-only build of [SDL3](https://github.com/libsdl-org/SDL) plus `sdl3probe`, a small tool that
prints what SDL3 sees of a gamepad. It was made to test [SInput](https://github.com/HandHeldLegend/SInput-HID)
gamepads over Bluetooth LE on a Raspberry Pi, where the distro's SDL3 is too old (Debian trixie ships 3.2.10; the
SInput HIDAPI driver arrived in 3.4.0).

- **Pinned:** SDL 3.4.18, with the source tarball's sha256 checked before building.
- **Self-contained:** the tarball unpacks anywhere. `sdl3probe` finds `libSDL3.so.0` via `$ORIGIN/../lib`, and needs
  only the system `libc`/`libm` (newest symbol GLIBC 2.38; the build fails if that goes past 2.41). `libudev.so.1`
  is loaded at runtime. Nothing is installed system-wide.
- **Headless:** no video, audio, X11 or Wayland. Joystick, gamepad, haptic, HIDAPI (hidraw), sensor and power only.

## Get it

Download `sdl3-3.4.18-trixie-arm64.tar.gz` (Raspberry Pi 3/4/5 on 64-bit Raspberry Pi OS) or the amd64 one from
[Releases](../../releases), check it against `SHA256SUMS`, and unpack:

```sh
tar -C ~/sdl3 -xzf sdl3-3.4.18-trixie-arm64.tar.gz
~/sdl3/sdl3-3.4.18/bin/sdl3probe --wait 10
```

Or build it yourself with podman or docker. On Apple silicon or another arm64 host the arm64 build runs natively;
other arches run under emulation and are slower.

```sh
./build.sh arm64      # -> dist/sdl3-3.4.18-trixie-arm64.tar.gz (+ .sha256)
./build.sh amd64
SDL_VERSION=3.4.20 SDL_SHA256=<sha256 of SDL3-3.4.20.tar.gz> ./build.sh arm64   # another SDL release
```

## sdl3probe

```text
sdl3probe [--wait S] [--events S] [--rumble LO HI MS] [--led R G B] [--player N] [--no-hidapi]
```

It waits up to `--wait` seconds for a gamepad, then prints one `key=value` line per fact:

- `driver`: `hidapi` when the device came through `/dev/hidraw*` (SDL's own driver, the SInput one here), or `evdev`
  when SDL fell back to the kernel's input device
- name, VID/PID, gamepad type, connection, power and battery
- `cap.*`: rumble, trigger rumble, RGB/mono/player LED
- buttons and axes present, starting axis state (`state.*`), touchpads, accel/gyro and their rates, the mapping string

`--rumble`, `--led` and `--player` send those to the device. `--events S` prints input events for S seconds
(`ev t=<ms> button|axis|touch|sensor ...`, sensors throttled to 4 Hz). `--no-hidapi` turns off SDL's HIDAPI drivers
to compare with the evdev path.

HIDAPI needs read/write access to the device's `/dev/hidraw*` node. Without it SDL silently falls back to evdev, so
check `driver=` first.

## tools/sinput_try.py

Drives an [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad) board running the
[ESP32-BLE-Gamepad-HIL](https://github.com/LeeNX/ESP32-BLE-Gamepad-HIL) `hil_runner` firmware (`sinput` profile)
over its serial console while `sdl3probe` watches. It covers buttons, hat, sticks, triggers, IMU and touchpad. It also
reads back what the probe's rumble, LED and player-index calls delivered to the board. Pair the board with the host
first; `--connect MAC` reconnects it.

```sh
pip install pyserial
python3 tools/sinput_try.py /dev/ttyUSB0 ~/sdl3/sdl3-3.4.18/bin/sdl3probe [--no-hidapi] [--connect MAC]
```

## Results so far

2026-10-21 on a Raspberry Pi 3B+ (Debian 13.7, kernel 6.18, BlueZ 5.82), with an ESP32-C3 running `hil_runner`
`sinput` firmware (library at 80a0d7cb) over BLE. Full logs are in
[results/2026-10-21-esp32c3](results/2026-10-21-esp32c3).

| | HIDAPI (SDL's SInput driver) | evdev (`--no-hidapi`, linux-hid-sinput loaded) |
|---|---|---|
| Path | `/dev/hidraw0`, wireless | `/dev/input/event3` |
| Buttons | 26, incl. misc1–6, 4 paddles, touchpad click | 19, no misc or touchpad click |
| Sticks, triggers, hat | ✅ | ✅ |
| Touchpad | ✅ 1 pad, 2 fingers, x/y/pressure | — |
| Accel / gyro | ✅ 200 Hz, values match what was sent | — |
| Rumble | ✅ device got left=200 right=40 | ✅ |
| RGB LED | ✅ | — |
| Player LED | ✅ player index 2 → device LED 3 | — |
| Battery | ❌ `unknown` (corner case 2) | `unknown` |

### Corner case 1: a released trigger reads half-pulled

SDL's SInput driver treats the trigger's int16 minimum (−32768) as released and maps the full range to 0..32767.
The library sends the trigger value as set, and both the library and `hil_runner` use 0 for "released". So after
`AXIS rx 32767` then `AXIS rx 0`, SDL reports `lefttrigger 16383`. Every SDL3 app sees about 50% trigger at rest
once the trigger has moved. At startup SDL's own zeroed state hides it (`state.lefttrigger=0`) until the first
change. The evdev path shows the same.

### Corner case 2: the battery level never reaches SDL

SDL only reports power when the SInput plug-status byte is known (no battery, charging, charged or on battery).
Unless charging or discharging state has been set, the library sends plug status *unknown* with the level
(`BATTERY 77`), and SDL drops the level: `power=unknown battery=-1`. The linux-hid-sinput kernel driver does show
capacity 77.

## Layout

```text
build.sh                       container wrapper (podman/docker): ./build.sh [arm64|amd64]
scripts/build-in-container.sh  the build: fetch + verify SDL, headless cmake, sdl3probe, glibc check, tarball
src/sdl3probe.c                the probe
tools/sinput_try.py            hil_runner + sdl3probe driver
results/                       logs from runs on real hardware
.github/workflows/build.yml    arm64 + amd64 on native runners; tags v* publish a release
```
