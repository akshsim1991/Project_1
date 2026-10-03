#!/usr/bin/env python3
"""Generates res/app.ico: a white clipboard on a blue rounded square.

Pure Python (no Pillow). Each size is drawn with 4x4 supersampling and
stored as a PNG inside the .ico container.
Run from the ClipboardManager directory:  python tools/make_icon.py
"""
import struct
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
BG = (0, 95, 184)
FG = (255, 255, 255)
LINE = (120, 170, 220)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def shade(u, v):
    """A white clipboard with a clip and lines of text, on a blue rounded square."""
    if not inside_round_rect(u, v, 0.02, 0.02, 0.98, 0.98, 0.2):
        return None
    # the clip at the top
    if inside_round_rect(u, v, 0.36, 0.12, 0.64, 0.27, 0.05):
        hole = inside_round_rect(u, v, 0.45, 0.155, 0.55, 0.20, 0.02)
        return BG if hole else FG
    board = inside_round_rect(u, v, 0.22, 0.18, 0.78, 0.88, 0.07)
    if not board:
        return BG
    # lines of text on the board
    for y0, x1 in ((0.38, 0.66), (0.50, 0.66), (0.62, 0.58), (0.74, 0.50)):
        if y0 <= v <= y0 + 0.05 and 0.32 <= u <= x1:
            return LINE
    return FG


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
