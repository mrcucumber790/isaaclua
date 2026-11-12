# Покрытие API ПК-версии в isaaclua 0.99.162

Проверено на консоли (2026-11-12): мод `apichk` обращается к каждому имени из IsaacDocs у настоящего
объекта в забеге (игрок, комната, дверь, слеза…) и у глобальной таблицы класса. **Есть 1474 из 1498.**
«Есть» значит, что имя отвечает; что оно работает как на ПК, проверено отдельно для того, что нужно
проверенным модам.

## Нет в загрузчике (24)

| API | Почему нет |
|---|---|
| `Color:Color`, `KColor:KColor`, `RNG:RNG`, `Vector:Vector` | конструктор, вызванный у готового объекта; на ПК это побочный эффект LuaBridge, моды так не пишут. `Color(...)`, `Vector(...)` и т.д. работают |
| `Entity.SplatColor` | поле не найдено в Switch-версии |
| `EntityLaser.HomingLaser` | класс `HomingLaser` на Switch есть, но в API ПК он пустой |
| `EntityPlayer.FriendBallEnemy` | нужен класс `EntityDesc`, которого в загрузчике нет |
| `EntityPlayer.SecondaryActiveItem` | поле не найдено |
| `EntityProjectile.Damage` | поле не найдено |
| `EntityTear.Bounced`, `EntityTear.CanTriggerStreakEnd`, `EntityTear.PosDisplacement` | смещения на Switch не подтверждены |
| `Game:AddPixelation` | на Switch эффект хранится иначе (список у `Game`) |
| `Game:GetAmbush`, `Game:GetFont` | классов `Ambush` и шрифта игры в загрузчике нет |
| `Game:SpawnEntityDesc` | нужен класс `EntityDesc` |
| `GridEntityDoor:GetSpriteOffset` | смещение не подтверждено |
| `GridEntityRock.FrameCnt` | смещение не подтверждено |
| `Room:GetDeliriumDistance`, `Room:GetLRoomAreaDesc`, `Room:GetLRoomTileDesc`, `Room:GetLavaIntensity`, `Room:InvalidatePickupVision`, `Room:TryPlaceLadder` | функций нет среди экспортов Switch, смещения не подтверждены |

Ни одно из этих имён не встречается в модах, проверенных на консоли. Если мод обращается к имени,
которого нет, вверху экрана появляется жёлтая строка `not on Switch yet: <имя>`, мод продолжает работать.

## Не проверялись

Классы, которые `apichk` не может создать в обычном забеге: `GridEntityPressurePlate`,
`FontRenderSettings`, `ItemConfig_Costume`, контейнеры `CppContainer_*`.
