#!/usr/bin/env python3
"""png2pcx.py <mods dir>: give every mod sprite a .pcx copy the Switch game can load.

The game loads sprite sheets as .pcx only (Manager::LoadImage turns "x.png" into "x.pcx"), so each
<mod>/resources/**/*.png gets a <same name>.pcx next to it: PCX v5, RLE, 8 bpp x 4 planes RGBA,
premultiplied alpha (the format of the game's own files). Fonts load their .png pages directly
(Font::Load uses libpng), so resources/font/ is left alone. Needs Pillow."""
import os, struct, sys
from PIL import Image


def write_pcx(im, dst):
    im = im.convert("RGBA")
    w, h = im.size
    px = im.tobytes()
    bpl = w + (w & 1)  # bytes per plane line must be even
    hdr = bytearray(128)
    hdr[0:4] = bytes([0x0A, 5, 1, 8])
    struct.pack_into("<6H", hdr, 4, 0, 0, w - 1, h - 1, w, h)
    hdr[64], hdr[65] = 1, 4
    struct.pack_into("<HH", hdr, 66, bpl, 1)
    body = bytearray()
    for y in range(h):
        row = px[y * w * 4:(y + 1) * w * 4]
        planes = [bytearray(bpl) for _ in range(4)]
        for x in range(w):
            r, g, b, a = row[x * 4:x * 4 + 4]
            if a == 0:
                r = g = b = 0
            elif a < 255:
                r, g, b = (r * a + 127) // 255, (g * a + 127) // 255, (b * a + 127) // 255
            planes[0][x], planes[1][x], planes[2][x], planes[3][x] = r, g, b, a
        for pl in planes:
            i = 0
            while i < bpl:
                v, n = pl[i], 1
                while i + n < bpl and pl[i + n] == v and n < 63:
                    n += 1
                if n > 1 or v >= 0xC0:
                    body += bytes([0xC0 | n, v])
                else:
                    body.append(v)
                i += n
    with open(dst, "wb") as f:
        f.write(bytes(hdr) + bytes(body))


def main(mods):
    n = 0
    for mod in sorted(os.listdir(mods)):
        res = os.path.join(mods, mod, "resources")
        for root, dirs, files in os.walk(res):
            rel = os.path.relpath(root, res).replace(os.sep, "/")
            if rel == "font" or rel.startswith("font/"):
                continue
            for f in files:
                if not f.lower().endswith(".png"):
                    continue
                src = os.path.join(root, f)
                dst = src[:-4] + ".pcx"
                if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
                    continue
                write_pcx(Image.open(src), dst)
                n += 1
    print("png2pcx: %d sprite(s) converted" % n)


if __name__ == "__main__":
    main(sys.argv[1])
