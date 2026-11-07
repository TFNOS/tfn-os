# Make PNG files (2x) of raw RGB565 frames: python png.py <folder>  (the name ends in _WxH.raw)
import os, re, struct, sys, zlib

folder = sys.argv[1] if len(sys.argv) > 1 else "."
for name in sorted(os.listdir(folder)):
    m = re.search(r"_(\d+)x(\d+)\.raw$", name)
    if not m:
        continue
    w, h = int(m.group(1)), int(m.group(2))
    data = open(os.path.join(folder, name), "rb").read()
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            v = struct.unpack_from(">H", data, (y * w + x) * 2)[0]   # high byte first (the LCD order)
            r, g, b = (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
            row += bytes((r, g, b)) * 2
        rows.append(b"\0" + bytes(row))
        rows.append(b"\0" + bytes(row))
    raw = b"".join(rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * 2, h * 2, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(os.path.join(folder, name[:-4] + ".png"), "wb").write(png)
    print(name[:-4] + ".png")
