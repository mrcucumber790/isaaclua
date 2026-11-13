**isaaclua 1.0.3** lets PC Lua mods run on The Binding of Isaac: Repentance for Nintendo Switch. It needs Atmosphère.

**The zip holds only the loader and its settings. It contains no mods.**

### Install
1. Copy `atmosphere/` from the zip to the root of the SD card. If you are updating, only `exefs/subsdk8` changed.
2. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<mod folder>/`, as in the PC `mods` folder.
3. Start the game. In a run, a line at the top of the screen reads `isaaclua v1.0.3 (Lua 5.3.6), N mod(s) loaded`.

Something wrong? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose). The form lists the files to attach.

### Changes since 1.0.2
- A mod that reads the screen size while it loads now also sees 480x270 (`pc_screen`).
- Internal changes for the Repentance+ port. They don't apply without the `isaac-repplus` mod.
