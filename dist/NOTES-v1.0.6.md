**isaaclua 1.0.6** lets PC Lua mods run on The Binding of Isaac: Repentance for Nintendo Switch. It needs Atmosphère.

Two files:
- **`isaaclua_v1.0.6.zip`**: only the loader and its settings, no mods.
- **`isaaclua_v1.0.6-repplus.zip`**: the loader plus the Repentance+ mod `isaac-repplus` 0.6.114. What it does and what is missing: [README](https://github.com/mrcucumber790/isaaclua#repentance-build).

### Install
1. Copy `atmosphere/` from the zip to the root of the SD card. If you are updating, only `exefs/subsdk8` changed.
2. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<mod folder>/`, as in the PC `mods` folder.
3. Start the game. In a run, a line at the top of the screen reads `isaaclua v1.0.6 (Lua 5.3.6), N mod(s) loaded`.

Something wrong? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose). The form lists the files to attach.

### Changes since 1.0.5
- Main menu without Repentance+: the six rows of PC (MODS 4th) with a MODS entry in the menu's own handwriting and a cursor that no longer jumps; pressing A to open the Mods menu no longer switches off the first mod; Mods menu widget text fixed.
