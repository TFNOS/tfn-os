# TFN OS SDK: a picture of all frames of an icon.bin (a check of the icon; holes show as a check pattern).
#   python sdk/tools/icon_png.py sdk/out/dive/lcd349/icon.bin out.png
import struct
import sys
import zlib


def frames(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'TFI1', 'not an icon.bin'
    size, nf, ms, nc = struct.unpack('<HHHH', b[4:12])
    pal = struct.unpack('<%dH' % nc, b[12:12 + 2 * nc])
    p = 12 + 2 * nc
    out = []
    for _ in range(nf):
        ln = struct.unpack('<I', b[p:p + 4])[0]
        p += 4
        q, end, idx = p, p + ln, []
        while q < end:
            c = b[q]
            q += 1
            if c < 128:
                idx += list(b[q:q + c + 1])
                q += c + 1
            else:
                idx += [b[q]] * (257 - c)
                q += 1
        p = end
        out.append(idx[:size * size])
    return size, ms, pal, out


def rgb(v):
    v = ((v >> 8) | (v << 8)) & 0xFFFF
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def png(path, w, h, px):
    raw = b''.join(b'\0' + bytes(c for x in range(w) for c in px[y * w + x]) for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    size, ms, pal, fr = frames(sys.argv[1])
    cols = min(len(fr), 8)
    rows = (len(fr) + cols - 1) // cols
    W, H = cols * (size + 4), rows * (size + 4)
    px = [(40, 40, 48)] * (W * H)
    for i, f in enumerate(fr):
        ox, oy = (i % cols) * (size + 4), (i // cols) * (size + 4)
        for y in range(size):
            for x in range(size):
                v = f[y * size + x]
                px[(oy + y) * W + ox + x] = rgb(pal[v - 1]) if v else ((90, 90, 90) if (x // 8 + y // 8) % 2 else (120, 120, 120))
    png(sys.argv[2], W, H, px)
    print('%s: %d frames of %d px, %d ms, %d colours' % (sys.argv[1], len(fr), size, ms, len(pal)))
