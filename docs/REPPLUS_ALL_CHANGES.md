# Все изменения Repentance+ по сравнению с Switch Repentance 1.7.9b

Составлено 2026-11-05 по настоящим файлам: данные REP+ из `afterbirthp.a` последнего ПК-билда (депо 3353471) сравнены с данными Switch-версии, прочитанными с консоли (`romfs:/rp_patch/resources`). Поведение сверяется с кодом exe REP+. Вики используется только как подсказка, где искать.

Статус: ✓✓ работает на консоли, ✓ сделано (проверено на ПК-тесте), ○ не сделано, ✗ не переносится. Сделанное в 0.2.0–0.4.0 жило в отдельной сборке isaac-repplus на isaaclua 0.85; теперь переносится в isaaclua 0.99.48+.

## Содержание
1. Данные предметов
2. Качество и теги предметов
3. Пулы предметов
4. Карты, руны, таблетки
5. Персонажи, испытания, костюмы, огоньки Bethany
6. Монстры и боссы
7. Новые сущности, проклятие, звуки, малыши
8. Поведение предметов
9. Брелки
10. Общие изменения игры
11. Интерфейс
12. Lua API для модов
13. Онлайн (не переносится)

## 1. Данные предметов (items.xml)

Слой REP+ при загрузке меняет заряды, цены, сердца и флаги пересчёта (✓✓ с 0.2.0). Не сделаны: новый null-предмет 132 (тело Transcendence, ○). Persistent у Milk! заменён поведением в repplus.lua (эффект держится до конца этажа).

- + null 132 transcendance body
- active 78 Book Of Revelations: devilprice: — → 2
- active 111 The Bean: chargetype: — → timed; maxcharges: 1 → 120
- active 186 Blood Rights: cache: — → firedelay
- passive 243 Trinity Shield: devilprice: 2 → —
- active 294 Butter Bean: maxcharges: 90 → 60
- active 351 Mega Bean: maxcharges: 3 → 6
- passive 368 Epiphora: cache: — → firedelay
- passive 374 Holy Light: devilprice: — → 2
- active 386 D12: shopprice: — → 10
- active 421 Kidney Bean: chargetype: — → timed; maxcharges: 1 → 120
- familiar 436 Milk: persistent: — → true
- active 477 Void: maxcharges: 6 → 4
- active 478 Pause: shopprice: — → 10
- active 489 D Infinity: maxcharges: 2 → 4
- passive 533 Trisagion: devilprice: — → 2
- passive 554 2Spooky: cache: color → color firedelay shotspeed
- passive 594 Jupiter: hearts: 1 → 4
- passive 606 Ocular Rift: devilprice: 2 → —
- passive 643 Revelation: soulhearts: 4 → —
- familiar 651 Star Of Bethlehem: devilprice: — → 2
- passive 671 Candy Heart: cache: — → all
- passive 686 Soul Locket: cache: — → all
- trinket 66 Lazy Worm: cache: range shotspeed → range shotspeed damage
- trinket 169 Kids Drawing: cache: — → flying

## 2. Качество и теги предметов (items_metadata.xml)

Слой REP+ меняет качество и теги (✓✓ с 0.2.0). Тег `devilsacrifice` (сделка за шипы) сделан в repplus.lua, ✓✓ 0.6.32 (раздел 10).

- The Inner Eye (item 2): craftquality: — → 2; quality: 2 → 3
- My Reflection (item 5): quality: 2 → 3
- Skatole (item 9): tags: poop summonable nocantrip fly → poop summonable offensive nocantrip fly
- Halo Of Flies (item 10): tags: summonable monstermanual fly → summonable fly
- 1Up (item 11): tags: mushroom → uniquefamiliar mushroom
- The Virus (item 13): tags: nolostbr summonable syringe → summonable syringe offensive
- Skeleton Key (item 17): quality: 3 → 2
- Transcendence (item 20): quality: 3 → 2
- The Compass (item 21): quality: 2 → 1
- The Bible (item 33): tags: book offensive angel → nolostbr book angel
- The Necronomicon (item 35): quality: 1 → 0
- Tammys Head (item 38): quality: 2 → 1
- Moms Bra (item 39): quality: 1 → 0
- Cupids Arrow (item 48): quality: 3 → 2
- Treasure Map (item 54): quality: 2 → 1
- Distant Admiration (item 57): quality: 2 → 1
- Book Of Shadows (item 58): craftquality: — → 2; tags: book offensive → book
- Charm Of The Vampire (item 62): quality: 1 → 2
- Steam Sale (item 64): craftquality: 3 → —
- Anarchist Cookbook (item 65): quality: 1 → 0
- The Hourglass (item 66): quality: 1 → 0
- Rosary (item 72): tags: nolostbr nocantrip angel → tearsup offensive nocantrip angel
- Cube Of Meat (item 73): quality: 2 → 1
- A Quarter (item 74): craftquality: 1 → —
- Dead Cat (item 81): craftquality: — → 2
- Monstros Tooth (item 86): quality: 1 → 0
- Little Chubby (item 88): quality: 2 → 1
- The Book Of Sin (item 97): quality: 2 → 1
- Little Gish (item 99): quality: 2 → 1
- The Halo (item 101): quality: 2 → 3
- Guardian Angel (item 112): quality: 2 → 1
- Demon Baby (item 113): quality: 2 → 1
- Ouija Board (item 115): tags: nolostbr summonable → offensive summonable tearsup
- Bobbybomb (item 125): quality: 2 → 1
- Razor Blade (item 126): tags: nolostbr → nolostbr devilsacrifice
- Bomb Bag (item 131): quality: 2 → 1
- Guppys Paw (item 133): craftquality: — → 2
- Iv Bag (item 135): quality: 1 → 2
- Best Friend (item 136): quality: 1 → 0
- Remote Detonator (item 137): quality: 1 → 0
- Stigmata (item 138): tags: summonable → nolostbr summonable
- Infestation (item 148): tags: nolostbr summonable → nolostbr summonable fly
- Bloody Lust (item 157): tags: nolostbr summonable → nolostbr summonable devilsacrifice
- Crack The Sky (item 160): quality: 2 → 1
- Ankh (item 161): quality: 1 → 0
- Daddy Longlegs (item 170): tags: summonable spider offensive → summonable spider monstermanual offensive
- Rainbow Baby (item 174): tags: summonable baby offensive → summonable baby monstermanual offensive
- White Pony (item 181): quality: 2 → 3
- Blood Rights (item 186): tags: nolostbr → nolostbr devilsacrifice
- Abel (item 188): tags: summonable baby offensive → summonable baby monstermanual offensive
- Smb Super Fan (item 189): tags: summonable offensive → summonable tearsup offensive
- Moms Coin Purse (item 195): quality: 1 → 0
- Box (item 198): craftquality: 1 → —
- Midas Touch (item 202): quality: 2 → 1
- Guillotine (item 206): craftquality: — → 1; quality: 2 → 0
- Ball Of Bandages (item 207): quality: 2 → 1
- Champion Belt (item 208): craftquality: — → 2
- Butt Bombs (item 209): quality: 2 → 1
- Goat Head (item 215): tags: summonable offensive lazarussharedglobal → summonable offensive nogreed lazarussharedglobal
- Antigravity (item 222): quality: 2 → 1
- Gimpy (item 225): tags: nolostbr summonable → nolostbr summonable devilsacrifice
- E Coli (item 236): quality: 1 → 0
- Contract From Below (item 241): quality: 3 → 2
- Blue Map (item 246): quality: 2 → 1
- Screw (item 255): quality: 3 → 2
- Dark Matter (item 259): craftquality: — → 2
- Smart Fly (item 264): quality: 2 → 1
- Bbf (item 272): quality: 1 → 0
- Bobs Brain (item 273): quality: 1 → 0
- Dark Bum (item 278): tags: summonable → summonable offensive
- Big Fan (item 279): quality: 2 → 1
- Punching Bag (item 281): tags: summonable → summonable monstermanual
- How To Jump (item 282): tags: book → nolostbr book nocantrip
- D100 (item 283): craftquality: — → 2; quality: 3 → 1
- D4 (item 284): craftquality: — → 2; quality: 2 → 1
- Book Of Secrets (item 287): quality: 1 → 0
- Red Candle (item 289): quality: 2 → 1
- Satanic Bible (item 292): quality: 4 → 3; tags: nolostbr book → book offensive
- Head Of Krampus (item 293): quality: 2 → 1
- Butter Bean (item 294): quality: 1 → 0
- Magic Fingers (item 295): quality: 1 → 0
- Converter (item 296): craftquality: — → 2; quality: 2 → 1
- Undefined (item 324): quality: 2 → 1
- Scissors (item 325): quality: 1 → 0
- The Ludovico Technique (item 329): quality: 2 → 1
- Soy Milk (item 330): craftquality: — → 1
- Dead Onion (item 336): quality: 3 → 2
- Placebo (item 348): quality: 2 → 1
- Sworn Protector (item 363): quality: 3 → 2
- Sticky Bombs (item 367): quality: 2 → 1
- Epiphora (item 368): quality: 2 → 1; tags: summonable offensive → summonable tearsup offensive
- Charged Baby (item 372): quality: 3 → 2
- Lil Gurdy (item 384): quality: 2 → 1
- Censer (item 387): quality: 3 → 2
- Seraphim (item 390): tags: summonable baby monstermanual offensive → summonable baby monstermanual offensive angel
- Marked (item 394): craftquality: — → 1; quality: 1 → 0
- Ventricle Razor (item 396): quality: 1 → 0
- Tractor Beam (item 397): tags: summonable offensive → summonable tearsup offensive
- Spear Of Destiny (item 400): quality: 2 → 1
- D8 (item 406): quality: 2 → 1
- Cambion Conception (item 412): quality: 2 → 1
- Black Powder (item 420): quality: 1 → 0
- Kidney Bean (item 421): quality: 1 → 0
- Glowing Hour Glass (item 422): tags: offensive nocantrip → nocantrip
- Obsessed Fan (item 426): tags: summonable offensive fly → summonable monstermanual offensive fly
- Multidimensional Baby (item 431): tags: summonable baby offensive → summonable baby monstermanual offensive
- My Shadow (item 433): quality: 2 → 1
- Milk (item 436): tags: uniquefamiliar nolostbr summonable → uniquefamiliar nolostbr summonable tearsup
- D7 (item 437): tags: offensive → offensive nogreed
- Varicose Veins (item 452): quality: 1 → 0
- Broken Glass Cannon (item 474): tags:  → noeden
- Void (item 477): quality: 4 → 3
- Pause (item 478): quality: 1 → 0
- Compost (item 480): quality: 1 → 0
- Clicker (item 482): tags: nolostbr nochallenge nodaily → nolostbr nochallenge
- Dull Razor (item 486): quality: 1 → 0
- Edens Soul (item 490): quality: 3 → 2
- Adrenaline (item 493): quality: 2 → 1
- Sack Of Sacks (item 500): quality: 3 → 2
- Poke Go (item 505): tags: offensive nocantrip → summonable offensive nocantrip nogreed
- Moms Razor (item 508): quality: 2 → 1
- Sprinkler (item 516): quality: 2 → 1
- Buddy In A Box (item 518): tags: summonable baby offensive → summonable baby monstermanual offensive
- Lil Delirium (item 519): quality: 2 → 1; tags: summonable monstermanual offensive → summonable baby monstermanual offensive
- Jumper Cables (item 520): quality: 3 → 2
- Leprosy (item 525): quality: 1 → 0
- 7 Seals (item 526): tags: summonable offensive → summonable monstermanual offensive
- Deaths List (item 530): quality: 2 → 1; tags: summonable offensive → summonable offensive lazarusshared
- Lachryphagy (item 532): quality: 2 → 3
- Sacrificial Altar (item 536): quality: 2 → 1
- Mystery Egg (item 539): quality: 1 → 0
- Slipped Rib (item 542): quality: 2 → 1
- Pointy Rib (item 544): quality: 2 → 1
- Book Of The Dead (item 545): quality: 3 → 2
- Dads Ring (item 546): quality: 3 → 2
- Divorce Papers (item 547): tags: summonable offensive → summonable tearsup offensive
- Brittle Bones (item 549): tags: nolostbr → offensive tearsup
- 2Spooky (item 554): tags: summonable → summonable tearsup
- Golden Razor (item 555): quality: 1 → 0
- Sulfur (item 556): quality: 3 → 2
- It Hurts (item 560): tags: nolostbr summonable → nolostbr summonable tearsup
- Rock Bottom (item 562): quality: 3 → 4
- Nancy Bombs (item 563): quality: 2 → 1
- A Bar Of Soap (item 564): quality: 3 → 2
- Blood Puppy (item 565): tags: offensive → summonable monstermanual offensive
- Divine Intervention (item 568): quality: 2 → 1
- Blood Oath (item 569): tags: uniquefamiliar nolostbr summonable → uniquefamiliar nolostbr summonable devilsacrifice
- Monstrance (item 574): quality: 2 → 1
- The Intruder (item 575): quality: 3 → 2
- Damocles (item 577): craftquality: — → 2; quality: 2 → 3
- Free Lemonade (item 578): quality: 1 → 0
- Red Key (item 580): tags: offensive nogreed → offensive nocantrip nogreed
- Wavy Cap (item 582): quality: 1 → 0
- Rocket In A Jar (item 583): quality: 2 → 1
- Alabaster Box (item 585): quality: 2 → 1
- Luna (item 589): quality: 2 → 3
- Mercurius (item 590): quality: 3 → 2
- Mars (item 593): craftquality: — → 1; quality: 2 → 0
- Voodoo Head (item 599): tags: summonable offensive nocantrip lazarussharedglobal → summonable offensive nocantrip lazarussharedglobal nogreed
- Member Card (item 602): tags: summonable offensive → summonable offensive lazarussharedglobal
- Battery Pack (item 603): craftquality: 1 → —
- The Scooper (item 605): quality: 1 → 0
- Ocular Rift (item 606): quality: 3 → 2
- Bird Cage (item 610): quality: 1 → 0
- Lost Soul (item 612): quality: 2 → 1; tags: summonable nocantrip → uniquefamiliar summonable nocantrip
- Lil Dumpy (item 615): tags: summonable → summonable monstermanual
- Birds Eye (item 616): tags: offensive → summonable offensive
- Lodestone (item 617): quality: 3 → 2
- Genesis (item 622): craftquality: — → 3; quality: 3 → 2
- Meat Cleaver (item 631): quality: 1 → 0
- Evil Charm (item 632): quality: 2 → 1
- Purgatory (item 634): quality: 2 → 1
- Knockout Drops (item 637): quality: 3 → 2
- Eraser (item 638): quality: 2 → 1
- Akeldama (item 641): quality: 2 → 1
- Plum Flute (item 650): quality: 1 → 0
- Star Of Bethlehem (item 651): tags: summonable offensive nocantrip stars → uniquefamiliar summonable offensive nocantrip stars
- Cube Baby (item 652): tags: baby → summonable baby monstermanual offensive
- Vade Retro (item 653): quality: 2 → 1
- Spin To Win (item 655): quality: 1 → 0
- Card Reading (item 660): craftquality: — → 2
- Guppys Eye (item 665): tags: guppy offensive → dead guppy offensive
- Strawman (item 667): craftquality: — → 2; quality: 2 → 1; tags: dead offensive → uniquefamiliar dead offensive
- Sausage (item 669): tags: summonable offensive lazarussharedglobal → summonable tearsup offensive lazarussharedglobal
- A Pound Of Flesh (item 672): quality: 1 → 0; tags: summonable nokeeper nogreed → summonable nokeeper nogreed devilsacrifice
- Spirit Shackles (item 674): quality: 2 → 1
- Astral Projection (item 677): quality: 2 → 1
- Lil Portal (item 681): quality: 1 → 0
- Worm Friend (item 682): tags: summonable offensive → summonable monstermanual offensive
- Jar Of Wisps (item 685): quality: 2 → 1
- Friend Finder (item 687): quality: 3 → 2
- Sanguine Bond (item 692): tags: nolostbr summonable → nolostbr summonable lazarussharedglobal devilsacrifice
- Bloody Gust (item 695): tags: nolostbr summonable → nolostbr summonable tearsup devilsacrifice
- Vanishing Twin (item 697): quality: 2 → 1
- Isaacs Tomb (item 701): quality: 3 → 2
- Esau Jr (item 703): craftquality: — → 2; quality: 2 → 1
- Berserk (item 704): quality: 2 → 1; tags: offensive → offensive devilsacrifice
- Dark Arts (item 705): quality: 2 → 1
- Abyss (item 706): quality: 4 → 3
- Suplex (item 709): quality: 2 → 1
- Bag Of Crafting (item 710): tags: offensive → offensive nocantrip
- Keepers Box (item 719): quality: 2 → 1
- Everything Jar (item 720): quality: 3 → 2
- Anima Sola (item 722): quality: 3 → 2
- Ibs (item 725): tags: summonable offensive → poop summonable offensive
- Ghost Bombs (item 727): quality: 2 → 1
- Decap Attack (item 729): quality: 2 → 1
- Moms Toenail (trinket 16): tags: mom → 
- Black Lipstick (trinket 17): tags:  → nogreed
- Bible Tract (trinket 18): tags:  → nogreed
- Mysterious Paper (trinket 21): tags:  → nochallenge
- Missing Poster (trinket 23): tags:  → nochallenge
- Broken Ankh (trinket 28): tags:  → nochallenge
- Childs Heart (trinket 34): tags:  → nogreed
- Moms Pearl (trinket 38): tags: mom → nogreed
- Moms Locket (trinket 87): tags: mom →

## 3. Пулы предметов (itempools.xml)

Слой REP+ выставляет все пулы как в REP+ (✓✓ с 0.2.0). В 0.6.14 исправлено: после правки пулов не пересчитывалась сумма весов пула, из-за этого около половины пьедесталов давали Завтрак.

- **shop**; убраны Portable Slot (177)
- **devil**: добавлены Dry Baby (265); убраны Brother Bobby (8), Sister Maggy (67)
- **beggar**; убраны Portable Slot (177)
- **keyMaster**: добавлены Skeleton Key (17), Best Bud (274), Angry Fly (511), Sharp Key (623)
- **greedTreasure**: добавлены Heart (15), Boom (19), The Poop (36), Moms Bra (39), Kamikaze (40), Moms Pad (41), Shoop Da Whoop (49), The Hourglass (66), Monstros Tooth (86), Super Bandage (92), Demon Baby (113), Monster Manual (123), Forget Me Now (127), Bucket Of Lard (129), Best Friend (136), Crack The Sky (160), Spider Butt (171), Holy Water (178), The Black Bean (180), Midas Touch (202), Humbleing Bundle (203), Spiderbaby (211), Bogo Bombs (250), Leech (270), Bbf (272), Lil Brimstone (275), Isaacs Heart (276), Butter Bean (294), Magic Fingers (295), Libra (304), Thunder Thighs (314), Isaacs Tears (323), Fates Reward (361), Edens Blessing (381), Seraphim (390), Farting Baby (404), Fruit Cake (418), Mine Crafter (427), Lil Loki (435), Acid Baby (491), Yo Listen (492), Member Card (602), Strawman (667); убраны Deep Pockets (416), Eternal D6 (609), Spindown Dice (723); вес: Anima Sola (722) 0.1→0.5
- **greedBoss**: добавлены Pentagram (51), Super Bandage (92), Catoninetails (165), Placenta (218), Old Bandage (219); убраны Heart (15), Thunder Thighs (314); вес: Raw Liver (16) 1→0.2
- **greedShop**: добавлены Remote Detonator (137), Notched Axe (147), The Candle (164), Sharp Plug (205), Stop Watch (232), The Jar (290), Converter (296), The Boomerang (338), Diplopia (347), More Options (414), Smelter (479), Coupon (521), Eternal D6 (609); вес: D20 (166) 1→0.1
- **greedCurse**: добавлены Guppys Tail (134); убраны Cube Of Meat (73)
- **greedDevil**: добавлены Guppys Tail (134), Missing Page 2 (262), Dry Baby (265), Lil Brimstone (275), Succubus (417); убраны Sister Maggy (67), Tarot Cloth (451)
- **greedAngel**: добавлены The Bible (33), The Relic (98), The Halo (101), The Wafer (108), Dead Sea Scrolls (124), Scapular (142), Prayer Card (146), Lazarus Rags (332), Holy Light (374), Delirious (510), Lil Delirium (519)
- **bombBum**: добавлены Pyromaniac (223)
- **babyShop**: добавлены Gemini (318), Multidimensional Baby (431), Mystery Egg (539), Blood Puppy (565), Cube Baby (652), Vanishing Twin (697), Gello (728); убраны Sacrificial Dagger (172)

## 4. Карты, руны, таблетки (pocketitems.xml)

Все шесть изменений применяются при загрузке (✓✓ 0.6.34; на консоли 0.99.113 у Berkano, The Tower R и Soul of Judas MimicCharge 4, 4, 2).

- rune 38 Berkano: mimiccharge: 2 → 4
- card 72 The Tower R: mimiccharge: 3 → 4
- card 84 Soul Of Judas: mimiccharge: 3 → 2
- pilleffect 21 Hematemesis: mimiccharge: 6 → 4
- pilleffect 25 Amnesia: greedmode: — → false
- pilleffect 31 Question Marks: greedmode: — → false

Поведение карт (код exe):

- Bombs Are Key (таблетка 3) у Tainted ???, 1.9.7.11: меняет местами какашки (не больше максимума) и ключи вместо его невидимых бомб и ключей. ✓✓ консоль: 5 ключей 2 какашки → 2 ключа 5 какашек (в 1.7.9b 1 ключ 2 какашки 5 бомб); у обычного Isaac обмен бомб и ключей как прежде.
- Berkano (руна 38), 1.9.7.9: 3 обычные саранчи Abyss (3.231.25) на комнату вместо 3 синих мух и 3 синих пауков; Rune Shard с эффектом Berkano даёт 1 саранчу вместо мухи и паука (REP+ 0x7bea01, вики). repplus.lua: после MC_USE_CARD убирает только что созданных мух/пауков, ставит саранчу, на следующей комнате саранча убирается. ✓✓ консоль: Berkano → 231.25x3, в следующей комнате 0; Rune Shard → 231.25x1.
- X - Wheel of Fortune (карта 11), 1.9.7.11: бросок Random(100) вместо Random(3): 0 → кран-машина (6.16, если открыто достижение 0x25f, иначе гадалка), ≤ 25 → гадалка (6.3), иначе игровой автомат (6.1) (REP+ UseCard 0x7b34ae; в 1.7.9b 1/3 гадалка, 2/3 автомат, крана нет). ✓✓ консоль, 300 карт: кран 4, гадалка 76, автомат 220.

## 5. Персонажи, испытания, костюмы, огоньки Bethany


**players.xml**: У Каина (46), Евы (117, 122), Самсона (157), воскресшего Лазаря (214), Лилит (412) и Бетани (584) в players.xml REP+ убраны стартовые предметы: в REP+ их выдаёт код (список 0x7ce620). 1.7.9b даёт их из players.xml, но мод, подменивший players.xml на вариант REP+ (extra-animated-portraits), их терял: ✓✓ 0.6.36 repplus.lua выдаёт недостающие в начале забега.

- player 2 Cain: items: 46 → —
- player 5 Eve: items: 122,117 → —
- player 6 Samson: items: 157 → —
- player 11 Lazarus2: items: 214 → —
- player 13 Lilith: items: 357,412 → 357
- player 18 Bethany: items: 584 → —

**challenges.xml**: ✓ 0.6.34 (на консоли не проверено)

- challenge 24 PAY TO PLAY: achievements: 28,21 → 21
- challenge 36 Scat Man: startingitems: 116,9,36,236,209,209,209,247,314,576 → 116,9,36,209,209,209,247,314,576

**costumes2.xml**: Костюм 132 для Transcendence, ○: новый нулевой костюм тела Lost: REP+ Entity_Player::AddCostume 0x75d692 надевает его вместе с костюмом The Intruder (нулевой 55, 0x7844a3), если у игрока Transcendence. ○: пробовал в 0.6.34 (n_lost_body.anm2 из afterbirthp.a), на консоли Айзек без головы: слой head в анимации REP+ пустой, голову рисует что-то ещё; убрано

- costume 132 : anm2path: 132_A Lump of Coal.anm2 → n_lost_body.anm2; isFlying: — → true; priority: — → 101; type: passive → none

**wisps.xml**: Огоньки Book of Virtues: здоровье, урон и скорострельность ✓✓ 0.6.34 (на консоли 0.99.113 у огонька 441 здоровье 6); цвет и картинка (меч) огонька 705, серые слёзы s14/s16 ✓ 0.6.34 (скопированы с огоньков 441, 577, 47)

- wisp 441 : hp: 2 → 6
- wisp 705 : coreColor: core_black → core_red; coreGfx: — → sword.png
- wisp s14 : damage: 0.5 → 3; hp: 2 → 6; tearColor: — → tear_grey
- wisp s15 : hp: 2 → 6
- wisp s16 : fireDelay: 60 → 25; hp: 2 → 6; tearColor: — → tear_grey
- wisp s17 : hp: 2 → 6

## 6. Монстры и боссы (entities2.xml)

У финальных боссов появилась «броня» (shieldStrength), у Flesh Death's Head больше здоровья, часть сущностей не перебрасывается (noreroll).

✓✓ 0.6.34 (на консоли 0.99.113 на 5-м этаже: Flesh Death's Head 50 здоровья, Wizoob и Red Ghost по 32, радиус слезы 9.4 равен 5): здоровье Wizoob, Red Ghost, Flesh Death's Head, размер и тень слезы 9.4, тень монет Ultra Greed, noreroll (Entity_NPC::CanReroll проверяет тот же бит 0x10 тегов +0x118) и тег fly у 3.77. ✓✓ 0.6.73 броня боссов: shieldStrength (поле +0x120 записи EntityConfig на Switch, его копирует Entity_NPC::load_entity_config в NPC+0xccc) у Сатаны, Satan Stomp, Исаака, ???, Агнца и Lamb Body. Механика брони уже есть в 1.7.9b (Ultra-монстры, Hush, Мега Сатана), REP+ только добавил значения. На консоли 0.99.125: 10 ударов по 40 по Исааку сняли 224 вместо 400, по ??? 170.

Поведение боссов и врагов из патчноутов 1.9.7.4–1.9.7.13, по коду exe REP+ (сравнение функций ИИ ПК 1.7.9b и REP+; натив, src/repplus_bosses.inc), 1.0.2 / REP+ 0.6.110 (✓✓ на консоли 1.0.2: Hush при 40% HP 9 смен состояния за 30 с против 16 без REP+; Needle выныривал в 127–217 px против 84–121; D10 превратил 2 монеты в 2 пенни, в 1.7.9b у монет нет devolve, EntityConfig+0xd8 пуст. The Beast в своей комнате: вторая атака за 4 цикла 10/10/13/8, без REP+ всегда 8; всасывание 263–267 кадров против 360; Cod Worm под землёй 2 кадра против 4–62 без REP+):
- The Beast (Switch Entity_NPC_Beast::UpdateAI 0x127ea4): вторая атака цикла выбирается RandomU32() % 3 из SuckStart / DoubleShot / SoulCall при любой фазе (REP+ 0x593691), в 1.7.9b в фазе 0 только всасывание, иначе 50/50 двойной выстрел / души; всасывание заканчивается через 240 кадров вместо 360 (REP+ 0x593cb6). ○ в коопе (2+ игрока) игроков тянет с силой 400 (REP+ 0x5945be).
- Hush (ai_hush 0x19a48c): ниже 50% HP больше не пропускает перезарядку атаки и бросок «подождать»: порог 0.5 заменён на 0.005 (REP+ 0x4c12b8, 0x4c1365).
- Needle / Pasty (ai_needle 0x1ed1e8): выныривают в 120–240 px от игрока вместо 80–200 (REP+ 0x54f5c5).
- Cod Worm (ai_cod_worm 0x1d5f14): вылезает без броска 1 из 20 (REP+ 0x53184e).
- Delirium (ai_delirium 0x1837fc), 1.9.7.11: при смене формы на Mom (45) или ногу Сатаны (84.10) не бросает шанс ярости (DeliriumAggro 120 кадров: быстрее и лишние выстрелы) и сбрасывает её, промах броска тоже сбрасывает (REP+ 0x4a710e). ✓✓ консоль: за ~6 мин 9 раз форма 45.10, ярость всегда 0, шанс не меняется; у других форм ярость 120 после удачного броска. Сатана, выбранный как 84.0 и уже потом ставший ногой (50%), может быть в ярости, так же и в exe REP+.
- Kineti (ai_polty, 816.1) и Blood Cultist (ai_cultist, 885.1), 1.9.7.11: захват камней и кровавый выстрел только если враг на экране (Camera::IsPosVisible после того же броска, REP+ 0x55e22b, 0x4ee6c7). ✓✓ консоль, комната 2x1, враг за краем экрана: за 80 с без REP+ по 2 атаки, с REP+ 0; на экране атак столько же (6 и 3–4).
- ✓✓ 0.6.114 Mega Fatty, красный чемпион (264.0.1): струя рвоты в REP+ (0x4613db, состояние Puking, 9 снарядов раз в 3 кадра) идёт с HeightModifier -40 вместо -50 и FallingSpeedModifier +1.5 вместо -2 (1.7.9b 0x451632), из точки на 8 выше; repplus.lua правит снаряд на первом кадре. На консоли проверено: высота около -61 вместо -71, снаряды падают, а не летят вверх. ○ Camillo Jr. (230): в REP+ (0x53389d) перед атакой проверяется флаг FLAG_SHRINK (уменьшенный не начинает Attack); «дольше замах» из патч-нота в коде пока не найден, не перенесено.
- Комнаты (rooms/*.stb): на Switch комнаты Repentance лежат в rom:/rp_patch/resources/rooms, все 29 файлов того же размера и с тем же числом комнат, что resources-dlc3/rooms в REP+; отличается только порядок дверей в списке комнаты. Переносить нечего.
- Ultra Greed Coin 293.0–3: devolve в пенни 5.20.0 (entities2.xml REP+), D10 превращает монеты (repplus.lua).
- Не отличается от 1.7.9b: Gabriel (молитва и get_alive_enemy_count те же, «fix» в 1.9.7.9 чинил регрессию самого REP+). ○ Stone Eye (новая ветка Rotate перед лучом, REP+ 0x50624f), Mega Fatty (проверка флагов 0x1000a00 перед состоянием 8, 0x460209), Delirium (ступни Mom/Satan), Hush дружественный (флаг +0x16c), Kineti, Blood Cultists, Camillo Jr., Siren.

- + 6.19.0 Revive Machine
- + 1000.202.0 Mega Bean Explosion
- + 1000.203.0 Spawn Pentagram
- + 1000.204.0 Spawn Pentagram Boss
- 3.77.0 ???'s Only Friend: tags: cansacrifice → cansacrifice fly
- 9.4.0 Tear Projectile: collisionRadius: 8 → 5; shadowSize: 8 → 10
- 84.0.0 Satan: shieldStrength: — → 12
- 84.10.0 Satan Stomp: shieldStrength: — → 12
- 96.0.0 Eternal Fly: tags: fly → fly noreroll
- 102.0.0 Isaac: shieldStrength: — → 20
- 102.1.0 Bluebaby: shieldStrength: — → 25
- 219.0.0 Wizoob: stageHP: 4 → 3
- 273.0.0 The Lamb: shieldStrength: — → 25
- 273.10.0 Lamb Body: shieldStrength: — → 10
- 285.0.0 Red Ghost: stageHP: 4 → 3
- 286.0.0 Flesh Deaths Head: baseHP: 30 → 50
- 293.0.0 Ultra Greed Coin (Spinner): shadowSize: 14 → 28
- 293.1.0 Ultra Greed Coin (Key): shadowSize: 14 → 28
- 293.2.0 Ultra Greed Coin (Bomb): shadowSize: 14 → 28
- 293.3.0 Ultra Greed Coin (Heart): shadowSize: 14 → 28
- 409.1.0 Purple Ball: tags: — → noreroll
- 409.2.0 Rag Mega's Rebirth Pillar: tags: — → noreroll
- 802.0.0 Blood Puppy: tags: — → noreroll
- 802.1.0 Blood Puppy: tags: — → noreroll
- 951.31.0 Ultra War Bomb: tags: — → noreroll
- 951.42.0 Ultra Death Head: tags: — → noreroll

## 7. Новые сущности, проклятие, звуки, малыши

- Revive Machine (6.19): автомат воскрешения из онлайн-коопа, ✗.
- Mega Bean Explosion (1000.202): взрыв Mega Bean на всю комнату, как Mama Mega; своей анимации у него нет (anm2path пустой), ✓✓ 0.6.40 через 1000.127 (см. Mega Bean в разделе 8).
- Spawn Pentagram (1000.203): пентаграмма на месте появления врага, ✓✓ 0.6.75 для волн испытаний, Boss Rush и Greed (repplus.lua: враг волны спавнится как в 1.7.9b, но скрыт, заморожен и неуязвим, пока не кончится анимация Summon, 32 кадра; размер = радиус врага / 16 в пределах 0.8–2.0; звук 871 один раз на волну; анимация и картинка из afterbirthp.a REP+). На консоли 0.99.127 в комнате испытания: 3 пентаграммы, враги скрыты 30 тиков, затем появились. В exe REP+: новая функция 0x6fef00 вместо Game::Spawn (0x6fe6e0) ставит эффект 1000.203 и запоминает в нём тип, вариант, позицию, подтип, сид и спавнера (+0x47c..+0x4a4); обновление эффекта (0x5d44e0, таблица Entity_Effect::Update 0x60723c) масштабирует его по размеру врага из EntityConfig и, когда анимация кончилась, спавнит врага через Game::Spawn и удаляет себя. Вызывают её Ambush::SpawnWave (0x833ff0, волны испытаний и Boss Rush, 4 места), 0x8339b0, 0x816cc0 и 0x4b1c70. Анимация `gfx/1000.203_spawn pentagram.anm2` лежит в afterbirthp.a, которого сейчас нет. В entities2.xml REP+ есть ещё Spawn Pentagram Boss (1000.204, та же анимация; под тем же номером там же стоит Player Creep (Yellow)).
- Новое проклятие №8 Curse of the Giant (curses.xml): по вики в REP+ оно, как и в Repentance, не выпадает само (только консолью, curse 128), код объединения комнат есть и в 1.7.9b; переносить нечего, кроме строки в curses.xml (—).
- Новая giantbook-анимация (giantbook.xml, запись 47, GiantBook_047_Necronomicon_Powered.png): ✓✓ 0.6.83 (на консоли: при заряженном Necronomicon на новом этаже показывается красноглазая книга). В REP+ ItemOverlay::Show(47, 3) на новом этаже (0x7a5e6b, 0x7a5f15) и при возврате формы Necromancer с заряженным слотом (0x75e9f5). В Switch таблица ItemOverlay на 47 записей (0-46), поэтому показывается запись 1 (Necronomicon), а её картинка на время вызова подменяется на картинку 47.
- 21 новый малыш (babies.xml: Lucky Baby, Baby Named Finger, Sucky Baby и др.): облики для онлайн-коопа, ✗ (можно как косметику).
- recipes.xml: восемь бомб в Bag of Crafting дают Mr. Mega (106) вместо Mr. Boom (37); ✓✓ 0.6.75 (запись в std::map рецептов ItemConfig+0x1f8; на консоли GetCraftingOutput для 8 бомб вернул 106).
- music.xml: громкость 63 треков изменена (mul/layermul, в основном тише), ✓ 0.6.76 (значения в памяти игры сверены на консоли; на слух не проверялось).
- 258 новых звуков (sounds.xml, id 833 и дальше). Нужны ресурсы REP+, ○.

## 8. Поведение предметов

Меняется кодом игры. Берётся из кода exe REP+, а не из вики.


| Предмет | Изменение | Статус |
|---|---|---|
| Tammy's Head | короче анимация | ✗ вики: в REP+ изменилось только качество (уже в items_metadata) |
| Lemon Mishap | лужа больше, держится до выхода из комнаты (Entity_Effect::Update 0x5f13ad: Timeout+1 каждый кадр у лужи игрока; размер и рост 15/65 те же, что в Repentance) | ✓✓ 0.6.22 (repplus.lua) |
| Mega Bean | пердёж на всю комнату как Mama Mega! (открывает Boss Rush/Hush). В exe REP+: UseActiveItem 0x5bc52b спавнит новый эффект 1000.202 (Mega Bean Explosion, звук 0x365) вместо 1000.72 и не бьёт камни сам; логика эффекта новая, в Entity_Effect::Update (0x5d0421, 0x5fb2a9 и др.), на Switch её нет . Подробнее: UseActiveItem 0x5bc52b: тряска экрана (0x703ec0, 160, x2), пердёж радиусом 200 (0x704640), эффект 1000.202 на игроке, с Book of Virtues огонёк 351. 1000.202 (MEGA_BEAN_EXPLOSION в enums.lua REP+) делит инициализацию и обновление с 1000.127 (MAMA_MEGA_EXPLOSION, 0x5cf49b → 0x5d040c, звук 0x365 при появлении); в цикле урона (0x5fb2a9..0x5fb3f4) 202 бьёт каждого только раз (127 бьёт повторно), урон с флагами 0x1000004 и задержкой 30, и ещё 0x6ad6b0(-50) и 0x6ad7f0(90, ...) (вероятно, яд и ещё один статус) | ✓✓ 0.6.40 (repplus.lua): волна камней 1000.72 убрана, вместо 1000.202 на Switch спавнится 1000.127 от игрока с уроном 100 (по вики); на консоли проверено: волны нет, по 100 урона каждому врагу один раз, игрок не ранен, окаменение и пердёж 1.7.9b остались. Яд/статус 0x6ad7f0 из цикла 202 не повторён |
| Milk! | блокирует выстрелы (CanBlockProjectiles 0x612b80, пока цел), считает удары в SubType до 10 (TakeDamage 0x62d4b4, поле +0xf38 = 10 из Init 0x60d644), на 10-м разбивается как в 1.7.9b и даёт +1 скорострельности до конца этажа, на новом этаже снова цел (в 1.7.9b ломался от первого удара и возвращался в следующей комнате). На Switch удары считаются в repplus.lua (первые 9 отменяются со вспышкой, заблокированный выстрел тоже удар), блок натив | ✓✓ 0.6.38 (натив CanBlockProjectiles, repplus.lua), на консоли проверены блок, поломка на 10-м выстреле, +1 скорострельности в другой комнате, восстановление на новом этаже |
| Delirious | боссы сохраняются между комнатами, один за раз, теряют здоровье, особый урон | ✓✓ 0.6.72 (repplus.lua, по вики): дружественный босс переходит с игроком в следующие комнаты с тем же здоровьем, пока не убит; новое использование убивает прежнего босса и его дружественных монстров; эти монстры теряют здоровье до 1 и сереют (скорость на вики не указана, взята 1/300 макс. здоровья за кадр обновления) |
| Abyss | урон саранчи от качества предмета, саранча наследует эффекты | ✓✓ 0.6.72: locusts.xml REP+ (367 строк) переведён в формат 1.7.9b (tools/repplus/gen_locusts.py → resources-dlc3/locusts.xml: число, размер по качеству, скорость, урон, цвета, флаги без procChance), спрайты anm2 из afterbirthp.a ставит repplus.lua (locusts_data.lua). Множитель урона по качеству (x0.5/0.75/1/1.5/2 для качества 0–4, REP+ 0x628a73; саранча Apollyon's Best Friend считается качеством 3 = x1.5, 0x64bb60, патч 1.9.7.9) в 0.6.112 (MC_ENTITY_TAKE_DMG); ✓✓ консоль: удар 3.5 → Brimstone 7.0, Blood of the Martyr 5.25, Best Friend 5.25 (в 1.7.9b все 3.5). 0.6.114: эффекты с шансом (tearFlags2 / damageMultiplier2 / procChance) переносятся как есть: парсер 1.7.9b (0x3bc43c) понимает их так же, как REP+ (0x72afa0); строке REP+ с tearFlags2 без procChance ставится шанс 1 (иначе 1.7.9b обнуляет шанс и эффект не срабатывал: Sulfuric Acid, Contagion, Mucormycosis и др.). ✓✓ консоль: саранча Mucormycosis отравляет врага. locustFlags (особые эффекты: D1, Depression и т. п., 86 строк) не сделаны |
| Flip | заряд 6 комнат (items.xml), у Tainted Lazarus 4 (GetActiveMaxCharge 0x7915fc) | ✓✓ 0.6.18 (натив) |
| Portable Slot | в exe REP+ (UseActiveItem 0x5b74d5) награды те же, что в 1.7.9b (Switch 0x308590); новое только звуки: 898 portableslot_coin при каждом использовании (0x5b7539) и 899 portableslot_prize с каждой наградой (0x5b75a0..0x5b7a53) | ✓✓ 0.6.39 (repplus.lua, звуки REP+), на консоли проверены оба звука |
| Epiphora | скорострельность растёт плавно: x(1 + счётчик/180) до x2 за 180 кадров стрельбы, пересчёт каждый кадр (0x780448, 0x76575f) вместо ступеней раз в 90 кадров | ✓✓ 0.6.19 (натив) |
| God's Flesh | шанс уменьшения x2 (REP+ GetTearHitParams 0x7c1503: Random % 5 вместо Random(10); натив, Switch 0x2aebd0; ✓✓ 0.6.58, на консоли 7 из 39 слёз), вспышка и «уменьшенные не вредят при смерти» (○, не найдено в exe) | ◐ |
| Hushy | пока стоит (игрок стреляет), раз в 31 кадр выпускает кольцо из 8 слёз по 6 урона (Entity_Familiar::Update 0x61f807: счётчик +0xd4c от 30, FireProjectile 0x6399e0 под углами i×45°, урон 6 у варианта 107 в 0x639fa9); вики: 8 снарядов по 6 урона раз в 1.033 с | ✓✓ 0.6.38 (repplus.lua), на консоли проверены кольца по 8 слёз раз в 31 кадр, урон 6 |
| It Hurts | после урона слёзы оставляют кровь (вики). В exe REP+ (TakeDamage 0x7763dd) только одно отличие: в Greed/Greedier залп срабатывает 3 раза (счётчик Player+0x2ed0, сбрасывается только при создании игрока) | ✓✓ 0.6.26 (repplus_hooks.inc) |
| Mars | 1 с неуязвимости после рывка, огненное кольцо при ударе о препятствие. В exe REP+ 18 мест с Mars против 13 в Repentance; новые обрывают рывок (RemoveCollectibleEffect 593) при ударе о врага с отскоком (0x78fab5) и у края комнаты (0x7896b1), ещё 0x79aad2..0x79bc7f | ✓✓ 0.6.41 (repplus.lua): новая функция REP+ 0x7c85c0 (8 огненных столбов 1000.147 по кругу 40 px, урон 10, дружественные, только внутри комнаты; враги в 80 px отталкиваются) и конец рывка при ударе о стену и при отскоке от врага. На консоли проверено: столбы у стены и после удара о врага, игрока не ранят. 0.6.44: после удара 0x7c85c0 ставит Player+0x13bc = 30, и коллизия автомата 0x662d69 не берёт плату здоровьем, пока он >= 15: на Switch урон от донорского автомата, Devil Beggar, Hell Game и исповедальни отклоняется 15 кадров после удара (на консоли первый удар автомата пришёл на +39 кадре против +1 без удара). Donation Machine + Karma (rng(3000) < 3·(3c − min(3c, m))) не сделано: разница 1 из 3c |
| Evil Charm | иммунитет к паутине, огню, костру, грибам; 1 с в луже без урона (0x7dbdb0 из TakeDamage 0x773124) | ✓✓ 0.6.21 костры и лужи (repplus.lua); ✓✓ 0.6.74 паутина (REP+ GridEntity_Web::Update 0x721b31; натив: Entity::AddSlowing из GridEntity_Web::Update 0x397390 пропускается для игрока с предметом или его временным эффектом; на консоли паутина без предмета замедляет, с ним нет) |
| Vasculitis | выстрелы в виде сгустков крови. В exe REP+ (0x6bd23a..0x6bda23) слёзы из умершего врага: вариант 26 (Booger) с цветом (2, 0, 0, 1, офсет 0.2, 0, 0) вместо 1 (Blood); горящие (5) и ледяные (41) как были | ✓✓ 0.6.45 (repplus.lua), на консоли: 3 слезы варианта 26 с красным цветом, обычные выстрелы не тронуты |
| Lil Portal | полная переделка: двойное нажатие, портал в комнату хранения предметов (BackdropType 62) | ✓✓ 0.6.80 (на консоли проверено, repplus.lua): портал открывается после 1 + номер этажа пикапов, 4 размера распределены по этому числу, урон 2 × размер (через счётчик Coins, который 1.7.9b ai_chubby 0xb44f4/0xb46e0 растит на 100 за пикап). ✓✓ 0.6.79 (на консоли): портал ведёт в фиолетовую спальню (комната -3 с раскладкой стартовой, фон portal_bedroom.png через свободный слот фона 55), там выход назад и содержимое при первом входе за забег; после портала фамильяр скрыт до конца этажа. ✓✓ 0.6.80 (на консоли): фамильяр летит только после двойного нажатия в одну сторону (одиночный выстрел полёт отменяет, флаг полёта ai_chubby Familiar+0xd60 = -1); в спальне нет дверей; оставленные пикапы запоминаются и возвращаются на следующих этажах (открытые сундуки, пустые пьедесталы и товары не хранятся). Разбор спальни по exe REP+ (Room::Init 0x803756, комната вне сетки ROOM_LIL_PORTAL_IDX = -20, фон 62): при каждом входе, если в комнате нет эффекта 1000.161, он ставится у центра с подтипом 1000 + номер комнаты, откуда пришли (выход назад); при первом входе по SpawnSeed комнаты (xorshift 5/9/7) три броска: сундук в левом верхнем углу (0–3 вечный 5.53, 4–8 шипастый 5.52, 9–18 бомбовый 5.51, 19–28 красный 5.360, 29–53 запертый 5.60, иначе обычный 5.50), в правом нижнем 0–24 Dice Shard, 25–49 Butter!, 50–59 Soul of Eden, 60 Moving Box (последние три только если открыты), в левом нижнем с шансом 1/4 батарейка 5.90 |
| Linger Bean | облако каждые 240 кадров стрельбы вместо 450 (✓✓ 0.6.10, натив); облако живёт 300 кадров после PoofStart, растёт с 1.0 до 2.1 (радиус урона 50 × размер), урон от 1.7 до 0.58 урона игрока (REP+ Entity_Effect::Update, вариант 105, 0x6010ac; ✓✓ 0.6.74 repplus.lua, на консоли: размер 1.04 → 1.79, урон за 30 кадров 17.7 → 9.8, облако живёт 316 кадров) | ✓✓ |
| Jupiter | лечит 2 красных сердца вместо ½ | ✓✓ 0.2.0 (данные; на консоли 0.99.135: 1 → 3 сердца) |
| Dead Bird | остаётся между комнатами при здоровье ≤ ½ сердца, у Eve ≤ 1 (кости + синие + красные при контейнерах; TemporaryEffects::ClearEffects 0x930c1d) ✓✓ 0.6.12; у The Lost и Tainted Lost включён всегда (0x79d0c6) ✓✓ 0.6.11 | ✓✓ (main.lua) |
| Dataminer | после обмена статами каждый пьедестал в комнате (5.100, не пустой, не квестовый, тег 0x8000) становится новым глитч-предметом (UseActiveItem 0x5c04f0: ProceduralItemManager::CreateProceduralItem с RNG Dataminer, как у TMTRAINER) | ✓✓ 0.6.39 (натив core.repplus_proc_item, repplus.lua), на консоли проверены глитч-предмет вместо обычного, The Negative не тронут, подбор |
| Parasitoid | яйца от Technology и Brimstone | ✓✓ 0.6.72 (repplus.lua): удар лазером игрока с Parasitoid берёт тот же ролл Random(max(7 − удача, 2)) == 0, что слёзы (GetTearHitParams), и даёт белую замедляющую лужу и 1–2 синих мухи или паука |
| Meat Cleaver | на ангеле (Uriel/Gabriel) только один ключ (патч 1.9.7.11; в 1.7.9b обе половины роняют свой кусок ключа) | ✓✓ 0.6.112 repplus.lua: после удара тесаком при ангеле в комнате второй кусок того же вида от ангела убирается; консоль: было 238 238, стало 238 |
| Card Reading | зелёный портал в магазин среди порталов стартовой комнаты (патч 1.9.7.9) | ✓✓ 0.6.112 repplus.lua: при первом входе в стартовую комнату, если на этаже есть магазин, он попадает в пару с шансом 2/(k+1) (k = виды из 1.7.9b: сокровищница, босс, секретная), заменяя случайный портал игры; портал 1000.161 с SubType 1000 + индекс магазина, окрашен в зелёный. Консоль: стадии 1–8 — магазин на 3 из 6 этажей с магазином, в Womb нет; вход в портал → комната типа 2. Цвет взят зелёным тоном, не спрайтом REP+ |
| Зеркало Downpour / Dross II | разбивший зеркало получает −7 удачи (патч 1.9.7.13, REP+ 0x70ff3b) | ✓✓ 0.6.113 repplus.lua (флаг состояния 23 + MC_EVALUATE_CACHE). Консоль: удача 0 → −7 |
| Двойная ангельская комната | две статуи дают Уриила и Гавриила, а не двух одинаковых (патч 1.9.7.13) | ✓✓ 0.6.113 repplus.lua (MC_PRE_ENTITY_SPAWN). Консоль: статуи → 272, 271 |
| Bob's Bladder | засчитывается в трансформацию Bob (вики) | ✓✓ 0.6.113 repplus.lua (increment_player_form_counter). Консоль: счётчик 2 → 3 (Bob), без брелка снова 2 |
| Astral Projection | срабатывает в Boss Rush (вики) | ✓✓ 0.6.113 натив: TakeDamage проверяет флаг «комната зачищена» (Switch 0x2a83a4), в Boss Rush он стоит всю битву; на время проверки снимается, пока идёт Boss Rush. Консоль: удар в Boss Rush → призрак |
| Punching Bag | блокирует выстрелы врагов (вики «Added in Repentance+», патч 1.9.7.11) | ✓✓ 0.6.113 repplus.lua: вражеский снаряд, коснувшийся мешка, исчезает. Консоль: раньше пролетал насквозь |
| Forget Me Now | после перезапуска Сундука / Тёмной комнаты дверь к Mega Satan остаётся открытой (вики, патч 1.9.7.11) | ✓✓ 0.6.113 repplus.lua. Консоль: дверь открыта, комната Mega Satan сброшена |
| Book of the Dead | Bone Fly, Big Bony и Revenant без условия «умереть от них хоть раз» (вики) | ✓✓ 0.6.113 натив: GetBestiaryDeathCount в UseActiveItem (Switch 0x309b20/0x309b94/0x309c20) отвечает 1 |
| Sulfuric Acid / Terra | лазеры иногда выбивают двери (вики) | ✓✓ 0.6.113 repplus.lua: как do_damage_grid 1.7.9b (Switch 0xfd988, шанс 1/4 на тик урона) для закрытой двери под лазером. Консоль: 10 выстрелов без предмета — дверь закрыта, с Sulfuric Acid — выбита |
| Брелки саранчи (113–117) | саранча на каждой волне Испытания, Boss Rush и Greed (патч 1.9.7.9) | ✓✓ 0.6.113 repplus.lua: REP+ вынес код в функцию 0x7c8bb0 и зовёт её после каждой волны (0x8337d0, 0x833ff0, Greed 0x816cc0). Консоль: Испытание, саранча на волнах 1 и 2; Greed не проверялся |
| Azazel's Rage, Poke Go, тузы и Blood Puppy, Bot Fly, I AM ERROR на Alt Path | (вики / патчи) | — на Switch уже как в REP+ (консоль: Azazel's Rage копится за каждую волну, Poke Go Random(5), туз не трогает 3.241, Downpour I → ERROR → Dross II) |
| The Stairway у T.Lazarus, Birthright у The Lost | (вики) | ✓✓ через данные REP+ (теги lazarussharedglobal / nolostbr уже применены, консоль) |
| Hive Mind | урон саранчи Abyss x2, как у мух и пауков (Familiar::GetMultiplier 0x60dfa0) | ✓✓ 0.6.85: в 1.7.9b это уже так (саранча помечена как муха в EntityConfig), на консоли без перехвата удар саранчи 3.5 → 4.38 с Hive Mind, как с BFFS!; перехват 0.6.16–0.6.84 удваивал второй раз (6.12), убран |
| Flush! | убивает Turdlings | ✓✓ 0.3.0 (на консоли 0.99.136: два Turdlet после Flush! исчезли) |
| Plan C | смерть через 300 кадров в комнате Delirium и 345 у Mother вместо 90 (UseActiveItem 0x5be295) | ✓✓ 0.6.20 (repplus.lua) |
| Mega Blast | Entity_Player::FullCharge (0x7c3cbd): максимум/2 вместо /4, то есть 6 вместо 3 (48 Hour Energy, предметы-батарейки; Lil Battery и так даёт 6) | ✓✓ 0.6.11 (натив) |
| Car Battery | взаимодействия с Gello (второй Gello), Red Candle / The Candle (второе пламя), Mega Blast (луч дольше); Doctor's Remote (вторая ракета: число ракет x2 в Weapon_Rockets::Fire 0xa0950f для оружия без слота). Book of Belial и Telekinesis с Car Battery работают уже в Repentance (вики), в exe REP+ нового кода для них нет | ✓✓ 0.6.37 (repplus.lua; Doctor's Remote натив), на консоли проверены два Gello и две ракеты |
| Odd Mushroom (Thin) | по коду REP+: +1 +0.7×(копии+эффект) к выстрелам в секунду после формулы слёз (0x764fbd), а в сумму слёз до формулы гриб больше не входит (вики: «пробивает мягкий кап») | ✓✓ 0.6.84 (натив; до 0.6.84 бонус шёл в сумму слёз до формулы; на консоли 0.99.136: задержка 10 → 5.78 (1 копия), 4.85 (2 копии); у 1.7.9b было 5.25) |
| Sticky Bombs | Бомбы и так несут флаг TEAR_EGG (GetBombFlags, REP+ 0x780af9 и Switch одинаково); REP+ обрабатывает его в ApplyTearFlagEffects (0x67cfb8) для любого попадания, в том числе взрыва: белая лужа, звук BOIL_HATCH, 1-2 синих мухи или паука на каждого задетого врага | ✓✓ 0.6.95 (Lua; на консоли 0.99.147: 6 взрывов по врагу с 500 HP — 1-2 мухи или 1 паук каждый раз; Parasitoid-лазер теперь через ту же функцию) |
| Jar of Wisps | AddCollectible поднимает счётчик банки до 2 (REP+ 0x7621de, в 1.7.9b до 1); каждый огонёк с шансом 0.67 (0xbaa3ac) — случайный из списка активных предметов с огоньком, иначе огонёк Book of Virtues (1.7.9b берёт любой id, почти всегда обычный); заряд 12 уже в данных | ✓✓ 0.6.96 (натив + Lua; на консоли 0.99.148: 4 использования дали 2, 3, 4, 5 огоньков, особых 2/2, 2/3, 3/4, 3/5) |
| Percs! (лошадиная) | Кроме эффекта The Wafer на комнату ещё AddHearts(20) (REP+ UsePill 0x7c68ee) | ✓✓ 0.6.97 (Lua; на консоли 0.99.149: 0 → 12 из 12, обычная не лечит) |
| Hagalaz | После камней (UseCard 0x7b4389 → 0x5b28e3): 27 каменных врагов убиваются (Stoney, Stone Eye, турели, Brimstone Head, Gaping Maw, Quake/Bomb Grimace, Poky, Grudge, Ball and Chain, Spikeball, Rock Spider, Hardy, Blaster и др.), 10 бронированных превращаются (Host и Hard Host → Red Host, Mobile Host → Flesh Mobile Host, Knight/Floating/Bone Knight → Brain, Selfless/Black Knight → Pon, Death's Head → Flesh Death's Head) с пылью | ✓✓ 0.6.97 (Lua; на консоли 0.99.149: 11 врагов — все по списку). Не перенесено: Rune Shard с Hagalaz (25% на врага) и байт +0x3b0 у Death's Head |
| D4 / D100 / Dice Room / Missing No. | CanRerollCollectible в REP+ (0x7ce620) оставляет стартовые пассивки: Каин Lucky Foot, Ева Dead Bird и Whore of Babylon, Самсон Bloody Lust, Лазарь Anemic, The Lost Holy Mantle, Лилит Incubus и Cambion Conception, Вифания Book of Virtues (кроме Clicker) | ✓✓ 0.6.98 (натив, хук CanRerollCollectible у RerollAllCollectibles; на консоли 0.99.150: D4 за Каина, Еву, Лилит, Вифанию — пассивки остались, Sad Onion перебросился; с выключенным REP+ Lucky Foot пропадает) |
| Замороженные враги: эффекты при убийстве (Lusty Blood, Charm of the Vampire, Jar of Flies, Samson's Lock, Bird's Foot, Bat Wing, Sigil of Baphomet, Vasculitis, Death's List, Berserk!, Red Stew, 4.5 Volt, Jumper Cables) | REP+ 1.9.7.11 | ✓✓ консоль: Lusty Blood +1 за убийство с FLAG_ICE, без двойного срабатывания |
| Eden's Blessing: не больше 2 предметов в следующем забеге | REP+ 1.9.7.9 | ✓✓ консоль: счётчик 5 -> 2 предмета |
| Battery Bum: заполняет все активки до перезаряда; заряжает карманную активку без обычной | REP+ 1.9.7.11 | ✓✓ консоль |
| Glitched-активки стартуют полностью заряженными | REP+ 1.9.7.6 | ✓✓ на Switch уже так (консоль: пьедесталы и D100) |
| Золотые сердца: The Lost / T.Lost / Keeper / T.Keeper разбивают их в монеты при касании; Keeper не превращает их в мух | REP+ 1.9.7.13 (exe 0x7ca0c2, 0x6e436d) | ✓✓ консоль: Keeper и Lost +5 монет, Isaac берёт сердце как раньше |
| Keeper / T.Keeper: лечение монетами засчитывается в Immaculate Conception | REP+ 1.9.7.13 (exe 0x7597d1) | ✓✓ консоль: 16 лечений монетами дали бонус IC |
| T.Bethany: гнилое сердце даёт 1 заряд крови и подбирается | REP+ 1.9.7.13 (exe 0x6e9ae2) | ✓✓ консоль: заряд 6 -> 7 |
| T.Cain: предмет в Ultra Secret Room всегда даёт Cracked Key | REP+ 1.9.7.13 (по вики; в REP+ внутри SalvageCollectible) | ✓✓ консоль: 3 из 3 разборов с ключом |
| The Bean / Kidney Bean: огоньки Book of Virtues исчезают при смене комнаты | REP+ 1.9.7.11 | ✓✓ консоль |
| Clicker: последний предмет убирается с шансом 50% (было всегда) | REP+ 1.9.7.11 | ✓✓ консоль: 5 из 12 использований |
| Clicker: врождённые пассивки нового персонажа | REP+ 1.9.7.11 | ✗ не портировано |
| 1% тематический предмет босса (выбор с обычным предметом; брелоки отдельно) | REP+ 1.9.7.11 (exe 0x80e6c0, таблица 0x80ec84) | ✓✓ консоль (с принудительным броском): Monstro -> Monstro's Tooth рядом с обычным предметом |
| The Siren не зачаровывает Isaac's Heart и Incubus | REP+ 1.9.7.11 | ✓✓ консоль: CanCharm = нет для них, бой с Siren |
| Stone Eye: полный оборот перед первым лазером | REP+ 1.9.7.4 (по вики; у нас по кадрам: 180 кадров = 360°) | ✓✓ консоль: первый лазер на 180-183 кадре |
| Star of Bethlehem: усиление как у Hallowed Ground | REP+ 1.9.7.11 (exe 0x64e0f5: только AddHallowedGroundCountdown(2), без счётчика x1.5^n) | ✓✓ консоль: урон 3.5 -> 4.2 (было 6.3), задержка 10 -> 3.4 |
| The Ludovico Technique | +0.2 скорости выстрела за копию (REP+ 0x7679cb, тот же накопитель, что 2Spooky) | ✓✓ 0.6.94 (натив; на консоли 0.99.146: 1.0 → 1.2, с выключенным REP+ 1.0) |
| Black Feather | The Mark и The Pact считаются «злыми» (+0.5 урона за каждый, REP+ 0x766872..0x766925) | ✓✓ 0.6.94 (натив; на консоли: The Mark + перо 5.857 против 5.191 без REP+, The Pact 5.191 против 4.427) |
| 2Spooky | +0.5 к выстрелам в секунду (после формулы слёз, 0x7650a7), +0.2 скорости выстрела (до множителей) за копию, по коду REP+ | ✓✓ 0.6.84 (натив; в 0.3.0 было приближение, до 0.6.84 +0.5 шло в сумму слёз до формулы; на консоли 0.99.136: задержка 10 → 8.30, скорость выстрела 1.0 → 1.2) |
| Black Powder | взрывы больше (масштаб бомб 1.5 и урон 185, как у Mr. Mega; Entity_Player::GetBombScale 0x781263), костры (33, варианты 0, 1, 10), потушенные игроком, взрываются (0x6acd3e: BombExplosionEffects, урон 20, игрок не ранится; на Switch потушенный костёр остаётся с 1 HP без kill, поэтому взрыв по переходу в 1 HP после удара игрока) | ✓✓ 0.6.37 (натив GetBombScale, repplus.lua), на консоли проверены урон бомбы 185 и взрыв костра |
| Blood Rights | по коду REP+: пока действует, +0.33 +0.15×удары (счётчик Bloody Lust/Gust, теперь считает и у владельца Blood Rights) к выстрелам в секунду | ✓✓ 0.6.10 (натив) |
| Pageant Boy | при первом подборе к 7 монетам добавляется случайный доступный брелок-монета: Butt, Bloody, Burnt, Flat, Counterfeit, Rotten, Blessed, Charged, Cursed Penny (AddCollectible 0x7603b1) | ✓✓ 0.6.13 (main.lua) |
| Potato Peeler + Ball of Bandages | без Cube of Meat отнятый контейнер прокачивает Ball of Bandages (новый счётчик REP+ Player+0x1540, UseActiveItem 0x5bfacc); на Switch счётчика нет: уровни хранятся в repplus.lua (сохраняются с сидом забега), шарики проверяются как в EvaluateItems | ✓✓ 0.6.35 (repplus.lua) |
| Holy Card под другими святыми щитами | удар тратит щиты по порядку Wooden Cross, Blanket, Holy Mantle, Holy Card: карта сгорает последней и возвращает щит в каждой новой комнате, пока не потрачена (0x77446e; в 1.7.9b карта сгорала от первого же удара). В HUD у каждого источника своя иконка (ui_hearts.anm2 HolyMantleWoodenCross / Blanket / HolyMantle / HolyCard, 0x844f61): первый щит на месте после сердец, второй за ним полупрозрачный со сдвигом +6, -4; только для первого игрока | ✓✓ 0.6.77, иконки ✓✓ 0.6.78 (repplus.lua, gfx/ui/rp_holy_shields.anm2) |
| Wooden Cross + Mom's Box / золотой | при подборе щитов по уровню брелка до 2 вместо 1, при потере неиспользованные снимаются (0x7adc27 / 0x7adeef) | ✓✓ 0.6.35 (repplus.lua) |
| Telescope Lens + Mom's Box / золотая | планетарии и на 9-10 этажах (шанс планетария 0x74df40) | ✓✓ 0.6.35 (натив, FirstTrinketOwner в GetPlanetariumChance) |
| Hairpin + Mom's Box / золотой | заряд в комнате босса заполняет и батарейку (FullCharge с overcharge, 0x795309) | ✓✓ 0.6.35 (натив) |
| Orphan Socks | лужи (эффекты 22-26, 32, 37, 44-46, 53, 54, 56, 78, 90, 92, 94) не наносят урон (Entity_Effect::Update 0x5ebdfe..0x5f15b0); замедление белой лужей с Socks уже есть в 1.7.9b (Switch Entity_Effect::Update 0x9a670), REP+ добавил к нему только иммунитет Evil Charm | ✓✓ 0.6.35 (repplus.lua) |
| Necromancer (трансформация 15) | Necronomicon + Missing Page 2 + брелок Missing Page (0x75e6fe). На каждом новом этаже (Entity_Player::TriggerNewStage 0x7a5def) Necronomicon «заряжается» (VarData 1): 10 делений (0x79163c), использовать можно с 1 (0x791733), каждое использование тратит 1 (0x5b4977), тип заряда 2 (0x791880: комнаты и батарейки не заряжают), полный заряд со звуком 1049 и вспышкой; на 0 возвращается в обычный до конца этажа; без трансформации перестаёт быть заряженным (0x75e962). Значок hud_NecronomiconPowered, фиолетовая полоса заряда (HUD 0x846f2f) | ✓✓ 0.6.37 (натив GetActiveMaxCharge, GetActiveMinUsableCharge, ANM2::Render полосы; repplus.lua), на консоли проверены 10 делений на новом этаже, расход по 1, фиолетовая полоса и обычный режим после 0 |
| Spindown Dice, Keeper's Kin, Knife Piece 1, Jelly Belly, Guardian Angel, Swarm, Camo Undies | новые места в exe REP+: подсказка «Spins down into» встроенных описаний (есть EID), сетевой кооператив, Deathmatch, дубли функций; поведение одиночной игры не меняют | не переносится |

## 9. Брелки

| Брелок | Изменение | Статус |
|---|---|---|
| Tick | подобранный Tick можно поменять не только на Match Stick, но и на A Lighter (0x771a30) | ✓✓ 0.6.17 (натив) |
| Short Fuse | +15 урона бомб (×2 +30, ×3 +50), до множителей, не для Dr. Fetus (0x781d41); в 0.3.0 отметка была без кода | ✓✓ 0.6.47 |
| Tonsil | 6-12 попаданий вместо 12-20; миндалин до множитель+1 (было 2) | ✓✓ 0.6.9 (натив) |
| Bag Lunch, Wish Bone | шанс 1/20, ×2: 1/10, ×3: 1/7 (было 1/50) | ✓✓ 0.6.9 (натив) |
| Super Ball | шанс отскока 1/n: n = 10 (5 при множителе 2, 3 при множителе 3) минус удача/2, при n ≤ 0 всегда (GetTearHitParams 0x7c16fe) | ✓✓ 0.6.15 (натив) |
| Missing Poster | 33% открыть суперсекретку в новой главе | ✓ 0.6.84 (по exe REP+ TriggerNewStage 0x7a5c18: на каждом этаже, не только в новой главе, Random(100) < 33 / 66 / 100 при множителе 1 / 2 / 3+, на карте показывается значок суперсекретки (DisplayFlags | 4); до 0.6.84 было по вики: только новая глава и без золотого) ✓✓ на консоли 0.99.136: при множителе 3 значок есть на этажах 2 и 3, без брелка нет |
| Used Diaper | Skatole в комнате 33% вместо 15% (REP+ HasCollectible 0x770ccf; натив, Switch 0x27d6e4; ✓✓ 0.6.56, на консоли 30 из 78 комнат), синяя муха в боевой комнате (✓ 0.3.0) | ✓✓ |
| Rosary Bead | вики: сделка всегда ангельская. В exe REP+: убран +50% к шансу ангела (0x7f9a80), золотой даёт +5%/+10% к шансу сделки (0x7f98a5), HUD показывает весь шанс как ангельский (0x84c9c2); где сделка становится ангельской, ещё не найдено | ✓✓ 0.6.72 (repplus.lua, по вики): при зачистке комнаты босса с Rosary Bead ещё не выбранная комната сделки делается ангельской (Level::InitializeDevilAngelRoom(true, false)); +5%/+10% золотого сделано в 0.6.33 (§9a) |
| Modeling Clay / Error | показ предмета в Extra HUD: переплавленный брелок в списке предметов показывает то, чем он стал в этой комнате (HistoryHUD 0x83b74a) | ✓✓ 0.6.79 (repplus_hooks.inc rp_hist_item_c, на консоли Modeling Clay показал Ghost Baby) |
| Louse | синий паук за зачистку | ✓ 0.6.84 (по exe REP+ 0x7a97b7: один паук на держателя при зачистке, золотой не удваивает; до 0.6.84 был паук на каждый множитель) ✓✓ на консоли 0.99.136: один паук при зачистке, без брелка ни одного |
| Karma | бонусы у нищих и автоматов. В exe REP+ Karma (85) учитывается вместе с 0x12/0x5e/0x32e в коде нищих и автоматов (0x458599, 0x45ec9c, 0x45f520, 0x460141, 0x469811, 0x47029e, 0x470f06, 0x48262b): много мест | ✓✓ 0.6.42 (repplus.lua) по Entity_Slot коллизии REP+ 0x662a70: после оплаты 1 из 3, затем ролл: нищий 0.4 монеты назад / 0.8 +1 красное сердце / 0.95 удача; Restock 0.15 удача; донорский автомат, Devil Beggar, Hell Game, исповедальня 0.4 новый нищий / 0.8 сердце / 0.95 удача (всё × множитель брелка). На консоли: 25 подач нищему дали лечения, донорский автомат дал нищего. Меньше заклиниваний Donation Machine не сделано |
| Equality! | +2×множитель к выстрелам в секунду (после формулы слёз), пока монет = бомб = ключей (временный эффект брелка, как в REP+). Не перенесено: учёт во время подбора (брелок над головой) | ✓✓ 0.6.84 (натив; на консоли 0.99.136: задержка 10 → 5.35, без равенства 10; до 0.6.84 бонус прибавлялся дважды: через временный эффект из main.lua и через проверку монет в repplus_hooks.inc, в REP+ это «или», 0x76502a..0x76508f) |
| Lazy Worm | +0.5 урона | ✓✓ 0.6.84 (на консоли 0.99.136: урон 3.5 → 4.0; на 0.99.135 давал +1, прибавка была и в main.lua мода, и в repplus.lua) |
| Mom's Toenail | Mom топчет место сброса. В exe REP+ нет ванильного таймера 600/множитель (Repentance 0x75d1a1), новый код ещё не найден | ✓✓ 0.6.72: таймер убран (repplus_hooks.inc, GetTrinketMultiplier(16) на возврате 0x2cdd44 даёт 0); сброс брелка в бою ставит ногу Mom на его место, золотой и Mom's Box +1 нога каждый (repplus.lua, по вики) |
| Child Leash | +0.25 к множителю урона фамильяров за каждый множитель брелка (Familiar::GetMultiplier 0x60dfba) | ✓✓ 0.6.16 (натив) |
| Purple Heart | больше чемпионов, 50% лут, лут x2. В exe REP+: шанс чемпиона x3·множитель вместо x2 (0x6bbbeb, Switch Entity_NPC::load_entity_config 0x108aa0); лут чемпиона не меньше 50/66/75% (0x6bf481) и x2 (0x6bfc25, 0x6bfd8b, 0x6bffc4, 0x6c0270; Switch deathspawn_champion) | ✓ 0.6.25 шанс, 0.6.27 шанс лута в Hard (RNG::Random(3) у Switch 0x10bbb8 → Random(100) < 33/50/66/75) (repplus_hooks.inc); лут x2 ✓✓ 0.6.60 (repplus.lua: дроп чемпиона с цветом красный, жёлтый, зелёный, оранжевый, пульсирующий, крошечный, гигант или король получает ещё один такой же; на консоли зелёный и оранжевый дали по два) |
| Poker Chip | ✓✓ 1.0.3 / 0.6.111. Автоматы (натив, REP+ Entity_Slot::Update 0x65d480, в ПК 1.7.9b брелка там нет; Switch 0x32905c): слот-машина берёт таблицу Lucky Foot (Random(15) вместо 21) при Next() & 3 == 0 (0x65e490); кран выигрывает при Next() % 100 − 5 < 25 вместо Random(4) == 0 (0x65ea36); гадалка — шанс ×0.46 как с Lucky Foot при Random(5) == 0 (0x65fbbd); напёрстки и автомат обновления магазина — после промаха ещё Random(20) == 0 (0x660e7d, 0x6623cc). Золотой / с Mom's Box: пикапы выплаты автоматов и попрошаек ×min(множитель, 3) (0x65f0c1, помощник 0x665a30; Switch: копии в Game::SpawnEx из Entity_Slot::Update). Предмет качества 0–2 из сундука перевыбирается с качеством +1 из пула Golden Chest, иначе Treasure, не в Chest / Dark Room (по вики, Lua; в exe это место не найдено). Консоль: все 5 мест срабатывают; золотой: 9 пикапов за 4 выплаты (без REP+ 1 за 1); сундуки: 2 из 5 предметов подняты (343 q1 → 355 q2, 571 → 370 q3). (Код 0x6dff64, сделанный в 0.6.24 под этим именем, на деле относится к брелку 165 Nuh Uh!, см. §9a.) | ✓✓ |
| Blessed Penny | (нет на вики) шанс полсердца души по таблице: брелок x1 0.17/0.30/0.60/0.83, x2 0.25/0.40/0.70/0.90, x3 0.30/0.50/0.75/1.0 для монеты 1/2/5/10 вместо 1-0.8334^ценность (Entity_Pickup::handle_collision 0x6eae42) | ✓✓ 0.6.23 (repplus_hooks.inc) |
| Cursed Penny | выше шанс телепорта. В exe REP+ переписан целиком: подбор запоминает подтип монеты (Player+0x13b8, 0x6eb08d), телепорт (0x784a3e) выбирает тип комнаты по подтипу и шансу 50% (+25/+50 у золотого) | ✓✓ 0.6.33 (repplus.lua). Телепорт 0x784a3e по подтипу монеты (бонус b: золотой x2 +25, x3 +50; броски RNG брелка): пенни < 50+b непосещённая незачищенная обычная комната, иначе любая из типов 1 2 4–9 11 12 13 20 21 22 24 18; никель < 50+b типы 2 4 9 11 12 24, иначе 1 5 6 7 8 13 20 21 22 18; дайм < 25+b комната сделки (50% ангел, Level::InitializeDevilAngelRoom, индекс -1), иначе типы 1 2 4–13 20 21 24 18 и комнаты -2 (I AM ERROR), -6 (чёрный рынок); двойная < 50+b 20 21 18, иначе 1 2 4–9 11 12 13 24 и -6; счастливая < 50+b 7 8, иначе 1 2 4–9 11 12 13 20 21 24 18 и -6; золотая < 10+b комната -2, иначе 1 2 4–9 11 12 13 24 20 21 18 и -6; липкий никель 50% проклятая (10), иначе босс (5). Нет комнаты: телепорт 1.7.9b. На Switch телепорт 1.7.9b (UseActiveItem 44/419) отменяется в MC_PRE_USE_ITEM после подбора монеты и заменяется. Не сделано: липкий никель создаёт проклятую комнату, если её нет (0x748810). На консоли: пенни → обычная, никель → магазин/жертвенная, дайм → комната ангела, золотая → секретная/жертвенная |
| Золотые брелки | эффекты для всех, у кого не было | ✓✓ все найденные в exe REP+ сделаны и проверены на консоли (0.99.137), список ниже (9a) |


### 9a. Брелки с новыми проверками золотого множителя

Вызовы Entity_Player::GetTrinketMultiplier (1.7.9b PC 0x78d4c0 → REP+ 0x7cba60) с номером брелка прямо перед вызовом; в скобках места в exe REP+. Брелок в списке, если в REP+ таких вызовов больше. Каждое место сверяется с кодом: часть оказалась перестройкой без нового эффекта.

- 4 Broken Remote: 1 → 2 (0x7849fe, 0x785850) — уже есть в 1.7.9b Switch (Telepills при множителе > 1)
- 6 Broken Magnet: 1 → 3 (0x6edaab, 0x6edaea, 0x6edaf8) — ✓✓ 0.6.34 (на консоли 0.99.126: ключ в другом конце комнаты обычный магнит не тянет, золотой тянет): притягивает как Magneto
- 7 Rosary Bead: 0 → 1 (0x7f98ba) — ✓✓ 0.6.33: +5%/+10% к шансу сделки
- 14 Callus: 1 → 2 (0x70a47a, 0x7740b0) — ✓✓ 0.6.34 (на консоли 0.99.134, шипы жертвенной комнаты: без брелка и с обычным −1 сердце, с золотым −½): ×2 полурона от шипов жертвенной комнаты (не в Greed), ×3 как The Wafer
- 15 Lucky Rock: 0 → 1 (0x71ba6c) — ложное срабатывание (только подсчёт по игрокам)
- 17 Black Lipstick: 0 → 1 (0x766934) — ✓ уже так в 1.7.9b на Switch: Black Feather считает его по множителю (EvaluateItems 0x28303c), разница только с ПК 1.7.9b
- 19 Paper Clip: 1 → 3 (0x6edcdd, 0x710db9, 0x711e14) — ✓✓ 0.6.75 (0.6.34 не открывал блоки касанием: на Switch это встроено в GridEntity_Lock::Update 0x387780; на консоли 0.99.127 золотой открывает блок без ключа, обычный нет; двери при ×3 не проверены): ×2 блоки с замком без ключа, ×3 запертые двери (вариант 1, 2) без ключа
- 28 Broken Ankh: 0 → 2 (0x7a1e3b, 0x7a1e4e) — ✓✓ 0.6.34: шанс воскрешения 22/33/50% (на консоли 0.99.137 при множителе 3: 13 воскрешений из 32 смертей, у 1.7.9b ожидалось бы около 6)
- 30 Pinky Eye: 0 → 1 (0x7bfc69) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: яд 18/150 → 30/150)
- 31 Push Pin: 1 → 2 (0x7bfe35, 0x7bfe4b) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: пробивание 14/150 → 27/150)
- 40 Red Patch: 0 → 1 (0x774c11) — ✓✓ 0.6.34 (на консоли 0.99.126: ярость в 10 из 60 ударов → 21 из 60 с золотым): шанс ярости Random(100) < 20/33/50 + удача×5 (Greed 10/20/30 + удача×2.5)
- 43 Cursed Skull: 0 → 1 (0x778585) — ✓✓ 0.6.34 (на консоли 0.99.133: обычный перенёс в обычную комнату, золотой оба раза в магазин): ×2 телепорт в случайную особую комнату (магазин, сокровищница и т. д.); ×3 ещё и при одном красном сердце без синих/чёрных (порог 1 + (множитель > 2), 0x7dd700)
- 49 Bloody Penny: 0 → 1 (0x6ead1d) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: дропы 8 → 11 за 25 монет)
- 50 Burnt Penny: 0 → 1 (0x6eab6f) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: бомбы 7 → 14 за 25 монет)
- 51 Flat Penny: 0 → 1 (0x6eac4b) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: ключи 7 → 10 за 25 монет)
- 52 Counterfeit Penny: 0 → 1 (0x6eae35) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: лишние монеты 11 → 35 за 25 монет)
- 54 Isaacs Head: 0 → 1 (0x639dd8) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: слёзы головы 3.5 → урон игрока 6.45): слёзы Isaac's Head с уроном игрока (×3: ×1.5) вместо 3.5
- 57 Soul: 0 → 1 (0x76efbf) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: душ 1 / 2 / 3 (с Mom's Box)): душ Blue Baby min(множитель, 3)
- 61 Left Hand: 0 → 2 (0x6dbfcf, 0x6dc5e7) — ✓✓ 0.6.72 (repplus.lua, по вики): при множителе 2+ открытый красный сундук даёт ещё один пикап, пауки (85) из него убираются. Точный лут из exe не разобран
- 66 Lazy Worm: 1 → 2 (0x76747f, 0x767ca5) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: урон +1.0 → +2.0): +0.5 урона × множитель
- 68 Super Magnet: 1 → 3 (0x6edb38, 0x6edbf3, 0x6edc01) — ✓✓ 0.6.34 (на консоли 0.99.133: ключ через комнату тянется как с Magneto; на Switch 1.7.9b обычный Super Magnet уже тянет так же, разницы не видно): притягивает как Magneto
- 71 Bobs Bladder: 1 → 2 (0x6a4391, 0x780c24) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: лужа от бомбы размер 1.0 → 1.5): лужа ×1.5 (×3: ×2 и дольше)
- 76 Poker Chip: 0 → 1 (0x65f0c1) — ✓✓ 0.6.111: пикапы выплаты автоматов ×min(множитель, 3), см. таблицу предметов
- 85 Karma: 3 → 8 (0x66192b, 0x661c39, 0x661c5e, 0x662c41, 0x662c61, 0x662c8a, 0x662ed0, 0x662ef2) — ✓✓ 0.6.43 (см. таблицу §9)
- 89 Child Leash: 0 → 2 (0x60dfeb, 0x60e001) — ✓✓ 0.6.16
- 94 Fish Tail: 0 → 6 (0x674ed7, 0x759d87, 0x7c8d10, 0x7c8e1c, 0x7c8f2b, 0x7c9097) — ✓✓ 0.6.33 (саранча)
- 95 Black Tooth: 1 → 2 (0x7c182d, 0x7c1847) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: зуб 30/800 → 50/800)
- 97 Tonsil: 0 → 1 (0x776bba) — ✓✓ 0.6.9
- 99 Super Ball: 1 → 2 (0x7c16e8, 0x7c1702) — ✓✓ 0.6.15
- 101 Dim Bulb: 5 → 6 (0x76493c, 0x765f26, 0x767a4e, 0x768445, 0x76916d, 0x76fa93) — ✓✓ 0.6.54 (натив, EvaluateItems после GetTrinketMultiplier(101)): при пустом заряде все 6 прибавок × множитель (слёзы +0.5, урон +1.5, скорость слезы +0.3, дальность +60, скорость +0.5, удача +2); на консоли ×2: урон 7.51, дальность 380, удача 4
- 102 Fragmented Card: 0 → 1 (0x73c88f) — ложное срабатывание (результат не используется)
- 103 Equality: 0 → 1 (0x76507f) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: задержка −6.71 → −7.67): +2×множитель к сумме слёз, пока монет = бомб = ключей (и у обычного)
- 106 Lost Cork: 1 → 8 (0x5e82f8, 0x5e83be, 0x5e844b, 0x5eb319, 0x5eb3df, 0x5eb46c, 0x5f128d, 0x638a17) — ✓✓ 0.6.61 (натив: HasTrinket(106) в Entity_Effect::Update, Switch 0x85db0 и 0x85e18; на консоли размер лужи 20 → 33/38/43, Scale 1.65/1.9/2.15; место 0x638a17 у игрока не перенесено): лужи игрока крупнее по множителю: ×1.65/1.9/2.15 вместо 1.65 и +1/+1.25/+1.5 вместо +1 к размеру; на Switch это 3 места в Entity_Effect::Update (0x85db8, 0x85e1c, 0x944e0) с разными ветками размера, не перенесено
- 108 Walnut: 0 → 1 (0x77335c) — ✓✓ 0.6.34 (на консоли 0.99.133: обычный при разрушении дал 4 пикапа, золотой 8): каждый дроп (брелок, сердце, ключ, монета) × множитель
- 111 Bloody Crown: 2 → 3 (0x7065fd, 0x73e717, 0x7f3fe6) — ✓✓ 0.6.34 (на консоли 0.99.133: сокровищница Womb 1 с обычной 1 предмет, с золотой 2; Womb 2 тоже 2): в Womb (7, 8) работает как More Options
- 113 Locust Of Wrath: 1 → 2 (0x7c8be1, 0x7c8bf7) — ✓✓ 0.6.33 (золотые саранчи)
- 114 Locust Of Pestilence: 1 → 2 (0x7c8cc6, 0x7c8cdc) — ✓✓ 0.6.33
- 115 Locust Of Famine: 1 → 2 (0x7c8dd2, 0x7c8de8) — ✓✓ 0.6.33
- 116 Locust Of Death: 1 → 2 (0x7c8ee1, 0x7c8ef7) — ✓✓ 0.6.33
- 117 Locust Of Conquest: 1 → 2 (0x7c8ff1, 0x7c9007) — ✓✓ 0.6.55: число = m + Random(m == 2 ? 5 : 4) × n (n = 1, с Fish Tail 2, с золотым Fish Tail 1/5: 3 или 4) вместо (Random(4) + 1) × (Fish Tail ? 2 : 1); на консоли ×2: 2-6 саранчи вместо 1-4
- 118 Bat Wing: 0 → 1 (0x7a07a9) — ✓✓ 0.6.46: шанс полёта при убийстве 1/10 (×2), 1/7 (×3) вместо 1/20; на консоли ×2: 13 полётов на 100 убийств
- 123 Filigree Feathers: 0 → 1 (0x8006b3) — ✓✓ 0.6.46: ангел (Uriel/Gabriel) кроме предмета роняет пол-души (×2, 5.10.8) или душу (×3, 5.10.3); на консоли ×2: 5.10.8 у Uriel
- 125 Extension Cord: 0 → 1 (0x79d9a5) — ✓✓ 0.6.49: урон луча (лазер 2.4 от спутника) 6 × множитель вместо 6; на консоли 6 (×1), 12 (×2)
- 129 Jaw Breaker: 1 → 2 (0x7c1913, 0x7c192c) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: зуб 14/150 → 32/150)
- 130 Chewed Pen: 1 → 2 (0x7c0170, 0x7c0189) — ✓✓ 0.6.34 (на консоли 0.99.126, обычный → золотой: замедление 15/150 → 23/150)
- 133 Short Fuse: 1 → 3 (0x781d33, 0x7a0fcb, 0x7a0fdc) — ✓✓ 0.6.47: бомба игрока получает +15 урона (×2 +30, ×3 +50) до множителей (0x781d41), не для Dr. Fetus; на консоли 100 → 132.25 (×1), 149.5 (×2); ×1.15 в этих числах был лишним: старый код main.lua мода (бомбы ×1.15) работал вместе с новым, убран в 0.6.84; на консоли 0.99.136: 115 (×1), 130 (золотой)
- 137 Myosotis: 0 → 1 (0x802f32) — ✓✓ 0.6.72 (repplus_hooks.inc): несёт 4/6/8 пикапов при множителе 1/2/3; число берётся в RNG::RNG на возврате 0x46d9ec из Room::Update. На Switch предел 4 стоит в Room::Update (0x46d9cc, счётчик w22 до RNG::RNG, возврат 0x46d9ec); правка готова, но на консоли перенос пикапов не удалось надёжно проверить (монеты переносились и без брелка), не выпущено
- 144 Brain Worm: 0 → 1 (0x7c2c33) — ✓✓ 0.6.48: ×2 25%, ×3 50% слёз получают TEAR_PIERCING и вид Cupid's Arrow (вариант 11/12); на консоли ×2: 7 из 21 слезы
- 146 Devils Crown: 0 → 1 (0x73e4d1) — ✓ 0.6.59 (RoomConfig::GetRandomRoom из Lua на MC_POST_NEW_LEVEL; консоль: этаж 3, комната типа 14, цены сердцами): ×2 25%, ×3 33%: сокровищница становится комнатой дьявола (RoomConfig, Level::load_room 0x73e4a1)
- 151 Flat File: 0 → 5 (0x4e7eee, 0x4f60ec, 0x58dd64, 0x58dd8b, 0x5ce71d) — ✓✓ 0.6.72 (repplus.lua): при множителе 2+ Grudge, Ball and Chain и шар Singe получают спрайт *_retracted.png из REP+ и урон касанием 0 (как в exe). Не сделано: эффект 0x5ce71d (анимации SummonWombDisabled нет в 1.7.9b)
- 155 Holy Crown: 0 → 6 (0x7065cc, 0x73a43c, 0x73a5f9, 0x73abab, 0x73e6e6, 0x7f3fb5) — ✓✓ 0.6.34: в Cathedral как More Options (на консоли 0.99.137: золотая корона в сокровищнице Cathedral даёт 2 предмета, без неё 1, в Sheol не действует)
- 159 Gilded Key: 0 → 1 (0x7fb140) — ✓✓ 0.6.50: выбранная награда за комнату становится сундуком (×2 10%, ×3 25%), сундук золотой по правилу Gilded Key; на консоли ×2: 6 из 48 наград
- 161 Wicked Crown: 0 → 5 (0x706596, 0x73a5cd, 0x73ab75, 0x73e6b0, 0x7f3f7f) — ✓✓ 0.6.34: в Sheol как More Options (на консоли 0.99.137: золотая корона в сокровищнице Sheol даёт 2 предмета)
- 165 Nuh Uh: 0 → 1 (0x6dff8f) — ✓✓ 0.6.24 (числился как Poker Chip; на консоли 0.99.137 при множителе 3: двойных сердец и бомб в наградах за комнату 6 из 32, без брелка 3 из 33): ×2 10%, ×3 20% сердце становится двойным (5.10.5), бомба двойной (5.40.2); в Lua только для награды за комнату, в REP+ при любом случайном выборе пикапа
- 171 Keepers Bargain: 0 → 3 (0x815665, 0x8156de, 0x816705) — ✓✓ 0.6.52 (натив, Room::GetShopItemPrice 0x4866a4): при множителе 2+ все сделки с дьяволом за монеты вместо 50%; на консоли ×2: 8 из 8 цен в монетах, ×1: 5 из 8. ✓✓ 0.6.57 ×3 (0x816705; натив, Room::TryGetShopDiscount 0x486ae4): в комнате дьявола 1 из 3 сделок считается как с ещё одним Steam Sale (выбор по хешу seed комнаты и слота, а не RNG REP+); на консоли ×3: цена 7 вместо 15
- 172 Cursed Penny: 0 → 1 (0x784a96) — ✓✓ 0.6.33
- 174 Number Magnet: 0 → 1 (0x7f9857) — ✓✓ 0.6.33: шанс сделки
- 175 Strange Key: 0 → 2 (0x6ecb8d, 0x7107f6) — ✓✓ 0.6.51: в Blue Womb (этаж 9) при множителе 2+ сундуки 5.60, 5.53, 5.55 открываются без ключа, как с Paper Clip (0x6ecb8d); на консоли ×2: открыт с 0 ключей, с 3 ключами ключ не потрачен. Двери с ключом на этом этаже (0x7107f6) не сделаны: в Blue Womb их обычно нет
- 180 Found Soul: 0 → 1 (0x76755c) — ✓✓ 0.6.82 (на консоли: душа 1.75 → 2.62 при уроне Исаака 3.5; в 0.6.34 не работало: у души хозяин в Parent, а не в SpawnerEntity, и урон считался до появления хозяина): урон души ×1.5 (×3: ×2)
- 184 Adoption Papers: 0 → 2 (0x814eaa, 0x816664) — ✓✓ 0.6.53 (натив, Room::TryGetShopDiscount 0x486a98): слот спутника (тип 18) при множителе 2+ стоит 6 вместо 2/3 цены; на консоли ×2: 6 монет вместо 10. ○ ×3 (0x814eaa): дополнительная ветка для магазина, не разобрана

## 10. Общие изменения игры

| Изменение | Статус |
|---|---|
| Гарантированный портал в Void после Lamb, ???, Mega Satan (если открыт) и после Mother (если видели концовку 21) | ✓✓ 0.6.28. Lamb/???/Mega Satan: в 1.7.9b Room::SpawnClearAward бросает RNG::Random(5)==0 (0x459184), REP+ 0x7fcd95 ставит портал без броска; остальные проверки те же (нет трофея, Void доступен: Level::IsStageAvailable(12)). Mother: REP+ 0x7fc2b2..0x7fc355 ставит портал под большим сундуком (+0,40), в 1.7.9b его нет; сделано в repplus.lua. Поле сохранения REP+ (PersistentGameData+0xa7c > 1) на Switch не найдено и не проверяется |
| Бомбовые камни делают мосты через пропасти | ✓✓ 0.6.28. Game::BombDamage строит мост, только если Room::DestroyGrid вернул true (0x355a84); бомбовый камень от Destroy только взводится (0x38fcf0) и возвращает false. С REP+ взведение бомбового камня там считается разрушением (repplus_hooks.inc) |
| Таблетки/карты у тейнтед-персонажей с вечным предметом кладутся за ним | ✓✓ 0.6.30. Entity_Player::add_pocketitem: в 1.7.9b новая карта/таблетка всегда в слот 0 (0x2e8a80), REP+ 0x7a3eba кладёт её в слот 1, если в слоте 0 карманный активный предмет. Сброс при полном кармане тот же. На консоли: с Dark Arts в кармане карта и таблетка встают вторыми, Dark Arts остаётся первым |
| Golden Bomb / Golden Key у Bomb Bum / Key Master: мгновенная выплата предметом | ✓✓ 0.6.29. REP+ 0x662ced/0x662d15: с золотым ключом/бомбой обычный ключ/бомба не нужны и не тратятся (на Switch Entity::IsForcedCollision в Slot::handle_collision отвечает true, 0x32f1f4). REP+ 0x66336f..0x663504: Key Master всегда платит, Bomb Bum даёт приз 3 без броска, золотой ключ/бомба снимаются; приз Key Master идёт по ветке Random(8)==0 и без Paper Clip (0x66145b, 0x6616b9). На консоли: без ключей и бомб оба дали предмет, золотые снялись. Слои золотого ключа/бомбы ✓ 0.6.34 (на консоли не проверено): anm2 и png Key Master / Bomb Bum из afterbirthp.a REP+ в моде, при оплате слой 2 (Coin) виден без, слой 3 (Gold) с золотым ключом/бомбой (REP+ 0x66337c) |
| Уничтожение Devil Beggar, Bomb Bum, Key Master: +35% шанс сделки | ✓✓ 0.6.29. REP+ 0x665507 ставит флаг уровня 0 (STATE_BUM_KILLED, +0,35 в Room::GetDevilRoomChance) для бомжей 4, 5, 18, 7, 9, 13; в 1.7.9b для 7 и 9 ничего, для 5 флаг 1. На Switch после Entity::BloodExplode в Slot::TakeDamage (0x330808). На консоли флаг ставится для Key Master и Devil Beggar. Флаг 1 у Devil Beggar оставлен (в шансах сделки его никто не читает) |
| Bomb Bum, Key Master при выплате: +10% шанс ангела | ✗ в exe REP+ нет: Slot::Update ставит флаг ухода попрошайки (Level::SetStateFlag 3, 0x428da0) только для Beggar, Battery Bum и Rotten Beggar, флаг 4 для Devil Beggar, как в 1.7.9b; шанс ангела (0x7f9bc9) читает те же флаги |
| Часть сделок дьявола оплачивается шипами (50% / 100%) | ✓✓ 0.6.32. Room::GetShopItemPrice 0x815b45: коллекционка с ценой сделки дьявола и тегом devilsacrifice (бит 33: 126, 157, 186, 225, 569, 672, 692, 695, 704) получает цену -10 (не с A Pound of Flesh и не при ценах в монетах). Под ней новая сетка шипов (тип 28, анимация PaymentSpike). Урон игроку от этих шипов (0x70b24c) открывает ближайший предмет с ценой -10: первый удар с шансом 50% (RandomInt(2) == 0) или всегда с Judas' Tongue x3, второй всегда. Открытие (SetPrice 0x6e2f12): цена 0, звук 80, облачко 1000.15. На Switch нет сетки 28 и цены -10: цена 0 с AutoUpdatePrice off, под предметом обычные шипы (8), подбор запрещён до открытия. На консоли: предмет закрыт, удар открывает (раз с 1-го, раз со 2-го), открытый подбирается |
| Новые эффекты лошадиных таблеток: I'm Excited!!!, I'm Drowsy, ???, Telepills, Amnesia, Percs!, Addicted!, Gulp! | ✓✓ 0.6.30–0.6.31 (все восемь). I'm Drowsy / I'm Excited!!!: лошадиная ставит флаг уровня 24 / 25 (REP+ UsePill 0x7c72e9 / 0x7c7334), и каждая комната этажа начинается как после таблетки (REP+ 0x7f7a99..0x7f7c1c): замедление (Room+0x78f0 = 0x7fffffff) или ускорение и таймер Excited у каждого игрока; сделано в repplus.lua, MC_USE_PILL теперь получает цвет таблетки 4-м аргументом, как в API REP+. На консоли: флаги ставятся, в новой комнате состояние часов 2, таймер и эффект 28 есть. Флаги 24/25 не попадают в сохранение 1.7.9b (после «Продолжить» эффект этажа пропадает). Gulp! (0x7c7396): перед переплавкой брелоки в руках становятся золотыми (| 0x8000), на консоли брелок 1 переплавился золотым (множитель 2). Percs! (0x7c68f7): ещё AddHearts(20), на консоли лечит. Addicted! (0x7c68c8): ещё AddBrokenHearts(1), на консоли +1 сломанное сердце. Telepills (0x7c5381 + 0x7ce2b0): вместо случайной комнаты случайная комната этажа типа 4, 2, 7, 8, 18, 20, 21, 12, 11, 24, 9, 13 (сначала непосещённые), RNG таблетки 19, анимация телепорта; в repplus.lua вторым переходом в том же кадре; на консоли три раза попал в сокровищницу, секретную и магазин (0.6.31). Amnesia (0x7c66ce): ещё сбрасывает опознание всех таблеток, если нет False PHD, PHD, Virgo, Lucky Foot или Zodiac на Деве; на консоли 13 → 0, с PHD остаются 13 (0.6.31; добавлен ItemPool:UnidentifyPill из API REP+). ??? (0x7c694c): после Curse of the Maze ещё одно проклятие, которого на этаже нет, из Darkness/Lost/Unknown/Blind (у REP+ RNG уровня, здесь RNG таблетки 31); на консоли 64 → 100 (0.6.31) |
| Боссы: 1% шанс дополнительного тематического предмета | ✓✓ 0.6.76 (repplus.lua): REP+ Room::SpawnClearAward 0x7fd79a → 0x80e6c0, таблица «босс → предмет» из exe (67 боссов, черви дают один из 11 брелков-червей, всадники свою саранчу); предмет встаёт вторым пьедесталом «на выбор» с предметом босса. На консоли с принудительным броском: Gurglings дали Lil Gurdy, после взятия второй пьедестал исчез |
| Счётчик волн в Boss Rush | ✓✓ 0.6.28. HUD REP+ 0x9a46bf..0x9a48a8: `gfx/ui/ui_bosshealthbar_rush.png` по центру сверху и «волна/всего» шрифтом. На Switch Ambush = Game+0x26faa8. Сдвиг на 20 точек ниже не сделан: REP+ сдвигает, пока HUD+0x2d0 > 0 (ПК Game+0x2619c, рядом с надписью подобранного предмета; на Switch надпись = спрайт HUD+0xc4fc0, HUD::ShowItemText 0x3ab3b8) и пока ПК Game+0x26630 > 0 (похоже на онлайн); проверять на консоли |
| Трансформация Necromancer | ✗ в exe REP+ нет: в его resources/scripts/enums.lua PlayerForm тот же (NUM_PLAYER_FORMS = 15, как в Repentance), строки Necromancer в exe нет |
| Jacob и Esau: опция управления Classic/Better | ✓✓ 0.6.43 логика ввода (repplus_hooks.inc: хуки control_active_item / control_pocket_item, ответы на IsAction* 9/10/11; по умолчанию Better, как в REP+). На консоли проверены все 4 сочетания в обоих режимах: Better ZL активка Jacob, R карта Jacob, ZR+ZL активка Esau, ZR+R карта Esau; Classic как 1.7.9b. Пункта в меню настроек и подсказок HUD пока нет (переключение только из Lua: Options.JacobEsauControls). Из exe: В exe REP+ новая опция JacobEsauControls (options.ini, по умолчанию 1, Options+0x98 = Manager+0x2a3d4, Lua Options.JacobEsauControls 0x897650). Её читают: использование активного предмета (0x791e2e) и карманного (0x7a411a, кнопка 9 или 10 и удержание 11 у Esau), флаг ввода по сети (0x9012e6), подсказка кнопок HUD (0x93513e, 0x9351f6), положение активных предметов J&E в HUD (0x9a765d) и пункт меню настроек (0x9b474e, анимация JacobEsauControl). На Switch нужны и логика ввода в нескольких местах, и новый пункт настроек |
| Описания предметов в игре (после победы над Mom) | ✓✓ 0.6.81 (на консоли, repplus.lua): с 0.6.91 окно не показывается, пока загружен и не скрыт EID (по просьбе Егора: EID показывает то же и знает скрытые предметы; на консоли с EID окна нет, с EID.isHidden есть, под Curse of the Blind нет); с 0.6.92 окно не показывает и скрытые предметы без проклятия (второй предмет сокровищницы альтернативного этажа и др.: Entity_Pickup::SetForceBlind, байт +0x562 на Switch; на консоли проверено); найдено в exe REP+ (объект HUD+0x5c80: обновление 0x83cc80, раскладка 0x83ee90, место 0x83d769). После достижения 641 «Item Info» (даётся при убийстве Mom вместе с 4-6, 0x7fc54f; на Switch проверяется достижение 4) и при включённой опции (Manager+0x2a3f8, переключается действием 31 ToggleDescriptions; на Switch кнопки нет, опция всегда включена) в зачищенной комнате у ближайшего к игроку пикапа в радиусе 100 (предмет, брелок, пилюля, карта) показывается окно: значок, имя, строки info_display REP+, у активных предметов значок заряда ui_charge_icons (0x8403e0), у Spindown Dice строка «Spins down into». Не подобранный ни разу предмет: «???» и «Collect this item once to identify it», неопознанная пилюля: «Unidentified Pill». Окно чёрное 60%, текст 0.625, левый край x 65 если игрок справа, иначе правый край в 90 от правого края, верх y 60. Не сделано: пропуск пьедесталов с «?» вне Curse of the Blind (Lua 1.7.9b не видит лист слоя). Раньше: страница My Stuff (0.6.0) |
| Новые звуки предметов и врагов | ◐ В exe REP+ около 120 мест, где играются новые звуки 833-1090 (поиск по SFXManager::Play). ✓✓ 0.6.62 (repplus.lua, файлы из REP+ sfx в content/sounds.xml): Forget Me Now (1054, кадр после использования, 0x5b73b4), Bumbo (842 при появлении, 843-845 на 6/12/24 монетах, 0x62fcc9), Sprinkler (1070 при появлении, 0x60f1e6); на консоли все шесть играют. ✓✓ 0.6.63: Little Chubby (849) и Big Chubby (835) при броске (0x618b93, 0x618bdf; ловится рывок скорости из покоя). ✓✓ 0.6.64: Lil Haunt (859, цель появилась, 0x61bbc7) и Dark Bum (853, выплата, 0x61b3e4; ловится появление пикапа или паучка с Dark Bum в SpawnerEntity). ✓✓ 0.6.65: Cube Baby (852, пинок: скорость с покоя до 8+, 0x96a743) и Glitter Bombs (867, фитиль бомбы с TEAR_GLITTER_BOMB, 0x781bb9). ✓✓ 0.6.66: Infamy (863, блок снаряда, 0x65be77; на консоли 11 блоков из 20 в Hard, 11 звуков). Заодно найдено неучтённое изменение: в Greed/Greedier REP+ блокирует при Random(4) == 0 (25%) вместо Random(2) (50%), сделано нативно (Switch Random возвращается в 0x32818c), в Greed на консоли не проверено. ✓✓ 0.6.67: бросок какашки (865, сущность 245 получает FLAG_THROWN, 0x7d296d) и Suplex! (1031 захват при состоянии 2, 0x78f066; 1032 прыжок при состоянии 3, 0x7d48e4; состояние Player+0x2678 на Switch); на консоли все три играют. ✓✓ 0.6.68: Ocular Rift (1087 слеза с TEAR_RIFT, 0x7c218f; 1088 появление разлома 1000.180, 0x649af4 и др.); на консоли слеза-разлом и разлом дали по одному звуку. ✓✓ 0.6.69: Butter! (901 при выбрасывании брелка 122, DropTrinket 0x771e85) и Cracked Orb (850, удар открыл комнату на карте, 0x77651f -> 0x7d80e2); на консоли оба играют, другой брелок звука не даёт. ✓✓ 0.6.70: Esau Jr. (1048 при смене персонажа, 0x7ad349); на консоли играет. ✓✓ 0.6.71: звуки боссов. В exe REP+ 44 места новых звуков боссов стоят сразу после Sprite::Play/SetAnimation или IsEventTriggered с именем анимации или события; repplus.lua (BOSS_SND) играет звук, когда босс начинает эту анимацию или срабатывает событие. На консоли играют у Cage (Puking, RollStart, RollLoop), Mega Fatty (все три), Big Horn (HandSlap, Charge, SmallHoleClose), Death (Attack02), Gish (Walk, JumpUp), Clog (Shoot), Imp (Vanish). У Loki, Little Horn, Fallen, The Frail, лиц Gurdy, Sisters Vis, Forsaken, Stain, событий Big Horn и Death Vox нужных анимаций или событий в anm2 1.7.9b нет (они из anm2 REP+, архив animations.a в режиме сжатия 1 мой распаковщик пока не читает), эти правила сработали на консоли в 0.6.72 с anm2 REP+ из afterbirthp.a (Gurdy за минуту нужных атак не показал). ✓✓ 0.6.86: Immaculate Conception (1010, новый бонус к характеристикам на 15-м сердце, 0x7c84ec; Switch Player+0x1db0) и лестница Member Card в тайный магазин (916, появление 1000.156 в Room::TrySpawnSecretShop 0x7f8644; повторное появление при возвращении в комнату без звука, как в REP+); на консоли оба играют. Заодно убран повтор звука Esau Jr (обработчик был подключён дважды). ✓✓ 0.6.87: прыжок в люк (1052, перед PlayExtraAnimation("Trapdoor") в GridEntity_TrapDoor 0x720c53 / 0x720e7a и 0x6e68c3, один раз на всех игроков); на консоли играет. ✓✓ 0.6.88: смерть Little Horn (944, 0x49210f) и The Haunt (996, 0x482b82), по MC_POST_ENTITY_KILL самого босса; на консоли оба играют. ✓✓ 0.6.89: Big Horn: смерть (878, 0x454bb4) и встряска после оглушения (879, начало анимации DizzyEnd, 0x454b51); на консоли оба играют. ✓✓ 0.6.90: коса Death (926, анимация Throw у кос 66.10, 0x45b00e / 0x45b039); на консоли звук звучит, когда Death бросает косы (Attack04). ✓✓ 0.6.93: смерть Steven (961) и Little Steven (962), 0x6c2d9e / 0x6c2e57; на консоли оба играют. Остальные (звуки боссов без строк рядом, Tick, лестница в подвал и др.) ○ |

## 11. Интерфейс

Найдено по именам файлов, которые есть в exe REP+ и которых нет в Switch-версии. Всё ○, кроме отмеченного.

- ◐ Пауза: вкладка «My Stuff» со списком собранных предметов (`gfx/ui/pausescreen_mystuff.anm2`, `gfx/ui/mystuff_sectioncursor.png`): сделана в 0.6.0, на консоли не проверена.
- ✓✓ Компактная пауза (`gfx/ui/pausescreen_small.anm2`): сделана в 0.6.5, проверена на консоли. В REP+ она единственная (PauseScreen::Open ставит стиль 1).
- Меню Deeds, отметки прохождения (`gfx/ui/deedsmenu.anm2`, `gfx/ui/deedsdeathscreen.anm2`).
- HUD, разобрано по exe REP+ (2026-11-11):
  - ✓✓ иконки святых щитов по источникам (`ui_hearts.anm2` HolyMantleWoodenCross / Blanket / HolyCard), 0.6.78.
  - ✓✓ значок заряженного Necronomicon (`hud_necronomiconpowered.png`), сделан раньше вместе с Necromancer.
  - ✓✓ счётчик волн Boss Rush (`ui_bosshealthbar_rush.png`, HUD::Render 0x9a46xx), сделан раньше.
  - ✓✓ Modeling Clay / Error в Extra HUD (HistoryHUD 0x83b74a), 0.6.79.
  - ✗ `ui_isaachealthbar.png`: рисуется только в комнате -19 (арена онлайн-дуэли, PlayerHUD 0x84422f), это онлайн.
  - ✓✓ `ui_charge_icons.anm2` (0x83caa5, объект HUD+0x5c80 = окно описания предмета в комнате, см. раздел предметов): 0.6.81. `ui_chargebar_small.anm2` (HUD::LoadGraphics 0x9a23b0, PlayerHUD 0x84580b): малая полоска заряда у игроков кооператива, в одиночной игре не видна.
  - ○ косметика: перерисованные сердца (`ui_hearts.png`, `ui_hearts_small.png`), значки в тексте (`text_icons.anm2`), подсказки кнопок (`buttonpromptwidget.anm2`).
- Экран босса: новые `versusscreen.anm2`, `versusscreen_dogma.anm2`, `versusscreen_mother.anm2`.
- Главное меню: новые картинки (`titlemenu.png`, `titlemenu_2.png`, `titlemenu_3.png`), экран загрузки (`loading.anm2`, `loadingspinner.anm2`), курсор (`cursor.anm2`), окна (`popup_beige.png`, `popup_white.png`). У Switch свои варианты для экрана 426×240 (`*_427.anm2`), картинки придётся подгонять.
- Кооп: меню выбывания и возрождения игрока (`menu_dropplayer.anm2`, `menu_reviveplayer.anm2`, `reviveplayer_selector.anm2`, `dropplayer_counter.anm2`).
- Онлайн-меню (`onlinemenu`, лобби, друзья, эмоции, платформы): ✗.

## 12. Lua API для модов

Найдено по exe REP+ и его `resources/scripts` (main.lua, enums.lua). Сделано в 0.5.0 (✓✓: на консоли 0.99.113 вызваны REPENTANCE_PLUS, FontRenderSettings, Entity:KillWithSource, Room:DestroyGridWithSource и ItemPool:GetCollectible с backupPool, все работают), кроме пунктов с ○. Когда мод REP+ включён, загрузчик выставляет `REPENTANCE_PLUS = true`: 12 скачанных модов каталога (из них 4 из топ-30: MinimapAPI, Planetarium Chance, Music Mod Callback, Extra Animated Portraits) идут тогда по ветке для REP+.

- Глобальная `REPENTANCE_PLUS = true`.
- Класс `FontRenderSettings`: EnableAutoWrap, EnableTruncation, Get/SetAlignment, Get/SetLineHeightModifier, Get/SetMaxCharacters, Get/SetMissingCharacterOverride, IsAutoWrapEnabled, IsTruncationEnabled.
- Enum `DrawStringAlignment` (TOP_LEFT … BOTTOM_RIGHT).
- `Font:DrawString(str, x, y, sx, sy, color, settings)`: новый вызов (выравнивание и MaxCharacters учитываются; с 0.6.76 автоперенос и обрезка с «...» по exe REP+ 0xa1bdf0: `EnableAutoWrap(ширина)` и `EnableTruncation(ширина)` принимают ширину в пикселях, ✓ на ПК-тесте); старый вызов с KColor четвёртым аргументом работает как раньше. DrawStringScaled/UTF8 стали обёртками.
- `Entity:KillWithSource`, `GridEntity:DestroyWithSource`, `GridEntity:HurtWithSource`, `Room:DamageGridWithSource`, `Room:DestroyGridWithSource`.
- `ItemPool:GetCollectible(..., backupPool)`: новый необязательный аргумент, пока не учитывается (○).
- `GridEntity:Hurt` не было и в 1.7.9b-части загрузчика, добавлен вместе с `HurtWithSource`.


○ Прочее из старого списка (сверить с exe; Options.JacobEsauControls и Room:MamaMegaExplosion с позицией уже есть): параметр IgnoreBosses у Entity:Add{Burn,Charmed,Fear,Freeze,MidasFreeze,Poison,Shrink,Slowing}, ItemPoolType у EntityPlayer:AddCollectible, ShowIcon и KColor у Game:Fadein/Fadeout, StackUpText у HUD:ShowItemText, Room:MamaMegaExplosion с необязательной позицией, BackdropType 61/62, новые значения ButtonAction.

## 13. Онлайн (не переносится)

- Онлайн: Quick/Public/Friend Match, онлайн-Daily, чат, колесо эмоций, Revive Machine, режим Deathmatch (BackdropType 61), 9 кооп-малышей онлайна (можно отдельно как косметику).
- Тейнтед-персонажи во вторничных дейли (дейли идут с сервера).
