#!/usr/bin/env python3
"""Generates res/app.ico (a white page on a red rounded square).

Pure Python (no Pillow needed). Each size is drawn with 4x4 supersampling
and stored as a PNG inside the .ico container (supported since Vista).
Run from the FeatherPDF directory:  python tools/make_icon.py
"""
import struct
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
BG = (214, 57, 44)
PAGE = (255, 255, 255)
FOLD = (246, 196, 190)
LINE = (214, 57, 44)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def shade(u, v):
    """Colour (with alpha) of the unit-square point (u, v)."""
    if not inside_round_rect(u, v, 0.02, 0.02, 0.98, 0.98, 0.2):
        return None
    # Page with a folded top-right corner.
    px0, py0, px1, py1, fold = 0.26, 0.16, 0.74, 0.84, 0.16
    if px0 <= u <= px1 and py0 <= v <= py1:
        du, dv = u - (px1 - fold), v - py0
        if du > 0 and dv < fold:
            if du > dv:
                return BG  # cut-away corner
            return FOLD
        # text lines
        for i, ly in enumerate((0.40, 0.50, 0.60, 0.70)):
            right = 0.66 if i % 2 == 0 else 0.58
            if 0.34 <= u <= right and ly <= v <= ly + 0.045:
                return LINE
        return PAGE
    return BG


def render(size):
    ss = 4
    rows = []
    for y in range(size):
        row = bytearray([0])  # PNG filter: none
        for x in range(size):
            r = g = b = a = 0
            for sy in range(ss):
                for sx in range(ss):
                    c = shade((x + (sx + 0.5) / ss) / size, (y + (sy + 0.5) / ss) / size)
                    if c:
                        r += c[0]; g += c[1]; b += c[2]; a += 255
            n = ss * ss
            if a:
                row += bytes((r * 255 // a, g * 255 // a, b * 255 // a, a // n))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


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
