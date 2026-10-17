#!/usr/bin/env python3
"""Generates res/app.ico: a fitted curve with data points on a teal rounded square.

Pure Python (no Pillow). Each size is drawn with 4x4 supersampling and
stored as a PNG inside the .ico container.
Run from the CurveForge directory:  python tools/make_icon.py
"""
import math
import struct
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
BG = (0, 112, 140)
FG = (255, 255, 255)
DOT = (255, 196, 64)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def curve(u):
    """An S-shaped fitted curve across the icon."""
    return 0.80 - 0.60 / (1 + math.exp(-11 * (u - 0.5)))


DOTS = [(0.22, curve(0.22) + 0.07), (0.40, curve(0.40) - 0.07), (0.60, curve(0.60) + 0.06), (0.80, curve(0.80) - 0.06)]


def shade(u, v):
    """A white fitted curve with data points around it."""
    if not inside_round_rect(u, v, 0.02, 0.02, 0.98, 0.98, 0.2):
        return None
    for (dx, dy) in DOTS:
        if (u - dx) ** 2 + (v - dy) ** 2 <= 0.055 ** 2:
            return DOT
    if 0.12 <= u <= 0.88:
        # Distance to the curve, measured roughly along the normal.
        c = curve(u)
        slope = (curve(u + 0.001) - curve(u - 0.001)) / 0.002
        if abs(v - c) / math.sqrt(1 + slope * slope) < 0.045:
            return FG
    return BG


def render(size):
    ss = 4
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            r = g = b = a = 0
            for sy in range(ss):
                for sx in range(ss):
                    c = shade((x + (sx + 0.5) / ss) / size, (y + (sy + 0.5) / ss) / size)
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
    images = [render(s) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for s, img in zip(SIZES, images):
        dim = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    with open("res/app.ico", "wb") as f:
        f.write(header + entries + b"".join(images))


if __name__ == "__main__":
    main()
