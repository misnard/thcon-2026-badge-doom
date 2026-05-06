#!/usr/bin/env python3
"""
Relay host keystrokes to the embedded DOOM badge over the serial console.

Usage:
  scripts/play.py [PORT]
  PORT=/dev/cu.usbserial-XXX scripts/play.py
  (auto-detects /dev/cu.usbserial*, /dev/cu.usbmodem*, /dev/ttyUSB* if not given)

Controls:
  W A S D       move / turn
  F             fire
  E             use / open
  Enter / Esc   menus
  G             cycle OLED gamma
  m             cycle OLED render mode
  [   ]         shift render Y offset
  Ctrl-]        quit relay (does not reach badge)

Holding a key works: the host TTY auto-repeats and each repeated byte extends
the badge-side keydown by ~4 game tics, so movement stays continuous.
"""
import glob
import os
import select
import sys
import termios

BAUD = 115200
QUIT_BYTE = b"\x1d"  # Ctrl-]

BAUD_CONST = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
}


def find_port():
    for pattern in ("/dev/cu.usbserial*", "/dev/cu.usbmodem*", "/dev/ttyUSB*"):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    return None


def open_serial(path, baud):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, _, _, cc = attrs
    iflag = termios.IGNBRK
    oflag = 0
    lflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    speed = BAUD_CONST[baud]
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, cc])
    return fd


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("PORT") or find_port()
    if not port:
        sys.stderr.write("no serial port found; pass one as argv[1] or set PORT=...\n")
        sys.exit(1)

    sfd = open_serial(port, BAUD)
    tin = sys.stdin.fileno()
    saved = termios.tcgetattr(tin)
    raw = termios.tcgetattr(tin)
    raw[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG)
    raw[6][termios.VMIN] = 1
    raw[6][termios.VTIME] = 0
    termios.tcsetattr(tin, termios.TCSADRAIN, raw)

    sys.stderr.write(f"\rconnected: {port} @ {BAUD}  —  Ctrl-] to quit\r\n")
    sys.stderr.flush()

    try:
        out = sys.stdout.buffer
        while True:
            r, _, _ = select.select([tin, sfd], [], [], 0.1)
            if tin in r:
                data = os.read(tin, 64)
                if not data:
                    break
                idx = data.find(QUIT_BYTE)
                if idx >= 0:
                    if idx > 0:
                        os.write(sfd, data[:idx])
                    break
                os.write(sfd, data)
            if sfd in r:
                try:
                    data = os.read(sfd, 256)
                except BlockingIOError:
                    data = b""
                if data:
                    out.write(data)
                    out.flush()
    finally:
        termios.tcsetattr(tin, termios.TCSADRAIN, saved)
        os.close(sfd)
        sys.stderr.write("\r\nbye\r\n")
        sys.stderr.flush()


if __name__ == "__main__":
    main()
