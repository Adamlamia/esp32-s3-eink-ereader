"""One-shot: reset the ESP32-S3 over COM7 and capture the boot log.

The LilyGo T5 S3 enumerates as native USB-Serial/JTAG. Opening the port does
not reset the chip, so we pulse DTR/RTS (classic reset sequence) and then read
serial output for a few seconds to capture the single boot sequence.
"""
import sys
import time

import serial  # pyserial ships with the PlatformIO python env

PORT = "COM7"
BAUD = 115200
CAPTURE_SECONDS = 16


def reset(ser: "serial.Serial") -> None:
    # Classic ESP reset: EN low briefly (via RTS), IO0 high (via DTR) so the
    # app runs instead of dropping into the ROM bootloader.
    ser.dtr = False   # IO0 -> HIGH  (normal boot)
    ser.rts = True    # EN  -> LOW   (hold in reset)
    time.sleep(0.15)
    ser.rts = False   # EN  -> HIGH  (release -> boot)


def main() -> int:
    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        reset(ser)
        deadline = time.time() + CAPTURE_SECONDS
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
