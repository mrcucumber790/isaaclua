# isaaclua: Lua mods for Isaac on Switch

A plugin for **The Binding of Isaac: Repentance on Nintendo Switch** (1.7.9b, title 010021C000B6A000)
that embeds Lua 5.3 (the same version as PC) into the game and runs mods in the PC format:
`mods/<name>/main.lua`, `RegisterMod`, `mod:AddCallback(ModCallbacks...)`, `Isaac.*`, `EntityPlayer`,
`Vector`. The rule is that mod files stay unmodified: whatever API a mod needs is added to the loader.

The Switch build of the game has no Lua at all, so the whole PC API is rebuilt on top of the game's
exported functions and known field offsets. Status (0.99.162): **the loader is complete**. Every PC
callback is called; of the 1498 PC API names (IsaacDocs) 1474 exist, the rest are listed in
`coverage/api_coverage.md`. Not every PC mod will run: the Switch build has no shaders, a 426x240 screen
and less memory. The Russian `README.md` is the main document and carries the full version history.

Download `isaaclua_v<version>.zip` from [Releases](https://github.com/mrcucumber790/isaaclua/releases):
the loader and its settings only, no mods; take mods from PC. Found a problem? [Open an issue](https://github.com/mrcucumber790/isaaclua/issues/new/choose)
with the form, it says which files to attach.

Current version: **0.99.162**. The version is shown at the top of the screen in a run
(`isaaclua v0.99.162 (Lua 5.3.6), N mod(s) loaded`).

## Install (Switch with Atmosphère)

Copy `atmosphere/` from `isaaclua_v<version>.zip` to the root of the SD card:

```
sd:/atmosphere/contents/010021C000B6A000/exefs/subsdk8           the plugin
sd:/atmosphere/contents/010021C000B6A000/romfs/mods/<name>/      mods, as on PC
sd:/atmosphere/contents/010021C000B6A000/romfs/isaaclua.ini      settings
```

Disable a mod as on PC: put an empty `disable.it` file in its folder. The plugin is named `subsdk8` so
it can sit next to isaacqol (`subsdk9`).

If the game crashes, Atmosphère writes a report to `sd:/atmosphere/crash_reports/`. Send the newest one.

## Included mods

All of them are the PC versions with no file changed.

| Mod | What the loader had to add | Status |
|---|---|---|
| Regret Pedestals | `Sprite`, `GetSprite`, `WorldToScreen`, `GetItemConfig` | works on Switch |
| External Item Descriptions 5.25 | mod resources, `Font`, full `ItemConfig`, 7 callbacks, player/pickup/room/level fields, `Options`, `RNG`, `json` | works on Switch |
| Treasure Room Start 1.16.0 | `Isaac.ExecuteCommand("reseed")`, `BitSet128`, `EntityRef`, `EntityPtr`, room grid | PC test |
| Enhanced Boss Bars 1.34 | `Sprite:GetTexel`, entity flags, NPC and champion fields | works on Switch |
| Visible Cards Redux 2.2 | `EntityPlayer.CanFly` | PC test |
| GoodTrip 1.2.8 | room transitions, doors, room flags | PC test |
| Animated Items 1.11 | mod entities from `content/entities2.xml`, render callbacks | works on Switch |
| Mod Config Menu Impure 2.4 | `MC_INPUT_ACTION`, gamepad buttons in `Input`, `require` across mods, `HUD:SetVisible`, `ControlsEnabled` | opens and draws on Switch; controls fixed in 0.15 |

## Controls

- **Mod Config Menu**: press the right stick (R3) in a room with no enemies, as with a PC gamepad.
  Either stick or the d-pad moves, A selects, B goes back (since 0.18 the left stick counts only along its main axis in MCM, so a slightly sideways push no longer changes an option). Both sticks at once (L3+R3) restart the run and do not
  open the menu. The menu is drawn at 0.84 of its size to fit the 426x240 screen.
- **Touchscreen as the mouse** (since 0.19): `Input.GetMousePosition` and `Input.IsMouseBtnPressed(0)`
  follow the first finger. In GoodTrip, drag its minimap by the bar on top, tap the pin to keep it shown,
  tap a room to travel there.
- **Left stick for mods** (since 0.19): a move action counts as pressed from half a push, so a slightly
  drifting stick no longer reads as walking.
- **On-screen keyboard**: PC mods read keys, the Switch has none. In a run, **+ and − together** open a
  keyboard at the bottom of the screen: stick or d-pad picks a key, A presses it, B closes. The game
  sees no buttons while it is open. A single + (pause) or − (map) reaches the game a few frames late.

## Console (since 0.24)

As on PC: the `` ` `` key of the on-screen keyboard opens it. Commands: `giveitem` (`g`) `c<id>|t<id>|k<id>|p<id>|<name> [count]`,
`remove` (`r`), `spawn <type>.<variant>.<subtype> [count]`, `reseed`, `lua` (`l`) `<code>`, `clear`, `help`.
Up/Down: history, Tab: complete, Esc or `` ` ``: close. Other commands go to mods' `MC_EXECUTE_CMD`.
Bind a button to it in `isaaclua.ini`: `ZL = GRAVE_ACCENT`.

## Mod save data

`mod:SaveData(str)` writes into the game's own save data (where your progress lives), as
`isaaclua/<mod name>.dat`, and commits it at once, so settings survive closing the game (since 0.16; 0.17 fixes a crash at launch in 0.16).
The game opens its save data only after a user is picked, which is after mods load: until then data
stays in memory, and the first access after that reads the file. Deleting the game's save data deletes
the mods' data too.

## isaaclua.ini

```
language = ru          # Options.Language for mods (default: the game's language)
perf = 1               # timings at the bottom of the screen: updates/s, fps, Lua ms per update, slowest mods (since 0.21)
menu_scale = 0.9       # size of Mod Config Menu (default: fit the screen, 0.84)
keyboard = PLUS+MINUS  # two buttons that open the on-screen keyboard
L3 = F1                # a pad button pressing a keyboard key (the game no longer sees it)
```

## Known limitations

- No shaders on Switch: `MC_GET_SHADER_PARAMS` runs (mods draw HUD from it), the returned parameters are dropped.
- The screen is 426x240 instead of 480x270: mods with hard-coded positions near the bottom or right edge
  draw past it.
- Big mods are slower than on PC (Fiend Folio 16-30 fps); `perf = 1` shows which mod costs the most.
- Missing API names: `coverage/api_coverage.md`.

## Building

```
sh build.sh        # out/subsdk8; needs clang, ld.lld, llvm-readelf, python3
sh test/run.sh     # host test with a fake engine (gcc + ASan), runs every included mod
sh tools/package.sh       # release/isaaclua_v<version>.zip: the loader + ported mods
sh tools/package.sh test  # release-test/isaaclua_v<version>-test.zip: plus mods being checked (branch test)
```

`build.sh` fails if the plugin imports anything the game itself does not import
(`tools/allowed_imports.txt`). The game dump used for reverse engineering is not part of the
project and must never be committed.
