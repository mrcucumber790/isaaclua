#!/usr/bin/env python3
"""Convert a linked aarch64 ELF (3 PT_LOAD: RX, R, RW) into an uncompressed NSO.
usage: elf2nso.py <in.elf> <out.nso>
"""
import hashlib, struct, sys

e = open(sys.argv[1], "rb").read()
assert e[:4] == b"\x7fELF" and e[4] == 2
phoff = struct.unpack_from("<Q", e, 0x20)[0]
phentsize, phnum = struct.unpack_from("<HH", e, 0x36)
shoff = struct.unpack_from("<Q", e, 0x28)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", e, 0x3A)

loads = []
for i in range(phnum):
    typ, flags, off, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", e, phoff + i * phentsize)
    if typ == 1:
        loads.append((flags, off, vaddr, filesz, memsz))
assert len(loads) == 3, loads
(tf, to, tv, ts, tm), (rf, ro_, rv, rs, rm), (df, do, dv, ds, dm) = loads
assert tf == 5 and rf == 4 and df == 6
assert tv == 0 and rv % 0x1000 == 0 and dv % 0x1000 == 0

text = e[to:to + ts].ljust(rv - tv, b"\0")
ro = e[ro_:ro_ + rs].ljust(dv - rv, b"\0")
data = e[do:do + ds]
bss = (dv + dm) - (dv + ds)
bss = (bss + 0xFFF) & ~0xFFF if bss else 0

build_id = b"\0" * 32
shstr_off = struct.unpack_from("<Q", e, shoff + shstrndx * shentsize + 0x18)[0]
dynstr = dynsym = (0, 0)
for i in range(shnum):
    name, typ, flags, addr, off, size = struct.unpack_from("<IIQQQQ", e, shoff + i * shentsize)
    nm = e[shstr_off + name:e.index(b"\0", shstr_off + name)]
    if nm == b".note.gnu.build-id":
        namesz, descsz = struct.unpack_from("<II", e, off)
        start = off + 12 + ((namesz + 3) & ~3)
        build_id = e[start:start + descsz][:32].ljust(32, b"\0")
    elif nm == b".dynstr":
        dynstr = (addr - rv, size)
    elif nm == b".dynsym":
        dynsym = (addr - rv, size)

hdr = bytearray(0x100)
struct.pack_into("<4sIII", hdr, 0, b"NSO0", 0, 0, 0)  # flags 0: uncompressed, no hash check
toff = 0x100
roff = toff + len(text)
doff = roff + len(ro)
struct.pack_into("<III", hdr, 0x10, toff, 0, len(text))
struct.pack_into("<III", hdr, 0x20, roff, rv, len(ro))
struct.pack_into("<III", hdr, 0x30, doff, dv, len(data))
struct.pack_into("<I", hdr, 0x3C, bss)
hdr[0x40:0x60] = build_id
struct.pack_into("<III", hdr, 0x60, len(text), len(ro), len(data))
struct.pack_into("<II", hdr, 0x90, *dynstr)
struct.pack_into("<II", hdr, 0x98, *dynsym)
hdr[0xA0:0xC0] = hashlib.sha256(text).digest()
hdr[0xC0:0xE0] = hashlib.sha256(ro).digest()
hdr[0xE0:0x100] = hashlib.sha256(data).digest()
open(sys.argv[2], "wb").write(bytes(hdr) + text + ro + data)
print(f"text 0x{len(text):x} ro 0x{len(ro):x}@0x{rv:x} data 0x{len(data):x}@0x{dv:x} bss 0x{bss:x} id {build_id[:8].hex()}")
