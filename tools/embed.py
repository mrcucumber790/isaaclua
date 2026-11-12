#!/usr/bin/env python3
"""embed.py <in> <out.h> <name>: a file as `static const unsigned char name[]` (NUL-terminated)."""
import sys
data = open(sys.argv[1], "rb").read() + b"\0"
rows = [",".join(str(b) for b in data[i:i + 32]) for i in range(0, len(data), 32)]
open(sys.argv[2], "w").write(f"/* generated from {sys.argv[1].split('/')[-1]} */\nstatic const unsigned char {sys.argv[3]}[] = {{\n" + ",\n".join(rows) + "\n};\n")
