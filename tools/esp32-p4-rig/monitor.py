#!/usr/bin/env python3
"""Interactive serial console for the ESP32-P4 board.

Streams the board output and forwards every keypress immediately (no Enter).
Reconnects automatically if the board drops off USB, which this board does
when the motors kick and the 5 V rail sags.

Usage:
  ./monitor.py                       # /dev/ttyACM0 @ 115200
  ./monitor.py /dev/ttyACM0 115200
  ./monitor.py -t                    # timestamp every line
  ./monitor.py -l run.log            # also append the stream to a file

Keys (ESC test firmware): g=all  t=sequence  1-4=single  s=stop  l=log  c=clear
Keys (GPS firmware):      r=raw GPS on/off    b=next baud
Ctrl+C quits.
"""
import argparse
import select
import sys
import termios
import time
import tty

import serial

HELP = ("keys: g=all  t=sequence  1-4=single  s=stop  l=log  c=clear"
        "   |   GPS fw: r=raw  b=next baud")


def parse_args():
    ap = argparse.ArgumentParser(
        description="Interactive serial console with auto-reconnect.")
    ap.add_argument("port", nargs="?", default="/dev/ttyACM0")
    ap.add_argument("baud", nargs="?", type=int, default=115200)
    ap.add_argument("-t", "--timestamps", action="store_true",
                    help="prefix each line with a timestamp")
    ap.add_argument("-l", "--log", metavar="FILE",
                    help="append received data to FILE")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="suppress banner and key help")
    return ap.parse_args()


class Console:
    def __init__(self, args):
        self.args = args
        self.ser = None
        self.log = open(args.log, "a", buffering=1) if args.log else None
        self.line_start = True
        self.raw = bytearray()

    def connect(self):
        try:
            self.ser = serial.Serial(self.args.port, self.args.baud, timeout=0)
        except Exception as exc:  # noqa: BLE001
            self.ser = None
            self.out(f"[waiting for {self.args.port}: {exc}]\n", to_board=False)
            return False
        if not self.args.quiet:
            self.out(f"--- {self.args.port} @ {self.args.baud}, Ctrl+C to quit ---\n",
                     to_board=False)
        return True

    def disconnect(self):
        try:
            if self.ser:
                self.ser.close()
        except Exception:  # noqa: BLE001
            pass
        self.ser = None

    def out(self, text, to_board=True):
        if self.args.timestamps and to_board:
            text = self.stamp(text)
        sys.stdout.write(text)
        sys.stdout.flush()
        if self.log:
            self.log.write(text)
            if not text.endswith("\n"):
                self.log.flush()

    def stamp(self, text):
        """Prefix each new line with [HH:MM:SS.mmm]."""
        parts = text.split("\n")
        result = ""
        for i, part in enumerate(parts):
            if i:
                result += "\n"
            if part == "" and i:
                continue
            prefix = "[%s] " % time.strftime("%H:%M:%S")
            if i == 0 and not self.line_start:
                result += part
            else:
                result += prefix + part
            self.line_start = False
        if text.endswith("\n"):
            self.line_start = True
        return result

    def pump(self, stdin_fd, interactive):
        while True:
            if self.ser is None:
                if not self.connect():
                    time.sleep(0.5)
                    continue

            watch = [self.ser] + ([stdin_fd] if interactive else [])
            try:
                ready, _, _ = select.select(watch, [], [], 0.05)
            except (OSError, ValueError):
                self.out("\n[usb dropped, reconnecting...]\n", to_board=False)
                self.disconnect()
                continue

            if self.ser in ready:
                try:
                    data = self.ser.read(4096)
                except Exception:  # noqa: BLE001
                    self.out("\n[usb dropped, reconnecting...]\n", to_board=False)
                    self.disconnect()
                    continue
                if data:
                    self.out(data.decode("ascii", "replace"))

            if interactive and stdin_fd in ready:
                key = sys.stdin.read(1)
                if key:
                    try:
                        self.ser.write(key.encode())
                    except Exception:  # noqa: BLE001
                        self.out("\n[usb dropped, reconnecting...]\n", to_board=False)
                        self.disconnect()


def main():
    args = parse_args()
    interactive = sys.stdin.isatty()
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd) if interactive else None

    console = Console(args)
    if not args.quiet:
        print(HELP)
        if not interactive:
            print("(stdin is not a terminal: output only, keys disabled)")

    try:
        if interactive:
            tty.setcbreak(fd)
        console.pump(fd, interactive)
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        if interactive and saved:
            termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        console.disconnect()
        if console.log:
            console.log.close()


if __name__ == "__main__":
    main()
