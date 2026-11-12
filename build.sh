#!/bin/sh
# Build isaaclua as an extra NSO (subsdk8) for The Binding of Isaac: Repentance (010021C000B6A000), 1.7.9b.
# Needs clang + ld.lld (aarch64 target), llvm-readelf, Python 3.
set -e
cd "$(dirname "$0")"
# DEBUG=1: the autotest build (src/autotest.lua, version "<v>-debug") -> out/debug/subsdk8
if [ "$DEBUG" = 1 ]; then OUT="${OUT:-out/debug/subsdk8}"; DBG="-DISAACLUA_DEBUG"; AUTOTEST="src/autotest.lua src/remote.lua"; REMOTE=remote
else OUT="${OUT:-out/subsdk8}"; DBG=""; AUTOTEST=""; REMOTE=""; fi
mkdir -p build/lua "$(dirname "$OUT")"
FLAGS="--target=aarch64-none-elf -march=armv8-a -mtune=cortex-a57 -fPIC -ffreestanding -fno-stack-protector \
 -funwind-tables -fvisibility=hidden -mno-outline-atomics -fno-math-errno -O2 -Wall"
CFLAGS="$FLAGS -std=gnu11 -isystem libc/include -Ilua -Ibuild -DLUA_COMPAT_5_2 $DBG"
cat src/enums.lua src/prelude.lua src/api.lua src/api_gen.lua src/api_post.lua src/enums_docs.lua src/repplus.lua $AUTOTEST src/console.lua > build/prelude_all.lua
python3 -I tools/embed.py build/prelude_all.lua build/prelude_lua.h kPreludeLua
OBJS=""
for f in lua/*.c; do
  o=build/lua/$(basename "$f" .c).o
  clang $CFLAGS -Wno-string-plus-int -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
clang $CFLAGS -c libc/libc.c -o build/libc.o
clang $CFLAGS -Wextra -Wno-unused-parameter -c src/main.c -o build/main.o
clang $CFLAGS -Wextra -c src/api.c -o build/api.o
clang $CFLAGS -Wextra -c src/sprite.c -o build/sprite.o
clang $CFLAGS -Wextra -c src/keyboard.c -o build/keyboard.o
for f in native modfs font $REMOTE; do clang $CFLAGS -Wextra -c src/$f.c -o build/$f.o; done
clang --target=aarch64-none-elf -c src/start.s -o build/start.o
ld.lld -shared -T link.ld --hash-style=sysv --build-id=sha1 --eh-frame-hdr -init=isaaclua_init \
  -z norelro -z max-page-size=0x1000 -Bsymbolic --gc-sections \
  -o build/isaaclua.elf build/start.o build/main.o build/api.o build/sprite.o build/keyboard.o build/native.o build/modfs.o build/font.o ${REMOTE:+build/remote.o} build/libc.o $OBJS
python3 -I tools/check_imports.py build/isaaclua.elf tools/allowed_imports.txt
python3 -I tools/elf2nso.py build/isaaclua.elf "$OUT"
