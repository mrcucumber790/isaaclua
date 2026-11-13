# isaaclua

PC Lua mods for **The Binding of Isaac: Repentance on Nintendo Switch**.

The Switch version of the game has no Lua and no mod support. isaaclua is an Atmosphère plugin that
adds Lua 5.3 (the same version as on PC) to the game and rebuilds the PC modding API on top of the
game's own code. PC mods (`main.lua`, `RegisterMod`, `mod:AddCallback(...)`, `resources/`, `content/`)
are loaded as they are, without changing their files.

Current version: **1.0.5**.

## What works

- **Callbacks:** every PC callback (`ModCallbacks`) is called, with the same filters and return values as on PC.
- **API:** 1474 of the 1498 PC API names from IsaacDocs: `Isaac`, `Game`, `Level`, `Room`, all entity and
  grid classes, `ItemPool`, `ItemConfig`, `Sprite`, `Font`, `SFXManager`, `MusicManager`, `HUD`, `Input`,
  `Options`, `RNG`, `Vector`, `Color` and others. The missing names are listed in
  [coverage/api_coverage.md](coverage/api_coverage.md).
- **Mod files:** `resources/` (sprites, animations, sounds, music, fonts) and `content/` (`items.xml`,
  `entities2.xml`, `players.xml`, `itempools.xml`, `pocketitems.xml`, `music.xml`, `sounds.xml`,
  `costumes2.xml`, `challenges.xml`).
- **Mod save data:** `SaveData` / `LoadData`, stored in the game's own save.
- **Load order:** the same as on PC (by mod name).
- **Mods menu:** turn mods on and off from the main menu.
- **On-screen keyboard:** for mods that read keys. Press + and − together.
- **Touchscreen as the mouse.**
- **Debug console.**

Mods checked on a Switch include External Item Descriptions, Mod Config Menu, Enhanced Boss Bars,
Minimap API, StageAPI, Epiphany, Fiend Folio, Animated Items, Da Rules and GoodTrip.

## Install

You need a Switch with Atmosphère and Repentance on the current update (PC 1.7.9b content). On any
other build of the game the plugin turns itself off.

1. Download `isaaclua_v<version>.zip` (or `isaaclua_v<version>-repplus.zip`, see below) from [Releases](https://github.com/mrcucumber790/isaaclua/releases).
2. Copy the `atmosphere/` folder from the zip to the root of the SD card.
3. Put PC mods into `sd:/atmosphere/contents/010021C000B6A000/romfs/mods/`, one folder per mod, exactly
   as in the PC `mods` folder. You can take them from your PC game or from the Steam Workshop.
4. Start the game. In a run, a line at the top of the screen shows `isaaclua v1.0.5 (Lua 5.3.6), N mod(s) loaded`.

To turn a mod off, put an empty `disable.it` file in its folder, or use the Mods menu.

## Repentance+ build

Every release has two files:

- `isaaclua_v<version>.zip`: the mod loader only.
- `isaaclua_v<version>-repplus.zip`: the same loader plus the `isaac-repplus` mod (now **0.6.113**). It brings
  the changes of Repentance+ (the newest PC update) to the Switch game. It is not a port of the PC
  executable: it is a mod made from the REP+ game data and from the REP+ program code, checked on a Switch.

Install it the same way. To go back to plain Repentance, turn `isaac-repplus` off in the Mods menu and
restart the game. While it is on, mods see `REPENTANCE_PLUS = true` and use their REP+ code paths.

What is done:

- Item, trinket, card, pill and character data from the REP+ files: quality, charges, prices, tags,
  transformations and all item pools.
- Almost all REP+ changes to items, trinkets, pills, bosses and enemies, including those from the
  1.9.7.4–1.9.7.17 patch notes.
- Final boss armor, REP+ boss animations, about 2/3 of the new sounds, the Lil Portal bedroom.
- The "My Stuff" page on the pause screen, with REP+ item descriptions.
- The REP+ Lua API additions (`FontRenderSettings`, `Font:DrawString` with settings, `KillWithSource` and others).

What is not done:

- REP+ visual changes: new heart art, boss intro screens, Deeds, the new HUD, the compact pause menu.
- Mega Fatty and Camillo Jr. behaviour changes, and some small bug fixes from the patch notes.
- About 1/3 of the new sounds.
- Online play.

The full list of REP+ changes with their status is `REPPLUS_CHANGES.ru.md` in the zip (in Russian).

## Settings

Settings live in `romfs/isaaclua.ini`. Every option is described inside the file.

| Setting | What it does |
|---|---|
| `language = ru` | The language mods see. By default it is the game's language. |
| `pc_screen = 0` | Turns off the PC screen mode, so mods see the real 426x240 screen. |
| `errors = 1` | Shows mod errors and warnings at the top of the screen. They are off by default and always go to `errors.log`. |
| `perf = 1` | Shows FPS, Lua time and the slowest mods at the bottom of the screen. |
| `menu_scale = 0.9` | The size of Mod Config Menu. |
| `keyboard = PLUS+MINUS` | The buttons that open the on-screen keyboard. |
| `L3 = F1` | Binds a pad button to a keyboard key. |

## Known problems

- **Not every PC mod works.** About 24 API names are missing, and mods that need REPENTOGON (not the
  standard API) don't work.
- **No shaders on Switch.** `MC_GET_SHADER_PARAMS` still runs, because mods draw HUD from it, but shader
  effects are not shown.
- **Smaller screen.** The Switch screen is 426x240, not 480x270. By default mods see a 480x270 screen,
  and what they draw is shrunk by about 11% to fit (`pc_screen`). This puts HUDs made for PC where they
  are on PC. Mod text and sprites look slightly smaller than on PC, and so do mod sprites drawn over the
  game world.
- **Speed.** Big mods are slower than on PC. Fiend Folio runs at 16–30 FPS. Stats+ alone caps the game at
  about 30 FPS. With an average set of mods, the game holds 60 FPS.
- **Memory and file count.**
  - Mods get up to 512 MB of memory. A very large set of mods at once (~200) can crash when a
    run starts.
  - Atmosphère's romfs has a file-count limit: thousands of mod files together can hang the game at boot.
- **Turning a mod off while the game is running can break mods that depend on it.** For example, StageAPI
  is needed by Minimap API's map and Fiend Folio. Restart the game after changing the mod set.
- **Mod errors are not shown on screen by default.** Set `errors = 1` to see them.
- **`errors.log` is rewritten at every launch.** It is the log of mod errors, kept in the game's save.
  Back it up right after a problem.

## Reporting a problem

Open an issue with the [bug report form](https://github.com/mrcucumber790/isaaclua/issues/new/choose).
It lists what to attach:

- your mod list;
- `errors.log` from a JKSV backup of the game's save;
- the newest crash report from `sd:/atmosphere/crash_reports/`;
- a screenshot of the error.

## Building

```
sh build.sh             # out/subsdk8; needs clang, ld.lld, llvm-readelf, python3
DEBUG=1 sh build.sh     # debug build (version ends in -debug)
```

The plugin may import only functions that the game itself imports (`tools/allowed_imports.txt`).
`build.sh` checks this. No game files are part of this repository.

Layout:

- `src/main.c`: hooks into the game, mod loading.
- `src/prelude.lua`, `src/api*.lua`: the PC API in Lua.
- `src/native.c`: calls into the game for Lua.
- `src/modfs.c`: mod files.
- `src/keyboard.c`: on-screen keyboard.
- `lua/`: Lua 5.3.6.
