**isaaclua 1.0.2** lets PC Lua mods run on The Binding of Isaac: Repentance for Nintendo Switch. It needs Atmosphère.

**The zip holds only the loader and its settings. It contains no mods.**

### Install
1. Copy `atmosphere/` from the zip to the root of the SD card.
2. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<mod folder>/`, as in the PC `mods` folder.
3. Start the game. In a run, a line at the top of the screen reads `isaaclua v1.0.2 (Lua 5.3.6), N mod(s) loaded`.

If you already have 1.0.0, replace `exefs/subsdk8`. Your `isaaclua.ini` can stay.

Something wrong? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose). The form lists the files to attach.

### Changes since 1.0.0
- Mods now see PC's 480x270 screen. Everything they draw is fitted onto the Switch's 426x240, so HUDs made for PC no longer end up off screen. To turn this off, set `pc_screen = 0` in `isaaclua.ini`.
- Internal changes for the Repentance+ port. They don't apply without the `isaac-repplus` mod.
