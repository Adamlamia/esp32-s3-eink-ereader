"""Passive serial monitor: read COM7 for N seconds WITHOUT resetting the board.

Use this to watch runtime logs (e.g. button diagnostics) while pressing the
physical button. Unlike capture_boot.py this does NOT pulse DTR/RTS, so the
running firmware keeps its state.
"""
import sys
import time

import serial  # pyserial ships with the PlatformIO python env

PORT = "COM7"
BAUD = 115200
SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 30


def main() -> int:
    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        deadline = time.time() + SECONDS
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
