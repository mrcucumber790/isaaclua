**isaaclua 1.0.0** is the first complete release. It lets PC Lua mods run on The Binding of Isaac: Repentance for Nintendo Switch. It needs Atmosphère.

**The zip holds only the loader and its settings. It contains no mods.**

### Install
1. Copy `atmosphere/` from the zip to the root of the SD card.
2. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<mod folder>/`, as in the PC `mods` folder.
3. Start the game. In a run, a line at the top of the screen reads `isaaclua v1.0.0 (Lua 5.3.6), N mod(s) loaded`.

Something wrong? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose). The form lists the files to attach.

### What's included
- Every PC callback is called.
- 1474 of the 1498 PC API names exist.
- Mods' `resources/` and `content/` are loaded.
- Mods can save their data.
- Mod Config Menu works, and there are an on-screen keyboard and a console.

The [README](https://github.com/mrcucumber790/isaaclua#known-problems) lists the known problems.

### Changes since 0.99.162
- Mod errors and warnings no longer appear on screen. They still go to `errors.log`, and `errors = 1` in `isaaclua.ini` shows them again.
