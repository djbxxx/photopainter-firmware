#!/usr/bin/env python3
"""Talk to the PhotoPainter custom firmware over USB serial.

  ppctl.py log [seconds]      print the frame's log (default: until Ctrl-C)
  ppctl.py next               show the next picture
  ppctl.py drive              switch the SD card back to USB drive mode
  ppctl.py status             print battery/charging/drive status
Needs: pip install pyserial
"""
import glob
import sys
import time

import serial

CMDS = {"next": b"\x0e", "drive": b"\x04", "status": b"\x13"}


def port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("PhotoPainter not found (no /dev/cu.usbmodem*)")
    return ports[0]


def follow(s, seconds):
    end = time.time() + seconds if seconds else None
    buf = b""
    while end is None or time.time() < end:
        buf += s.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            print(line.decode(errors="replace").rstrip("\r"), flush=True)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("log", *CMDS):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    with serial.Serial(port(), 115200, timeout=0.2) as s:
        if cmd == "log":
            follow(s, float(sys.argv[2]) if len(sys.argv) > 2 else 0)
        else:
            s.write(CMDS[cmd])
            try:
                follow(s, 2 if cmd == "status" else 1)
            except serial.SerialException:
                pass  # "drive" reconnects USB, which closes the serial port


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
