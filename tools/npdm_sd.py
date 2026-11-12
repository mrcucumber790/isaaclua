#!/usr/bin/env python3
"""Adds the SdCard bit (fs AccessControlBits 1<<21) to the game's main.npdm, so nn::fs::MountSdCard works in the
remote build. Nothing else changes.
Usage: npdm_sd.py <the game's exefs/main.npdm> <out main.npdm>. Only the two FS permission masks change (ACI0
FS access header and ACID FS access control), by that one bit; Atmosphere loads an exefs/main.npdm override like this one, as it
does for skyline/exlaunch mods. The output is derived from the game: never commit it."""
import struct, sys

d = bytearray(open(sys.argv[1], 'rb').read())
assert d[:4] == b'META', 'not an npdm'
aci_off, _ = struct.unpack_from('<II', d, 0x70)
acid_off, _ = struct.unpack_from('<II', d, 0x78)
assert d[aci_off:aci_off + 4] == b'ACI0' and d[acid_off + 0x200:acid_off + 0x204] == b'ACID'
fah_off, _ = struct.unpack_from('<II', d, aci_off + 0x20)
fac_off, _ = struct.unpack_from('<II', d, acid_off + 0x220)
SDCARD = 1 << 21
for where, at in (('ACI0 FS access header', aci_off + fah_off + 4), ('ACID FS access control', acid_off + fac_off + 4)):
    old = struct.unpack_from('<Q', d, at)[0]
    print('%s: 0x%016x -> 0x%016x' % (where, old, old | SDCARD))
    struct.pack_into('<Q', d, at, old | SDCARD)
open(sys.argv[2], 'wb').write(d)
