#!/usr/bin/env python3
"""check_imports.py <elf> <allowed.txt>: every undefined dynamic symbol must be in the allow list,
and the plugin must not export anything (it would interpose the game's own symbols)."""
import subprocess, sys
out = subprocess.run(["llvm-readelf", "--dyn-syms", "-W", sys.argv[1]], capture_output=True, text=True, check=True).stdout
allowed = {l.strip() for l in open(sys.argv[2]) if l.strip() and not l.startswith("#")}
bad, exported, imports = [], [], []
for line in out.splitlines():
    f = line.split()
    if len(f) < 8 or not f[0].rstrip(":").isdigit() or f[7] == "":
        continue
    name, ndx, bind = f[7], f[6], f[4]
    if ndx == "UND":
        imports.append(name)
        if name not in allowed:
            bad.append(name)
    elif bind in ("GLOBAL", "WEAK"):
        exported.append(name)
print(f"{len(imports)} imports, all from the SDK" if not bad else f"NOT ALLOWED: {' '.join(bad)}")
if exported:
    print("EXPORTED:", " ".join(exported))
sys.exit(1 if bad or exported else 0)
