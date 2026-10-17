#!/usr/bin/env python3
"""Generates the legacy (pre-Android 8) launcher PNGs in app/src/main/res/mipmap-*.

Reuses the drawing of the Windows icon (FeatherPDF/tools/make_icon.py): a white
page on a red rounded square; the round variant clips it to a circle.
Pure Python, no Pillow.  Run from FeatherPDF-Android:  python tools/make_icons.py
"""
import os
import struct
import sys

sys.dont_write_bytecode = True
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "FeatherPDF", "tools"))
import make_icon  # noqa: E402

DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def png(size, round_):
    ss = 4
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            r = g = b = a = 0
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + 0.5) / ss) / size
                    v = (y + (sy + 0.5) / ss) / size
                    if round_:
                        if (u - 0.5) ** 2 + (v - 0.5) ** 2 > 0.48 ** 2:
                            continue
                        c = make_icon.shade(u, v) or make_icon.BG
                    else:
                        c = make_icon.shade(u, v)
                    if c:
                        r += c[0]; g += c[1]; b += c[2]; a += 255
            n = ss * ss
            row += bytes((r * 255 // a, g * 255 // a, b * 255 // a, a // n)) if a else bytes(4)
        rows.append(bytes(row))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def main():
    for d, size in DENSITIES.items():
        out = os.path.join("app", "src", "main", "res", "mipmap-" + d)
        os.makedirs(out, exist_ok=True)
        for name, round_ in (("ic_launcher.png", False), ("ic_launcher_round.png", True)):
            with open(os.path.join(out, name), "wb") as f:
                f.write(png(size, round_))


if __name__ == "__main__":
    main()
