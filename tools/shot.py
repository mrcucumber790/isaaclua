#!/usr/bin/env python3
"""shot() answer from the relay ("IMG <w> <h>\\n<base64 RGB>") -> PNG.   python3 shot.py answer.txt out.png"""
import base64, struct, sys, zlib
text = open(sys.argv[1]).read()
text = text[text.index("IMG "):]
head, _, data = text.partition("\n")
w, h = map(int, head.split()[1:3])
rgb = base64.b64decode(data.strip())
raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                              chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
