#!/usr/bin/env python3
"""End-to-end test of the App <-> orion-fc protocol, using the exact framings the
Betaflight App uses:
  - MSP v1:  '$M<' size cmd payload xor
  - MSP v2:  '$X<' flags cmd(le16) size(le16) payload crc8-dvb-s2
  - CLI:     STX + line + LF + ETX
"""
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
results = []


def crc8_dvb_s2(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def msp1(cmd, payload=b""):
    body = bytes([len(payload), cmd]) + payload
    crc = 0
    for b in body:
        crc ^= b
    return b"$M<" + body + bytes([crc])


def msp2(cmd, payload=b""):
    body = bytes([0x00, cmd & 0xFF, cmd >> 8, len(payload) & 0xFF, len(payload) >> 8]) + payload
    return b"$X<" + body + bytes([crc8_dvb_s2(body)])


class Fc:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.15)

    def v1(self, cmd, payload=b"", wait=0.25):
        self.s.reset_input_buffer()
        self.s.write(msp1(cmd, payload))
        time.sleep(wait)
        b = self.s.read(8192)
        j = b.find(b"$M>")
        return b[j + 5: j + 5 + b[j + 3]] if j >= 0 else None

    def v2(self, cmd, payload=b"", wait=0.4):
        self.s.reset_input_buffer()
        self.s.write(msp2(cmd, payload))
        time.sleep(wait)
        b = self.s.read(8192)
        j = b.find(b"$X>")
        if j < 0:
            return None
        n = b[j + 6] | (b[j + 7] << 8)
        return b[j + 8: j + 8 + n]

    def cli(self, line, wait=0.5):
        self.s.reset_input_buffer()
        self.s.write(b"\x02" + line.encode() + b"\n\x03")
        time.sleep(wait)
        txt = self.s.read(8192).decode("ascii", "replace")
        return [l.strip() for l in txt.replace("\x03", "\n").replace("\x02", "\n").splitlines()
                if l.strip() and not l.strip().startswith("#")]


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail else ""))


def main():
    fc = Fc(PORT)
    time.sleep(0.5)

    # --- handshake (App connect sequence) ---
    api = fc.v1(1)
    check("handshake MSP_API_VERSION", api is not None and len(api) == 3,
          f"protocol={api[0]} api={api[1]}.{api[2]}" if api else "no reply")
    var = fc.v1(2)
    check("handshake MSP_FC_VARIANT", var == b"BTFL", var.decode("ascii", "replace") if var else "no reply")
    board = fc.v1(4)
    check("handshake MSP_BOARD_INFO", board is not None and board[:4] == b"ESPF",
          board[:8].hex() if board else "no reply")

    # --- sensors ---
    imu = fc.v1(102)
    ok = imu is not None and len(imu) >= 18
    if ok:
        acc = struct.unpack("<3h", imu[0:6])
        g = (acc[0] ** 2 + acc[1] ** 2 + acc[2] ** 2) ** 0.5 / 2048.0
        check("sensors MSP_RAW_IMU", 0.7 < g < 1.3, f"|a|={g:.2f} g")
    else:
        check("sensors MSP_RAW_IMU", False, "no reply")

    # --- motors (App Motors tab) ---
    fc.v1(214, struct.pack("<4H", 1200, 1200, 1200, 1200))
    time.sleep(0.4)
    m = fc.v1(104)
    motors = struct.unpack("<4H", m[:8]) if m and len(m) >= 8 else (0,)
    check("motors MSP_SET_MOTOR 1200", motors[:4] == (1200, 1200, 1200, 1200), str(motors[:4]))
    fc.v1(214, struct.pack("<4H", 1000, 1000, 1000, 1000))
    time.sleep(0.3)
    m = fc.v1(104)
    check("motors stop", struct.unpack("<4H", m[:8])[:4] == (1000, 1000, 1000, 1000))

    # --- joystick (MSP_SET_RAW_RC -> MSP_RC) ---
    ch = [1700, 1500, 1000, 1500] + [1500] * 12
    fc.v1(200, struct.pack("<16H", *ch))
    time.sleep(0.5)
    rc = fc.v1(105)
    vals = struct.unpack("<16H", rc[:32]) if rc and len(rc) >= 32 else (0,)
    check("joystick MSP_SET_RAW_RC", 1650 <= vals[0] <= 1750, f"roll={vals[0]}")

    # --- waypoints (Flight Plan tab: CLI session) ---
    fc.cli("waypoint clear")
    fc.cli("waypoint insert 0 -6.3600403 106.8211178 2650 500 FLYOVER 0 ORBIT")
    fc.cli("waypoint insert 1 -6.3610000 106.8220000 3000 550 FLYOVER 50 ORBIT")
    lines = fc.cli("waypoint list")
    wps = [l for l in lines if l.startswith("waypoint insert ")]
    check("waypoints insert + list", len(wps) == 2, f"{len(wps)} waypoints")
    fc.cli("waypoint clear")
    check("waypoints clear", len([l for l in fc.cli("waypoint list") if l.startswith("waypoint insert ")]) == 0)

    # --- video (MSP2) ---
    start = fc.v2(0x2F00, b"e2e_clip.mjpeg", wait=3.0)
    check("video start", start == b"\x01", start.hex() if start else "no reply")
    time.sleep(4.0)
    st = fc.v2(0x2F02, wait=1.5)
    ok = st is not None and len(st) >= 7
    frames = struct.unpack("<H", st[1:3])[0] if ok else 0
    check("video status while recording", ok and frames > 0, f"frames={frames}")
    fc.v2(0x2F01, wait=1.5)
    listing = fc.v2(0x2F03, wait=1.5)
    text = listing.decode("ascii", "replace") if listing else ""
    check("video stop + list clips", "e2e_clip.mjpeg" in text, text.strip().replace("\n", " | ")[:80])

    fc.s.close()
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
