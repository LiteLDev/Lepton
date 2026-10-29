# U21 mobsdeep: Stonelings, Forgotten, Foxhound and Wraith

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `mobs/StonelingsModule`,
`mobs/ForgottenModule`, `mobs/FoxhoundModule`, `mobs/WraithModule` and their entity, item and AI
classes. Nothing is copied from Quark: geometry, animations, textures, particles and sound choices
are Lepton's own (textures drawn by `tools/textures/mobsdeep.ps1`).

Every mob is a behaviour-pack entity (`assets/pack/bp/entities/`) with a client entity, geometry,
animations and render controllers in the resource pack. Vanilla components do what Bedrock can
express (movement, goals, taming, breeding, loot, spawn rules); the native module
(`src/feature/mobsdeep/`) adds everything else (interactions, wariness, poofing, weapon swapping,
ignition, furnace boosting, soul bead guidance, config-driven spawn filtering).

## 1. Modules covered

### Stonelings (`enableStonelings`)
Small stone critters (0.5 x 0.9) that spawn deep underground on stone (y <= `maxYLevel`, dark,
overworld). They come in 11 stone variants and usually carry an item. A wild stoneling is wary: a
player who is not sneaking within 6 blocks startles it, and a startled stoneling runs away from
players and vanishes in a puff of smoke. Killing one drops its item and a **Heart of Diamond**. Using
the heart on a stone block of a variant turns the block into a new, player-made stoneling, which
holds items for you, eats stone blocks to change its variant and follows players holding its food.

### Forgotten (`enableForgotten`)
A rare skeleton variant (0.7 x 2.4, 60 HP) that replaces 5 % of skeletons spawning below y 0. It
carries a bow and a sheathed iron sword: it shoots blinding arrows and draws the sword while its
target is blind, switching back when the blindness is about to end. It always drops the
**Forgotten Hat** when killed by a player.

### Foxhound (`enableFoxhound`)
A hostile fire wolf of the Nether (0.8 x 0.8, fire immune) that sets what it bites on fire. It can
be tamed with coal while the player is immune to fire. A tame foxhound behaves like a tame wolf,
naps on furnaces and other warm blocks, and makes the furnace it sleeps on cook faster. Foxhounds
born in Soul Sand Valleys are blue ("soulhounds"); 1 in 256 are black ("basalt foxhounds").

### Wraith (`enableWraith`)
A neutral ghost of Soul Sand Valleys (0.6 x 1.95, fire immune) that speaks with the lowered voice of a
random mob. Once a player hits it, it attacks like a zombie, slows its victim and leaps back after
each hit. It drops a **Soul Bead**; using a soul bead releases a soul that flies towards the
nearest Nether Fortress.

## 2. Blocks and items

No blocks. Items (BP `items/`, names in RP `texts.d/*/mobsdeep.lang`):

| Id | English name | Notes |
|---|---|---|
| `lepton:diamond_heart` | Heart of Diamond | Uncommon (aqua name), always glints, stack 64. Use on a variant block -> stoneling. Creative: items (next to the ender pearl, Quark: tools tab). |
| `lepton:forgotten_hat` | Forgotten Hat | Uncommon, stack 1, head armour slot, no armour points, unbreakable, not enchantable. Worn model (attachable) on players. |
| `lepton:soul_bead` | Soul Bead | Stack 64. Use -> soul flies to the nearest fortress. |
| spawn eggs | Stoneling / Forgotten / Foxhound / Wraith Spawn Egg | Bedrock's generated eggs; Quark's colours (stoneling `#a1a1a1`/`#505050`, forgotten `#969487`/`#3a3330`, foxhound `#890d0d`/`#f2af4b`, wraith `#ececec`/`#bdbdbd`). |

No recipes (Quark has none). Entities: `lepton:stoneling`, `lepton:forgotten`, `lepton:foxhound`,
`lepton:wraith`, `lepton:soul_bead` (names "Stoneling", "Forgotten", "Foxhound", "Wraith", "Soul Bead").

Loot tables (`bp/loot_tables/entities/`):

| Table | Content (Quark's numbers) |
|---|---|
| `stoneling.json` | 1 Heart of Diamond (used while `enableDiamondHeart`; else `empty.json`). The carried item is dropped natively. |
| `stoneling_carry.json` | Spawn item (equipment table, 1 roll): diamond 100, ender pearl 200, redstone 10-15 x300, gold ingot 2-6 x300, iron ingot 4-7 x300, emerald 1-3 x200, coal 18-26 x300, zombie head 250, creeper head 250, lapis 4-8 x200, golden apple 50, saddle 50, iron/golden/diamond horse armour 50/40/30, name tag 100. |
| `forgotten.json` | 4-8 arrows (+0-2 per looting level), 2-3 bones (+0-1 per looting level), Forgotten Hat (killed by player). |
| `forgotten_gear.json` | A plain bow (the native spawn code replaces the gear with an enchanted bow and sword, Quark's `prepareEquipment`). |
| `foxhound.json` | 0-3 leather (+0-1 looting), 1-2 coal (+0-2 looting). |
| `wraith.json` | 1 Soul Bead (+0-1 looting). |

Block lists used by the mechanics (Bedrock ids; other units' blocks by id, ignored when absent):

| Variant (index) | Blocks that make / feed it |
|---|---|
| stone (0) | `minecraft:cobblestone`, `minecraft:stone` |
| andesite (1) | `minecraft:andesite`, `minecraft:polished_andesite` |
| diorite (2) | `minecraft:diorite`, `minecraft:polished_diorite` |
| granite (3) | `minecraft:granite`, `minecraft:polished_granite` |
| limestone (4) | `lepton:limestone`, `lepton:polished_limestone` (U3) |
| calcite (5) | `minecraft:calcite` |
| shale (6) | `lepton:shale`, `lepton:polished_shale` (U3) |
| jasper (7) | `lepton:jasper`, `lepton:polished_jasper` (U3) |
| deepslate (8) | `minecraft:deepslate`, `minecraft:polished_deepslate` |
| tuff (9) | `minecraft:tuff`, `minecraft:polished_tuff` |
| dripstone (10) | `minecraft:dripstone_block`, `lepton:polished_dripstone` (U2/U3) |

Foxhound spawnable blocks (Quark tag `foxhound_spawnable`): netherrack, soul sand, basalt, soul soil.
Wraith spawnable blocks (`wraith_spawnable`): soul sand, soul soil. Soul bead target (`soul_bead_target`):
the Nether Fortress.

## 3. Mechanics

### Stoneling
- Attributes: 8 HP, knockback resistance 1, step height 1, movement 0.7 (Java's default attribute)
  with Quark's speed modifiers. No fall damage, no cactus damage, never drowns (`minecraft:breathable`),
  not pushed by water.
- **Immune to projectiles without piercing**: arrow / trident / other projectile damage is cancelled
  natively unless the projectile has piercing (Bedrock arrows from Piercing crossbows).
- Goals (Quark priority -> Bedrock): 0 tempt 0.6 (when `tamableStonelings`), 1 act wary (wild),
  2 run and poof (startled), 3 tempt 0.6 (when `enableDiamondHeart` and not `tamableStonelings`),
  4 favour diamond ore (walks to diamond ore within its follow range, rechecked every 200-400 ticks,
  stays 1200-3600 ticks), 5 random stroll 0.2 avoiding water. Tempt food: Glow Shroom
  (`lepton:glow_shroom`, U28's Glimmering Weald) when that item exists, otherwise diamonds.
  Tempting stops forever once the stoneling is startled.
- **Variant**: random 0..10 on spawn, kept in the entity property `lepton:variant`. Lichen layer
  (`lepton:lichen`) for 3 in 5 stonelings spawned in a Glimmering Weald biome. The held item is
  tilted by a random angle in [-45, 45).
- **Carried item**: wild spawns roll `stoneling_carry`; the item is held (Bedrock mainhand) and shown on
  the stoneling's back; it is dropped on death (never despawns with it: a poofing or despawning
  stoneling deletes its non-player passengers only).
- **Wary** (`ActWaryGoal`, wild only, range 6): every tick, for every non-creative player within 6
  blocks: not sneaking -> startled; sneaking and `cautiousStonelings` -> startled when the player's
  acceleration (change of per-tick displacement) squared is >= 0.01. While wary players are near it
  creeps around at speed 0.1.
- **Startled** (`RunAndPoofGoal`, range 4 horizontally, 3 vertically): a startled wild stoneling
  that sees a non-creative, non-spectator player within range plays "meep", flees (16 / 7 blocks away,
  speed 0.5) and, when it stops (path finished or blocked; at most 100 ticks), poofs: 40 cloud +
  20 explosion particles and it disappears with its non-player passengers. No drops.
- Hurting a wild stoneling as a player startles it (Quark also loops over stonelings within 16
  blocks but, by a bug, startles only the hurt one; Lepton keeps that behaviour).
- **Interact** (main hand):
  - player-made, not sneaking, holding a variant block: heart particle; if the variant differs:
    16 block particles, "eat" sound, variant changes; "purr" sound (pitch 1-2); heals 1; uses one
    item (not in creative). Other non-empty items: nothing.
  - player-made, sneaking or empty-handed: swaps the hand item with the carried item ("give" sound
    when the hand was empty, else "take").
  - wild, `tamableStonelings`, holding the tempt food: heals 8, becomes player-made (no despawn,
    creature category), "purr", 4 hearts, uses one item.
  - name tags work as on any mob.
- **Heart of Diamond** on a variant block (block not unbreakable, player may build there): the block
  becomes air with break particles, a player-made stoneling of that variant appears at the block
  (facing away from the player, yaw + 180) without a carried item, the heart is used (not in creative).
  Logged as `mobsdeep: stoneling made (<variant>)`.
- Drops: carried item + Heart of Diamond (`enableDiamondHeart`). In a Glimmering Weald, a wild
  stoneling killed by a player also drops U23's Pathfinder's Quill (`lepton:pathfinders_quill`) with
  chance 0.08 (looting: 0.1 + 0.02 per level above 1), when `wealdPathfinderMaps` is on and the item exists.
- Ambient sound: none, except a stoneling named "Michael Stevens" / "Vsauce" (any case) makes the
  "michael" sound every 1200 ticks on average. Hurt "cry", death "die".
- Despawn: wild ones like monsters (far away), player-made ones never.
- Spawning (`stonelingsSpawnConfig`, weight 80, group 1): monster population, dark (light <= 7),
  on stone-based blocks, overworld biomes that are not Nether/End/void; natively vetoed when
  y > `maxYLevel`, the dimension fails `dimensions`, or the biome fails the config's biome filter.

### Forgotten
- Attributes: 60 HP, speed 0.3, knockback resistance 1, scaled x1.2 (0.7 x 2.4). Skeleton goals:
  bow attack (speed 1.0, every 20-40 ticks, 15 blocks), melee with the sword (1.2), flee wolves,
  target players / iron golems. Does not burn in daylight (it wears its hat), does not pick up loot.
- Spawn equipment: enchanted bow in the main hand, enchanted iron sword sheathed (Bedrock: off hand,
  drawn on its back), Forgotten Hat drawn on its head. Equipment never drops.
- Every tick: `shouldUseBow = no target || the target's blindness has < 20 ticks left`; when that
  differs from "holding a bow", the bow and the sheathed sword swap and the attack goal switches
  (Bedrock: component groups `lepton:ranged` / `lepton:melee`).
- Its arrows blind the hit entity for 100 ticks (amplifier 0).
- Replacement: a skeleton spawning naturally (not from a spawner) below `maxHeightForSpawn` is,
  with chance `forgottenSpawnRate`, replaced by a Forgotten at the same place (logged as
  `mobsdeep: skeleton replaced by forgotten`).
- White spell particles around it. Drops `forgotten.json`.
- **Forgotten Hat**: head slot, unbreakable; Quark adds +1 luck and +2 block / entity interaction
  range (see §7).

### Foxhound
- Wolf attributes: 8 HP wild, 30 HP tame, speed 0.3, attack 4. Fire immune, does not get wet, avoids
  water, walks on / paths over lava cheaply, fire and danger-fire path costs 4.
- Wild: hostile to players (not on Peaceful) and to sheep and rabbits, always angry (hostile texture),
  no bone / meat interactions. Iron golems attack wild ones, never tame ones.
- Bite: sets the target on fire for 5 s unless it is fire immune, has Fire Resistance, or blocks it
  with a shield; never hurts players on Peaceful.
- **Taming**: coal on a wild foxhound while the player is on Peaceful, invulnerable (creative) or has
  Fire Resistance: uses one coal (not in creative); with chance `tameChance` it is tamed (owner set,
  sits, 30 HP, hearts) else smoke. Without fire immunity coal does nothing.
- Tame: wolf behaviour (sit on interact and stay while sitting, follow owner (10/2 blocks), owner-hurt targets, heal and
  breed with meat; babies of a tame parent are tame, of a blue parent blue).
- **Resting**: a tame, not sitting foxhound looks for (in priority) a lit furnace, an unlit
  furnace, then any light-emitting (> 2) block without fluid, within 8 blocks with air above; on
  reaching it, it lies down (sleeping texture, smoke particles instead of flames). Hurting it or a
  successful interaction wakes it; it can't rest again for 100 ticks.
- **Furnaces** (`foxhoundsSpeedUpFurnaces`): a tame foxhound standing on a furnace / blast furnace /
  smoker that is cooking: every tick where the cooking progress is a positive multiple of 3, the
  progress becomes 5 (if it was 3) or progress + 1 (capped at total - 1): 3 progress every 2 ticks. Only one
  foxhound per furnace counts.
- **Tiny potato** (U23 `lepton:tiny_potato`) fed to a tame foxhound: "eat", 600 ticks later it pops
  out (dropped item) with the potato hurt sound; meanwhile smoke and crackle sounds.
- Flames (blue soul flames for blue ones, smoke while resting) around it.
- Sounds: ambient growl when angry, else 1/3 pant (whine when tame and < 10 HP), else idle; none while
  resting. Hurt, death.
- Spawning: `foxhoundSpawnConfig` (weight 30, 1-2, Nether Wastes and Basalt Deltas) and
  `lesserSpawnConfig` (weight 2, 1, Soul Sand Valley); not on Peaceful; on foxhound-spawnable
  blocks. Blue when spawned in a Soul Sand Valley, otherwise red; 1 in 256 (either biome) black.

### Wraith
- Zombie-based: 20 HP, follow range 35, speed 0.28, attack 3, knockback resistance 1, armour 0, no
  reinforcements, never a baby, no equipment, fire immune, no fall damage.
- Neutral until a player hits it directly (melee; arrows don't count); then it targets like a
  zombie (players, villagers, iron golems).
- On a successful hit: Slowness II (amplifier 1) for 60 ticks on the target, and the wraith jumps
  back: velocity `normalize(normalize(dx, 0, dz) + (0, 0.5, 0)) * 0.85` away from the target.
- Voice: on spawn it picks one entry of `wraithSounds` (idle|hurt|death; empty = silent), kept with
  the entity as its index in the list (entity property `lepton:voice`, -1 = not chosen yet; only the
  first 64 entries are used; an index past the end of an edited list is silent); pitch 0.75-0.85. Java ids are mapped to Bedrock sounds (table in
  `src/feature/mobsdeep/Common.cpp`); unknown ids are used as Bedrock sound names.
- Prefers walking on soul sand / soul soil. Mycelium-like and soul particles.
- Spawning: `wraithSpawnConfig` (weight 5, 1-3, Soul Sand Valley), monster light rules, on soul sand
  / soul soil.

### Soul Bead
- Use (server): finds the nearest Nether Fortress within 100 chunks (1600 blocks) of the player
  in the current dimension. If found: uses one bead (not in creative), plays the soul-spawn sound and
  spawns a `lepton:soul_bead` at `player + look * 2` (horizontal), y + 0.25. Nothing happens otherwise.
- The soul lives 6000 ticks. At life tick t its trail point is
  `start + dir * 0.08 * t + (cos(0.32 t) * 1.5, 0.025 t, sin(0.32 t) * 1.5)` where `dir` points
  horizontally at the fortress; 20 dust particles (colour 0.2, 0.12, 0.1) per tick around the point
  (spread 0.4) and 5 % soul-sand falling dust; 10 % per tick a ghast moan (volume 0.2). Target and
  age persist with the entity. Logged as `mobsdeep: soul bead -> <x> <z>`.

## 4. Screens

None. Quark has no screens for these modules; every interaction is in the world and works for
vanilla clients (the resource pack carries the models). Nothing needs the client mod.

## 5. Config

Section `mobsdeep` (`config/mobsdeep.json`), version 1:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enableStonelings` | bool | true | Stonelings module |
| `maxYLevel` | int | 0 | Highest spawn y |
| `dimensions` | object | `{ "isBlacklist": false, "dimensions": ["minecraft:overworld"] }` | Dimensions where stonelings spawn |
| `stonelingsSpawnConfig` | object | weight 80, group 1-1, biomes: tag blacklist `void`, `nether`, `the_end` | Spawn config |
| `enableDiamondHeart` | bool | true | Stonelings drop the Heart of Diamond (and the item works) |
| `cautiousStonelings` | bool | false | When enabled, stonelings are much more aggressive in checking for players |
| `tamableStonelings` | bool | true | Wild stonelings can be tamed with their food |
| `wealdPathfinderMaps` | bool | true | Disabled if if Pathfinder Maps are disabled. |
| `enableForgotten` | bool | true | Forgotten module |
| `forgottenSpawnRate` | double | 0.05 | This is the probability of a Skeleton that spawns under the height threshold being replaced with a Forgotten. |
| `maxHeightForSpawn` | int | 0 | Replacement height threshold (exclusive) |
| `enableFoxhound` | bool | true | Foxhound module |
| `tameChance` | double | 0.05 | The chance coal will tame a foxhound |
| `foxhoundsSpeedUpFurnaces` | bool | true | Tame foxhounds speed up furnaces they rest on |
| `foxhoundSpawnConfig` | object | weight 30, group 1-2, biomes whitelist `minecraft:nether_wastes`, `minecraft:basalt_deltas` | Spawn config |
| `lesserSpawnConfig` | object | weight 2, group 1-1, spawnCost 0.7, maxCost 0.15, biomes whitelist `minecraft:soul_sand_valley` | Spawn config |
| `enableWraith` | bool | true | Wraith module |
| `wraithSounds` | string[] | Quark's 20 entries (sheep ... frog) | Sound sets `idle|hurt|death` (missing from SPEC §5.1: a private `@Config` field in Quark) |
| `wraithSpawnConfig` | object | weight 5, group 1-3, spawnCost 0.7, maxCost 0.15, biomes whitelist `minecraft:soul_sand_valley` | Spawn config |

Spawn config shape (Quark's `EntitySpawnConfig` / `CostSensitiveEntitySpawnConfig` + `CompoundBiomeConfig`):

```json
{ "spawnWeight": 80, "minGroupSize": 1, "maxGroupSize": 1, "spawnCost": 0.0, "maxCost": 0.0,
  "biomes": { "isBlacklist": true, "biomes": [] },
  "tags":   { "isBlacklist": true, "biomeTags": ["void", "nether", "the_end"] } }
```

`biomes` holds biome ids (`minecraft:soul_sand_valley`), `tags` Bedrock biome tags. A spawn needs both
filters to pass. A disabled module's mob never spawns (eggs and `/summon` included); the filters and
weights apply to natural spawns only. The pack's spawn rules use Quark's default weights and biomes; the config narrows
them natively (veto of the attempt; a lower weight vetoes with probability `1 - weight / default`).

## 6. Strings

Item / entity names in `texts.d` (§2): "Heart of Diamond", "Forgotten Hat", "Soul Bead", "Stoneling",
"Forgotten", "Foxhound", "Wraith", "Soul Bead" and "<Mob> Spawn Egg" (Quark's wording, zh_CN from Quark's
`zh_cn.json`).

Mod strings (`assets/lang/*/mobsdeep.json`) keep Quark's hint texts under Quark's keys. Quark shows
them only in JEI's information panel, which Bedrock does not have, so Lepton does not display
them in game either; they are kept for other units (e.g. a future recipe/hint viewer).

| Key | en_US |
|---|---|
| `quark.jei.hint.diamond_heart` | Can be gotten by breaking a Stoneling's shell underground. Don't worry - the Stoneling is just fine. ... (Quark's full text) |
| `quark.jei.hint.forgotten_hat` | Drops from Forgotten, which can be found deep underground. Be wary of their powerful equipment and multiple combat styles. |
| `quark.jei.hint.soul_bead` | Drops from Wraiths in Soul Sand Valleys. Will fly towards the nearest Nether Fortress when used. |

Sound events (`sounds.d/mobsdeep.json`): `lepton.stoneling.{meep,purr,give,take,eat,cry,die,michael}`,
`lepton.foxhound.{eat,crackle}`, `lepton.soul_bead.{idle,spawn}`; the foxhound otherwise uses the
vanilla wolf sounds and the wraith the vanilla sounds of its voice.

## 7. Bedrock differences / not ported

- **Sounds**: Quark's own sound files (stoneling meep / squeak / hurt / die, the Vsauce clips, the
  soul powder scream) are Quark assets and are not copied. Lepton defines the same sound events
  (`sounds.d/mobsdeep.json`) with vanilla Bedrock sound files of a similar character. Bedrock has no
  subtitles for them.
- **Forgotten Hat interaction range**: Quark adds +2 block and +2 entity interaction range. Bedrock
  has no reach attribute (checked `SharedAttributes` in the 26.51.7 headers: absorption, air drag,
  attack damage, bounciness, follow range, friction, health, jump strength, knockback resistance,
  lava / underwater / normal movement, luck); reach is fixed by the client and the server's
  reach checks. The +1 luck modifier is applied (`SharedAttributes::LUCK`, modifier
  `lepton:forgotten_hat_luck`, checked every 10 ticks).
- **Colour runes** on Forgotten weapons (when ColorRunes is on): rune data is U13's item component;
  Lepton gives no rune until U13 exposes a contract.
- **Spawn costs** (`spawnCost`, `maxCost` of cost-sensitive configs) are Java's soul-sand-valley
  "spawn potential" system, which Bedrock does not have; the keys are kept for compatibility but not
  used. Group sizes and weights above the pack's defaults can't be raised at runtime.
- **Startle chain**: as in Quark only the hurt stoneling is startled.
- **Foxhound resting targets** are one `minecraft:behavior.move_to_block` (Bedrock allows one per
  entity): lit furnaces, furnaces and light-emitting blocks share one list and the nearest wins, where
  Quark tries the three groups in priority order. The light-emitting group is a fixed list (Bedrock
  can't filter by light level): magma, glowstone, sea lantern, shroomlight, lit redstone lamp,
  froglights, jack o'lantern, crying obsidian, beacon, campfires.
- **Natural despawn** of a stoneling does not delete its non-player passengers (poofing does).
- **Owner ignition** when a foxhound "needs pets" (PatTheDogs' `dogsWantLove`, U19) is not wired:
  U19 owns that state.
- **Tiny potato "angry" flag** on the potato a foxhound returns belongs to U23's tiny potato; the
  returned item is a plain tiny potato.
- **Sheathed sword** is the Bedrock off-hand slot (rendered on the back), not hidden synced data.
- Interaction-at position is ignored (Quark ignores it too); hand is always the main hand.
- Stoneling held-item angle is chosen per client (cosmetic only).

## 8. Tests

Selftests (`src/feature/mobsdeep/Tests.cpp`, dx 2100..2199):

| Name | dx | Checks |
|---|---|---|
| `mobsdeep.config` | - | defaults, variant table, Java->Bedrock wraith sound mapping, skeleton replacement decision, soul bead trail |
| `mobsdeep.stoneling.heart` | 2100 | heart on andesite: block removed, player-made andesite stoneling, heart used |
| `mobsdeep.stoneling.items` | 2110 | give / take swap, feeding a variant block changes the variant |
| `mobsdeep.stoneling.tame` | 2120 | tempt food tames a wild stoneling |
| `mobsdeep.stoneling.startle` | 2130 | a non-sneaking player startles a wild stoneling, which poofs |
| `mobsdeep.stoneling.drops` | 2140 | killing drops the carried item and the heart |
| `mobsdeep.forgotten.swap` | 2150 | bow without target, sword when the target is blind |
| `mobsdeep.foxhound.tame` | 2160 | coal without fire resistance does nothing; with it (chance forced to 1) tames |
| `mobsdeep.foxhound.furnace` | 2170 | a sitting tame foxhound stays on a cooking furnace and speeds it up (> 4/3 progress per tick) |
| `mobsdeep.foxhound.ignite` | 2175 | a wild foxhound's bite ignites |
| `mobsdeep.wraith.aggro` | 2180 | neutral until hit, slowness + leap after its hit, sound set stored |
| `mobsdeep.soulbead` | 2190 | soul bead entity follows the trail formula and expires |
| `mobsdeep.gallery` | 2195 | builds the screenshot row (all variants, resting foxhound, hat); with a real player online shows it for the client scenario |

E2E: `tools/e2e/server/scenarios/mobsdeep.ps1` (runs `mobsdeep.*`), `tools/e2e/client/scenarios/mobsdeep.ps1`
(summons each mob and variant next to the player and takes screenshots: stonelings of several
variants holding items, forgotten with bow and sheathed sword, red / blue / black foxhounds incl. a
resting one, wraith, the three items in the hotbar).
