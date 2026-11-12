**isaaclua 0.99.162** lets PC Lua mods run on The Binding of Isaac: Repentance for Nintendo Switch. It needs Atmosphère.

**The zip holds only the loader and its settings. It contains no mods.**

### Install
1. Copy `atmosphere/` from the zip to the root of the SD card.
2. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<mod folder>/`, as in the PC `mods` folder.
3. Start the game. In a run, a line at the top of the screen reads `isaaclua v0.99.162 (Lua 5.3.6), N mod(s) loaded`.

Something wrong? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose). The form lists the files to attach.

### Changes in 0.99.162
- More of the PC API: doors, poops, pits, rocks, room fields, `SpawnGridIndex`, `TearsOffset` and more. 1474 of 1498 PC API names now exist.
- `EntityPlayer:GetName()` returns the character's name instead of `#ISAAC_NAME`.
- The Lua garbage collector runs less often, so the 15–20 ms stutters are gone.
