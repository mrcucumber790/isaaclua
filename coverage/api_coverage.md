# PC API coverage in isaaclua 0.99.162

Checked on the Switch on 2026-11-12. The test mod `apichk` reads every name from IsaacDocs, both on a
real object in a run (player, room, door, tear and so on) and on the class's global table.

**1474 of 1498 names exist.** "Exists" means the name answers. Whether it behaves exactly as on PC was
checked separately, for the names the tested mods need.

## Missing (24)

| API | Why |
|---|---|
| `Color:Color`, `KColor:KColor`, `RNG:RNG`, `Vector:Vector` | A constructor called on an existing object. On PC this is a side effect of LuaBridge, and mods don't use it. `Color(...)`, `Vector(...)` and the others work. |
| `Entity.SplatColor` | The field was not found in the Switch build. |
| `EntityLaser.HomingLaser` | The PC API exposes nothing on this class. |
| `EntityPlayer.FriendBallEnemy`, `Game:SpawnEntityDesc` | Need the `EntityDesc` class, which the loader doesn't have yet. |
| `EntityPlayer.SecondaryActiveItem`, `EntityProjectile.Damage` | The fields were not found. |
| `EntityTear.Bounced`, `EntityTear.CanTriggerStreakEnd`, `EntityTear.PosDisplacement` | The offsets on Switch are not confirmed. |
| `Game:AddPixelation` | The Switch build stores this effect differently. |
| `Game:GetAmbush`, `Game:GetFont` | The loader doesn't have these classes yet. |
| `GridEntityDoor:GetSpriteOffset`, `GridEntityRock.FrameCnt` | The offsets are not confirmed. |
| `Room:GetDeliriumDistance`, `Room:GetLRoomAreaDesc`, `Room:GetLRoomTileDesc`, `Room:GetLavaIntensity`, `Room:InvalidatePickupVision`, `Room:TryPlaceLadder` | Not exported by the Switch build, and the offsets are not confirmed. |

None of these names is used by the mods tested on the console. When a mod uses a missing name, a
yellow line `not on Switch yet: <name>` appears at the top of the screen, and the mod keeps running.

## Not checked

Some classes can't be created in a normal run, so `apichk` skips them:

- `GridEntityPressurePlate`
- `FontRenderSettings`
- `ItemConfig_Costume`
- the `CppContainer_*` containers
