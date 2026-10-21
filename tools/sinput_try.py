#!/usr/bin/env python3
"""Drive an ESP32-BLE-Gamepad `hil_runner` board (sinput profile) over serial while sdl3probe watches it.

The board must already be paired with this host (bluetoothctl: scan on / pair / trust / connect). --connect MAC
asks bluetoothctl to connect it first. Needs pyserial.

  sinput_try.py <serial port> <sdl3probe> [--connect MAC] [--no-hidapi]

The serial commands (PRESS, AXIS, MOTION, TOUCH, RUMBLE?, ...) are hil_runner's, from
https://github.com/LeeNX/ESP32-BLE-Gamepad-HIL (firmware/src/hil_runner.cpp).
"""

import argparse
import subprocess
import threading
import time

import serial

ap = argparse.ArgumentParser()
ap.add_argument("port")
ap.add_argument("probe")
ap.add_argument("--connect", metavar="MAC")
ap.add_argument("--no-hidapi", action="store_true")
args = ap.parse_args()

if args.connect:
    out = subprocess.run(
        ["bluetoothctl", "connect", args.connect], check=False, capture_output=True, text=True, timeout=30
    )
    print("connect:", out.stdout.strip().splitlines()[-1] if out.stdout.strip() else out.stderr.strip(), flush=True)
    time.sleep(3)

ser = serial.Serial(args.port, 115200, timeout=0.2)


def cmd(c, timeout=2.0):
    """Send one hil_runner command, return its reply line (log lines start with '[')."""
    ser.reset_input_buffer()
    ser.write((c + "\n").encode())
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        line = ser.readline().decode(errors="replace").strip()
        if line and not line.startswith(("[", "HIL hil_runner ready")):
            return line
    return None


for c in ("RESET", "BATTERY 77", "RUMBLE?", "RGB?", "PLED?"):  # the ? queries also clear the received flags
    print(f">> {c}: {cmd(c)}", flush=True)

probe = [
    *(args.probe, "--wait", "8", "--events", "12"),
    *("--rumble", "51200", "10240", "1500"),  # 16-bit; the board should see left=200 right=40
    *("--led", "10", "20", "30", "--player", "2"),
]
if args.no_hidapi:
    probe.append("--no-hidapi")
p = subprocess.Popen(probe, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
ready = threading.Event()


def pump():
    for line in p.stdout:
        print("sdl |", line.rstrip(), flush=True)
        if line.startswith(("player=", "error=")):
            ready.set()
    ready.set()


threading.Thread(target=pump, daemon=True).start()
ready.wait(15)
time.sleep(0.6)
# What the probe's rumble / LED / player-index calls delivered to the board.
for c in ("RUMBLE?", "RGB?", "PLED?"):
    print(f">> {c}: {cmd(c)}", flush=True)
# Press/release pairs: buttons, a special (select), the hat, a stick, a trigger (rx), the right stick, IMU, touchpad.
STEPS = [
    ("PRESS 1", "RELEASE 1"),
    ("PRESS 2", "RELEASE 2"),
    ("SPECIAL PRESS 1", "SPECIAL RELEASE 1"),
    ("HAT 1 1", "HAT 1 0"),
    ("AXIS x 16000", "AXIS x 0"),
    ("AXIS rx 32767", "AXIS rx 0"),
    ("AXIS z 12345", "AXIS z 0"),
    ("MOTION 100 -200 300 -4096 4096 8192", None),
    ("TOUCH 0 1000 -1000 500", "TOUCH 0 0 0 0"),
    ("MOTION 0 0 0 0 0 0", None),
]
for step in STEPS:
    for c in filter(None, step):
        print(f">> {c}: {cmd(c)}", flush=True)
        time.sleep(0.4)
p.wait(20)
print("probe rc", p.returncode, flush=True)
