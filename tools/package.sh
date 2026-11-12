#!/bin/sh
# SD-card package: exefs/subsdk8 + isaaclua.ini + the ported PC mods -> release/isaaclua_v<version>.zip.
# With "test" (branch test): also the mods being checked -> release-test/isaaclua_v<version>-test.zip.
# Only the latest zip of each is kept (Egor, 2026-10-19).
# With "debug": only the autotest build (build.sh DEBUG=1, out/debug/subsdk8) -> release/isaaclua_v<version>-debug.zip,
# to put over an installed release (Egor, 2026-10-24: a separate debug file).
set -e
cd "$(dirname "$0")/.."
if [ "$1" = remote ]; then
    # the debug build + rom:/isaaclua_remote.ini (host, port, key from the environment) -> isaaclua_v<v>-remote.zip
    V=$(sed -n 's/^#define ISAACLUA_VERSION "\(.*\)"/\1/p' src/version.h)
    T=build/pkg-remote; C=$T/atmosphere/contents/010021C000B6A000
    rm -rf $T && mkdir -p $C/exefs $C/romfs
    cp out/debug/subsdk8 $C/exefs/subsdk8
    # full FS access in the game's npdm, so the relay can write to the SD card (GAME_NPDM = the game's exefs/main.npdm;
    # the patched copy is derived from the game: only in the zip, never committed)
    if [ -n "$GAME_NPDM" ]; then python3 -I tools/npdm_sd.py "$GAME_NPDM" $C/exefs/main.npdm; fi
    printf 'host = %s\nport = %s\nkey = %s\n' "$REMOTE_HOST" "${REMOTE_PORT:-80}" "$REMOTE_KEY" > $C/romfs/isaaclua_remote.ini
    cat > $T/README-REMOTE.txt <<EOF
isaaclua v$V-remote: Egor's build with remote access for Claude. NEVER share it: it holds the relay key.

Install over the normal release isaaclua_v$V.zip: copy atmosphere/ to the SD card root, replacing files.
It replaces only exefs/subsdk8 (debug build), exefs/main.npdm (SD access) and adds romfs/isaaclua_remote.ini;
mods and isaaclua.ini stay. Back to the normal build: copy exefs/subsdk8 from isaaclua_v$V.zip and
delete exefs/main.npdm and romfs/isaaclua_remote.ini.
EOF
    Z=release/isaaclua_v$V-remote.zip
    mkdir -p release && rm -f release/isaaclua_v*-remote.zip
    (cd $T && python3 -I -m zipfile -c ../../$Z atmosphere README-REMOTE.txt)
    python3 -I -m zipfile -l $Z
    exit 0
fi
if [ "$1" = debug ]; then
    V=$(sed -n 's/^#define ISAACLUA_VERSION "\(.*\)"/\1/p' src/version.h)
    T=build/pkg-debug; C=$T/atmosphere/contents/010021C000B6A000
    rm -rf $T && mkdir -p $C/exefs
    cp out/debug/subsdk8 $C/exefs/subsdk8
    cp tools/AUTOTEST.txt $T/AUTOTEST.txt
    Z=release/isaaclua_v$V-debug.zip
    mkdir -p release && rm -f release/isaaclua_v*-debug.zip
    (cd $T && python3 -I -m zipfile -c ../../$Z atmosphere AUTOTEST.txt)
    python3 -I -m zipfile -l $Z
    exit 0
fi
if [ "$1" = repplus ]; then
    # only Repentance+ (Egor, 2026-11-05): the release subsdk8 + the isaac-repplus mod, no other mods
    # -> release/repplus_v<REP+ version>.zip (README-REPPLUS.md = docs/README-REPPLUS.md, the change list too)
    RV=$(sed -n 's/^  local VERSION = "\(.*\)"/\1/p' src/repplus.lua)
    T=build/pkg-repplus; C=$T/atmosphere/contents/010021C000B6A000
    rm -rf $T && mkdir -p $C/exefs $C/romfs/mods
    cp out/subsdk8 $C/exefs/subsdk8
    cp -r test/mods_repplus/isaac-repplus $C/romfs/mods/isaac-repplus
    cp example/isaaclua.ini $C/romfs/isaaclua.ini
    cp docs/README-REPPLUS.md $T/README-REPPLUS.md
    cp docs/REPPLUS_ALL_CHANGES.md $T/REPPLUS_ALL_CHANGES.md
    Z=release/repplus_v$RV.zip
    mkdir -p release && rm -f release/repplus_v*.zip
    (cd $T && python3 -I -m zipfile -c ../../$Z atmosphere README-REPPLUS.md REPPLUS_ALL_CHANGES.md)
    python3 -I -m zipfile -l $Z | tail -3
    exit 0
fi
T=build/pkg; C=$T/atmosphere/contents/010021C000B6A000
rm -rf $T && mkdir -p $C/exefs $C/romfs/mods
cp out/subsdk8 $C/exefs/subsdk8
# real PC mods that run unmodified (test/mods/<name>/ORIGIN.txt says where each comes from)
for m in regret; do mkdir -p $C/romfs/mods/$m && cp test/mods/$m/main.lua test/mods/$m/metadata.xml $C/romfs/mods/$m/; done
# whole mods, as downloaded (test/mods_eid/<name>/ORIGIN.txt)
for m in eid; do cp -r test/mods_eid/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in trs; do cp -r test/mods_trs/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in ebb; do cp -r test/mods_ebb/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in vcr; do cp -r test/mods_vcr/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in goodtrip; do cp -r test/mods_gt/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in animated; do cp -r test/mods_ai/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
for m in mcm; do cp -r test/mods_mcm/$m $C/romfs/mods/$m; rm -f $C/romfs/mods/$m/ORIGIN.txt; done
cp -r test/mods_repplus/isaac-repplus $C/romfs/mods/isaac-repplus  # Repentance+ data (src/repplus.lua)
cp -r "test/mods_dr/da rules" "$C/romfs/mods/da rules"; rm -f "$C/romfs/mods/da rules/ORIGIN.txt"
# mod sprites stay .png: the plugin loads them with the game's PNG loader (isaaclua_png_redirect in src/main.c)
cp example/isaaclua.ini $C/romfs/isaaclua.ini
if [ "$1" = test ] && [ -d test/mods_cat ]; then
    # test branch: random mods from the mod browser catalogue (README "10 случайных модов")
    for d in test/mods_cat/*/; do m=$(basename "$d"); cp -r "$d" "$C/romfs/mods/$m"; rm -f "$C/romfs/mods/$m/ORIGIN.txt"; done
fi
cp README.md $T/README.md
cp README.en.md $T/README.en.md
V=$(sed -n 's/^#define ISAACLUA_VERSION "\(.*\)"/\1/p' src/version.h)
if [ "$1" = test ]; then D=release-test Z=release-test/isaaclua_v$V-test.zip; else D=release Z=release/isaaclua_v$V.zip; fi
mkdir -p $D && for z in $D/isaaclua_v*.zip; do case $z in *-debug.zip|*-remote.zip) ;; *) rm -f "$z" ;; esac; done
(cd $T && python3 -I -m zipfile -c ../../$Z atmosphere README.md README.en.md)
python3 -I -m zipfile -l $Z
