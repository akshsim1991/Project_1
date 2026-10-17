#!/usr/bin/env python3
"""Generates res/app.ico: a circle crossed by a line, with snap points, on a
blue rounded square (a drafting motif for Feather CAD).

Pure Python (no Pillow). Each size is drawn with 4x4 supersampling and
stored as a PNG inside the .ico container.
Run from the FeatherCAD directory:  python tools/make_icon.py
"""
import math
import struct
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
BG_TOP = (30, 110, 200)
BG_BOTTOM = (16, 64, 140)
FG = (255, 255, 255)
DOT = (255, 190, 50)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


CENTRE = (0.43, 0.57)
RADIUS = 0.25
LINE = (0.17, 0.83, 0.85, 0.17)
DOTS = [CENTRE, (LINE[0], LINE[1]), (LINE[2], LINE[3])]


def shade(u, v):
    if not inside_round_rect(u, v, 0.02, 0.02, 0.98, 0.98, 0.2):
        return None
    for (dx, dy) in DOTS:
        if abs(u - dx) <= 0.06 and abs(v - dy) <= 0.06:
            return DOT
    if abs(math.hypot(u - CENTRE[0], v - CENTRE[1]) - RADIUS) < 0.035:
        return FG
    if seg_dist(u, v, *LINE) < 0.035:
        return FG
    t = v
    return tuple(int(BG_TOP[i] * (1 - t) + BG_BOTTOM[i] * t) for i in range(3))


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
