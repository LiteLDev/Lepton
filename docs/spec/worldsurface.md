# U28 worldsurface: Overworld world generation (fairy rings, fallen logs, monster boxes, Glimmering Weald, cherry grove water petals)

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `FairyRingsModule`,
`FallenLogsModule`, `MonsterBoxModule`, `GlimmeringWealdModule`, `CherryGroveWaterPetalsModule`
and their generators / blocks / features / data. SPEC §2.1 assigns no other world module to U28
(§9 note: the "remaining" Overworld world modules are none).

## 1. Modules covered

### FairyRings
Once per chunk (Quark stage TOP_LAYER_MODIFICATION, weight 0) the generator picks a random column of
the chunk. In a forest biome it spawns a ring with chance `forestChance`, in a plains biome with
`plainsChance`. It searches down from y 128 to the first dirt-like block (not below y 30) and
builds the ring one block below it: in a 7x7 square around the centre, cells with
`dx² + dz² ∈ [7, 10]` get one flower (on the highest dirt-like block with air above, scanning y +5
.. -3), and every other cell has its first small flower (scanning y +6 .. -2) removed. All flowers
of a ring are the same: one flower of the biome's flower feature (oxeye daisy when the biome has
none). 25-34 blocks below the centre (searching up to 10 more blocks down for stone) one ore of
`oresRaw` is placed with each of the 6 neighbours also turned into that ore with 50 % chance.

### FallenLogs
Once per chunk (TOP_LAYER_MODIFICATION, weight 1): a random column; `1 / rarity` chance
(`1 / sparseBiomeRarity` in biomes of the "lower density" list). The log block comes from the biome
(`biomeTags`: tag -> log; when several tags match, a random one). With `hollowChance` it is the
Hollow Logs version of the log when U6's Hollow Logs exist. Up to 5 attempts: random horizontal
direction, length 3-4; every log cell must be air / replaceable / a flower and stand on a
`fallen_log_can_spawn_on` block (dirt-like), or on water when the per-attempt `onWaterChance` roll
succeeded. Decoration by climate (temperature T clamped 0..1, humidity H = downfall·T): T < 0.3 ->
ferns; H > 0.5 -> moss carpets (70 % on top of each log, never on water); H > 0.75 -> vines.
For each log cell: each of the two side cells gets a random decoration with 60 %; with 40 % each
the cell behind the start and the cell after the end of the log get one too.

### MonsterBox
Underground "monster box" block. Generation (UNDERGROUND_DECORATION, weight 6, never in flat
worlds): while `chance > 0 && rand < chance` (chance starts at `monsterBoxChancePerChunk`,
decremented by 1 per try, so values above 1 place several), pick x,z in the chunk and y in
`[minY, maxY)`, then scan down at most `searchRange` blocks (not below `minY`) for a replaceable,
non-liquid cell above a stone-based block with a sturdy top face. The box ticks (not in
peaceful): when a non-spectator player is within `activationRange` of its centre it growls
("Monsters arise", volume 0.5) and after 40 more ticks breaks with block-break particles and
spawns `minMobCount..maxMobCount` mobs from the `monster_box_spawns` table (witch 1, cave spider 2,
zombie 7), each at the box centre with a random motion of ±0.2 per axis and marked as spawned by a
box. While idle it shows flame particles, while breaking large smoke. A marked mob killed by a
player (mob loot on) also drops one roll of the `monster_box` loot table when
`enableExtraLootTable` is on. The block: hardness 25, metal sound, not pathfindable, no drops,
creative tab "spawn eggs" next to the spawner.

### GlimmeringWeald
A rare deep cave biome: Quark adds `quark:glimmering_weald` to the Overworld biome parameter list
at temperature/humidity/weirdness full range, continentalness `[-1.2, 0.75]`, erosion
`[-0.28, 0.08]`, depth `[1.03, 1.1]`, offset 0 (so it wins wherever it is the nearest climate
point: deep below the surface, away from dripstone caves and the deep dark). Its features:
`glow_shrooms` (625 tries per chunk: count 125 x 5, uniform height from the bottom to y 256,
scan down 12 blocks for a solid block below air, +1 y, biome filter) and `glow_shrooms_extras`
(200 tries, uniform height, +1 y), `ore_lapis_extra` (12 lapis ore veins per chunk, y -64..0), plus
the vanilla Overworld cave features. Fog colour `#C0D8FF`, music `quark:music.glimmering_weald`,
mobs: common monsters, bats, glow squids, and stonelings (creature, weight 200, groups 1-4).

Blocks:
- **Glow Shroom** (light 10): a small mushroom that only stays on deepslate (or a barrier). Bone
  meal (always valid, 40 % success) grows a huge glow shroom when the block below is deepslate.
  End-rod particle (1/12 per animate tick) when the block above is air.
- **Huge glow shroom** (feature `glow_shrooms`): stem height 2 + rand(4) (the second part of the
  stem shifted one block horizontally when > 0), needs free space (radius 2 around the stem,
  `cap radius + 1` = 2 around the cap level), rings (glow shroom rings facing out) on the
  1-2 blocks below the cap top on each side, a 3x3 cap (`glow_shroom_block` with every outer face
  but `down`), and with 60 % one more cap block on top. If the ground isn't deepslate a single
  glow shroom is placed instead.
- **Glow Shroom Block** (light 12, glowing) / **Glow Shroom Stem**: huge-mushroom blocks with six
  face states (`north/east/south/west/up/down`, true = skin, false = pores); a face touching
  another block of the same kind is pores. Not flammable. Shroom block: silk touch drops itself,
  otherwise 0-2 glow shrooms (uniform -6..2, at least 0). Stem: silk touch only. End-rod drips
  below a shroom block above air or a ring.
- **Glow Shroom Ring**: a ring fungus on the side of a block (`facing` north/south/east/west,
  waterloggable, no collision, instant break, fungus sound, breaks when its wall goes away).
- **Glow Lichen Growth** (light 8): small plant on a block with a sturdy top. Bone meal (valid
  when a horizontal neighbour is air above a sturdy top; always succeeds) copies it to a random
  such neighbour. Mycelium particles (11 per animate tick).
- **Potted Glow Shroom** (light 10), **Potted Glow Lichen** (light 8).
- Compost chances: glow shroom, shroom block, stem and ring 65 %, lichen growth 50 %.
- Item tag `glow_shroom_feedables` (glow shroom): stoneling food (used by U21).

Feature `glow_shrooms_extras`: in a 5x13x5 box around each try, every air cell with 50 % rolls
r: r > 0.85 -> lichen growth if deepslate is below; 0.35 < r <= 0.85 -> a glow lichen attached to
the first neighbouring deepslate face (order down, up, north, south, west, east).

### CherryGroveWaterPetals
Once per chunk with chance `cherryGroveWaterPetalsChancePerChunk`, `triesPerChunk` tries: a random
column at y 70 of the chunk in a biome of `biomes`, scanning down to y 10 for the first cell above
water. There it places a `size`(±`sizeVariation` per axis) rectangle of 4-petal water petals (random
facing) on air above water, and around it edge petals (2 petals facing the counter-clockwise
direction of the edge; with `edgeVariation` 1 or 3 petals facing that way). With `useCarpet` it
places Cherry Leaf Carpets (rectangle only) instead. Petals need U25's PetalsOnWater
(`lepton:water_pink_petals`), carpets U6's Leaf Carpet (`lepton:cherry_leaf_carpet`); without the
needed block nothing is placed (as in Quark).

## 2. Blocks and items

| Id | Name (en) | States | Light | Drops | Creative |
|---|---|---|---|---|---|
| `lepton:monster_box` | Monster Box | - | 0 | nothing | items, after spawn eggs (Bedrock has no spawner in the creative menu) |
| `lepton:glow_shroom` | Glow Shroom | - | 10 | itself | nature |
| `lepton:glow_shroom_block` | Glow Shroom Block | `lepton:north/east/south/west/up/down` (bool, default true) | 12 | silk touch: itself; else 0 (7/9), 1 (1/9), 2 (1/9) glow shrooms | nature |
| `lepton:glow_shroom_stem` | Glow Shroom Stem | same | 0 | silk touch only | nature |
| `lepton:glow_shroom_ring` | Glow Shroom Ring | `minecraft:block_face` (trait, north/south/east/west = facing) | 0 | itself | nature |
| `lepton:glow_lichen_growth` | Glow Lichen Growth | - | 8 | itself | nature |
| `lepton:potted_glow_shroom` | Potted Glow Shroom | - | 10 | flower pot + glow shroom | hidden |
| `lepton:potted_glow_lichen_growth` | Potted Glow Lichen | - | 8 | flower pot + lichen growth | hidden |

- No recipes (Quark has none for these blocks).
- Placement rules (Bedrock `minecraft:placement_filter`, also re-checked when the support changes):
  glow shroom only on top of deepslate or a barrier; ring only on the side of a block; lichen
  growth on top faces (sturdiness checked natively, see §3).
- Potted blocks come from using a glow shroom / lichen growth on an empty vanilla flower pot;
  using the potted block with an empty hand takes the plant back (vanilla flower pot again).
- Hardness: monster box 25 (pickaxe, explosion resistance 25); shroom block / stem 0.2 (axe);
  ring, glow shroom, lichen growth, pots 0 (instant).
- Block tags: `lepton:glow_shroom_block` (shroom block + stem; huge-mushroom face logic),
  `lepton:monster_box`.

## 3. Mechanics

### World generation hook
- `OverworldGeneratorMultinoise::decorateWorldGenPostProcess` (and the Nether / End generators,
  for the dimension filters) is hooked: after the game decorated a chunk (its 3x3 neighbourhood is
  writable, like Java's `WorldGenRegion`), Lepton runs its generators for that chunk, in Quark's
  order: underground decoration (GW features, monster boxes), then top-layer modification (fairy
  rings, fallen logs, water petals). Flat worlds never call it (Quark: no flat-world generation).
- Each generator gets its own random generator seeded from (world seed, chunk x, chunk z,
  generator id), so a seed always produces the same features (independent of chunk order).
- Dimension filters (`*Dimensions`): `{"isBlacklist": false, "dimensions": ["minecraft:overworld"]}`.
- Biomes are named with Java ids in the config. A biome matches under its Java path
  (`dark_forest`) and under its legacy Bedrock name (`roofed_forest`, `ice_plains`, ...), so the
  lists work with both naming schemes of Bedrock's biome registry (1.26 reports
  `minecraft:<name>`; the namespace is ignored).
- Generation runs on the game's world-generation threads: it only reads a snapshot of the config
  and a block palette resolved on the server thread (world ready / config reload; resolved again
  on first use when a chunk generates before the world's packs were ready).
- BDS enables mods after the spawn area was generated: those chunks have no Lepton features
  (Quark's features are part of the world generation, so pre-existing chunks never get them either).

### Fairy rings
As §1. Forest biomes (Java `#minecraft:is_forest`): forest, flower forest, birch forest, old growth
birch forest, dark forest, grove; plains (`#c:is_plains`): plains, sunflower plains (both lists
are config keys). "Dirt" = Java `#minecraft:dirt` (dirt, grass block, podzol, coarse dirt,
mycelium, rooted dirt, moss block, mud, muddy mangrove roots); "stone" = `#c:stones` (stone,
granite, diorite, andesite, deepslate, tuff). Flowers per biome (Java's flower features): forest /
birch / dark forest / taiga-like: poppy or dandelion; flower forest: any of its 11 flowers;
plains / sunflower plains: tulips or poppy / azure bluet / oxeye daisy / cornflower / dandelion;
meadow: allium, poppy, azure bluet, dandelion, cornflower, oxeye daisy; swamp: blue orchid;
cherry grove: pink petals; default oxeye daisy. Ores from `oresRaw` (missing ids are skipped with
a warning, like Quark).

### Fallen logs
As §1. Heightmap: Bedrock's "top solid block, ignoring water and leaves" (Java OCEAN_FLOOR_WG).
Biome tag lists and the reduced-density list are config keys (Quark's datapack tags):
- `has_fallen_acacia`: savanna, savanna plateau; `has_fallen_birch`: forest, birch forest, old
  growth birch forest; `has_fallen_cherry`: cherry grove; `has_fallen_dark_oak`, `has_fallen_mangrove`:
  none; `has_fallen_jungle`: jungle, sparse jungle; `has_fallen_oak`: forest, flower forest,
  meadow, windswept forest, swamp, wooded badlands; `has_fallen_spruce`: grove, windswept forest,
  taiga, old growth spruce taiga, snowy taiga.
- `has_lower_fallen_tree_density`: meadow, windswept forest, sparse jungle, swamp, savanna,
  savanna plateau.
Vanilla logs are placed with `pillar_axis` x/z; hollow logs (`lepton:hollow_<wood>_log`, U6) with
`minecraft:block_face` east/south (the `lepton:pillar` contract). Decorations: fern, moss carpet,
vine (`vine_direction_bits` towards the log). Climate = the Bedrock biome's temperature/downfall.

### Monster box
- Generation as §1. "Stone based" = Java's bass-drum instrument or stone map colour: the
  Bedrock `stone` block tag or a stone-like id (stone, deepslate, tuff, granite, diorite,
  andesite, calcite, cobblestone, ores, basalt, blackstone, netherrack, ...).
- Every generated or placed box is recorded (world DB `lp:worldsurface:boxes`); the server tick
  checks the recorded boxes near players. Activation: a non-spectator player whose position is
  closer than `activationRange` to the box centre, difficulty not peaceful. Growl:
  `lepton.monster_box.growl` (volume 0.5, pitch 1); 41 ticks later (Java: breakProgress > 40) the
  box is removed with the block-destroy particles and mobs are spawned.
- Spawns: `mods/Lepton/behavior_packs/.../loot_tables/lepton/monster_box_spawns.json` (spawn eggs,
  weights witch 1 / cave spider 2 / zombie 7; edited like Quark's datapack table) is read at world
  ready; each roll spawns the egg's mob at the box centre with motion (rand-0.5)·0.4 per axis.
  Spawned mobs get the entity tag `lepton:monster_box_spawned`.
- Extra loot: when such a mob dies by a player (last hurt by a player) and `doMobLoot` is on,
  `loot spawn <pos> loot "lepton/monster_box"` drops one roll of Quark's table (raw iron/copper/gold,
  diamonds, cobwebs, TNT, infested blocks, heads, bone blocks, coal, damaged diamond pick/sword,
  bread, rabbit hide, golden apple, name tag, enchanted book, 12 music discs, horse armours,
  saddle, monster box; weights as Quark).
- Particles (server side, every tick, to players within 32 blocks): `minecraft:basic_flame_particle`
  while idle, `minecraft:basic_smoke_particle` while breaking, at a random point in the block.

### Glimmering Weald
- **Where**: Bedrock gives no way to add a biome to the Overworld climate parameter list, so
  Lepton evaluates Quark's climate point itself with the game's own climate noise
  (`OverworldNoises3d::sample` of the world generator): a 4x4x4 cell is Glimmering Weald when
  the distance of its climate target to Quark's point is smaller than the distance to the
  nearest climate point of the vanilla biome Bedrock chose for that cell (the vanilla parameter
  list of `BiomeSource3d`; if it can't be read, when the target lies inside Quark's ranges). This
  is the nearest-point rule Java uses, restricted to the two candidates that matter.
- **Features** (in Glimmering Weald cells only, the biome filter): glow shrooms (625 tries:
  uniform y from the bottom to 256, scan down 12 for a solid block below air), extras (200 tries),
  12 extra lapis veins (size 7, y -64..0, replacing stone/deepslate). The vanilla features of the
  underlying biome stay (Bedrock already placed them).
- **Fog**: players whose position is in Glimmering Weald get the fog `lepton:glimmering_weald`
  (`fog <player> push`, removed when they leave; checked every second).
- **Stonelings**: if U21's `lepton:stoneling` exists, Glimmering Weald runs the creature spawn of
  its biome entry (weight 200, group 1-4) near players (every 20 s per player: one try at a random
  dark cave floor cell 24..48 blocks away, ≤ 8 stonelings within 64 blocks), since a Bedrock
  biome can't list it.
- **Bone meal** on a glow shroom / lichen growth (with the PlayerInteractBlock event; deferred one
  tick): consumes one bone meal (not in creative), green particles, then the growth as §1.
- **Huge-mushroom faces**: when a shroom block / stem is placed, its faces touching another
  block of the same kind become pores, and so do the touching faces of that neighbour.
- **Composter**: using a glow shroom (65 %), shroom block (65 %), stem (65 %), ring (65 %) or lichen
  growth (50 %) on a vanilla composter below level 7 consumes one and raises the level with that
  chance (level 7 then ripens to 8 by the game).
- **Pots**: see §2.
- **Lichen growth survival**: needs a sturdy top face below; placing it elsewhere is refused.
- Ambient particles of glow shrooms, lichen growths and shroom blocks: client mod only (§7).

### Cherry grove water petals
As §1, biome filter `biomes` (CompoundBiomeConfig). Water petals: `lepton:water_pink_petals` with
`lepton:amount` 1..4 (fallback `lepton:growth` 0..3) and `minecraft:cardinal_direction` (U25's
block). Carpet: `lepton:cherry_leaf_carpet` (U6).

## 4. Screens

None. (No Quark screen; nothing needs a fallback.)

## 5. Config

Section `worldsurface` (`config/worldsurface.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableFairyRings` | bool | true | FairyRings module |
| `forestChance` | double | 0.00625 | chance per chunk in forest biomes |
| `plainsChance` | double | 0.0025 | chance per chunk in plains biomes |
| `fairyRingsDimensions` | DimensionConfig | overworld whitelist | |
| `oresRaw` | string[] | emerald ore, diamond ore | ores under a ring |
| `fairyRingsForestBiomes` | string[] | Java `#minecraft:is_forest` | (Lepton) the forest tag |
| `fairyRingsPlainsBiomes` | string[] | Java `#c:is_plains` | (Lepton) the plains tag |
| `enableFallenLogs` | bool | true | FallenLogs module |
| `fallenLogsDimensions` | DimensionConfig | overworld whitelist | |
| `hollowChance` | double | 0.7 | share of hollow logs (needs Hollow Logs) |
| `rarity` | int | 5 | 1 in N chunks |
| `onWaterChance` | double | 0.1 | chance per attempt to allow water below |
| `sparseBiomeRarity` | int | 12 | 1 in N chunks in reduced-density biomes |
| `biomeTags` | string[] | Quark's 8 `has_fallen_<wood>=minecraft:<wood>_log` | tag -> log |
| `fallenLogsBiomeTagMembers` | object tag -> string[] | Quark's tag files | (Lepton) the biome tags |
| `fallenLogsReducedDensityBiomes` | string[] | Quark's `has_lower_fallen_tree_density` | (Lepton) |
| `enableMonsterBox` | bool | true | MonsterBox module |
| `monsterBoxChancePerChunk` | double | 0.2 | may exceed 1 (several per chunk) |
| `minY` / `maxY` | int | -50 / 0 | height range |
| `minMobCount` / `maxMobCount` | int | 5 / 8 | mobs per box |
| `monsterBoxDimensions` | DimensionConfig | overworld whitelist | |
| `enableExtraLootTable` | bool | true | extra drops of box mobs |
| `activationRange` | double | 2.5 | blocks |
| `searchRange` | int | 15 | blocks scanned down |
| `enableGlimmeringWeald` | bool | true | GlimmeringWeald module |
| `minDepthRange` | double | 1.55 | kept for parity: commented out in Quark 4.1-488 (no effect there or here) |
| `maxWeirdnessRange` | double | 2 | same |
| `enableCherryGroveWaterPetals` | bool | true | module |
| `cherryGroveWaterPetalsChancePerChunk` | double | 0.5 | |
| `triesPerChunk` | int | 1 | |
| `size` / `sizeVariation` | int | 3 / 1 | |
| `edgeVariation` | double | 0.25 | |
| `useCarpet` | bool | false | |
| `biomes` | CompoundBiomeConfig | biomes whitelist `minecraft:cherry_grove` | |
| `cherryGroveWaterPetalsDimensions` | DimensionConfig | overworld whitelist | |

Shapes: DimensionConfig `{"isBlacklist": bool, "dimensions": ["minecraft:overworld" | "minecraft:the_nether" | "minecraft:the_end"]}`.
CompoundBiomeConfig `{"tags": {"isBlacklist": true, "biomeTags": []}, "biomes": {"isBlacklist": false, "biomes": ["minecraft:cherry_grove"]}}`
(both must match; tags are the Java tags Lepton knows: `minecraft:is_forest`, `c:is_plains`,
`minecraft:is_taiga`, `minecraft:is_jungle`, `minecraft:is_savanna`, `minecraft:is_badlands`,
`minecraft:is_ocean`, `minecraft:is_mountain`, `c:is_snowy`, `c:is_swamp`).

A disabled module generates nothing; a disabled GlimmeringWeald also removes its blocks from
use (bone meal, pots, composting do nothing) and its fog; a disabled MonsterBox stops existing
boxes from activating.

## 6. Strings

| Key | en_US |
|---|---|
| `quark.subtitles.growl` | Monsters arise |
| `biome.quark.glimmering_weald` | Glimmering Weald |
| `quark.jei.hint.glow_shroom` | Found in the rare Glimmering Weald biome deep underground. Bone Meal can be used to create a large variant. |
| `quark.jei.hint.glow_lichen_growth` | Found in the rare Glimmering Weald biome deep underground. |
| `tag.item.quark.glow_shroom_feedables` | Glow Shroom Stoneling Food |
| `lp.worldsurface.locate.found` | The nearest {0} is at {1} ({2} blocks away) |
| `lp.worldsurface.locate.none` | Could not find a {0} within {1} blocks |
| `lp.worldsurface.locate.unsupported` | {0} needs a normal (non-flat) Overworld |

The subtitle is the RP sound's subtitle (`texts.d`). Block names: §2 (`texts.d`).

## 6a. Commands

| Command | Permission | What it does |
|---|---|---|
| `/quark locate glimmering_weald` | operator | Nearest Glimmering Weald cell (spiral, 6400 blocks, like `/locate biome`). |
| `/quark locate monster_box` | operator | Nearest recorded monster box. |
| `/quark worldgen stats` | operator | Counters of generated features since start (`worldsurface gen: ...`). |

## 7. Bedrock differences / not ported

- **Glimmering Weald is not a real Bedrock biome**: Bedrock 1.26 can't register a biome into the
  Overworld multi-noise parameter list (data-driven biomes can only *replace* vanilla biomes by a
  2D noise, not by climate depth). Lepton evaluates Quark's climate point with the game's own
  noise (§3), so the area is where Quark would put it, but the chunk's biome id stays vanilla:
  `/locate biome` and the F3-like biome display don't know it (use `/quark locate`), and vanilla
  spawns / sky / water colour of the underlying biome apply (Quark's water colours are vanilla's
  defaults anyway). The fog is applied per player instead.
- **Music**: Quark's Glimmering Weald track is an original asset that can't be copied, and
  Bedrock has no per-player background-music API (the `/music` command is world-wide); not ported.
- **Advancements** ("Adventuring Time" with the biome): Bedrock has none.
- Particles of glow shrooms, lichen growths and shroom blocks (client-side `animateTick` in Java)
  are drawn by the client mod only; monster box flames/smoke are sent by the server, so every
  client sees them.
- Monster box growl: Quark's sound file can't be copied; `lepton.monster_box.growl` plays the
  vanilla ravager idle sounds pitched down.
- The world-gen RNG is Lepton's own (deterministic per seed), not Java's, and Bedrock's terrain
  differs from Java's anyway: features appear at the same rates, not at the same coordinates.
- Fairy ring flowers come from a per-biome flower list (Bedrock biomes don't expose their flower
  features to native code).
- Dispensers do not bone-meal glow shrooms / lichen growths (bone meal on custom blocks is only
  handled for players).
- Huge-mushroom face update happens on placement (Java also updates on neighbour changes from
  other sources such as pistons).

## 8. Tests

Selftests (dx 2800..2899), `src/feature/worldsurface/Tests.cpp`:

| Name | dx | Checks |
|---|---|---|
| `worldsurface.blocks` | 2800 | every block id exists with its states; light levels |
| `worldsurface.fairy_ring` | 2805 | ring builder on a built dirt/stone column: flowers on the ring cells, ore below |
| `worldsurface.fallen_log` | 2815 | log builder places a 3-4 long horizontal log on grass, decor rules |
| `worldsurface.monster_box_gen` | 2825 | generator places a box on stone below air, not on dirt, not in water |
| `worldsurface.monster_box_activate` | 2830 | a player next to a box: growl log, box gone after 41 ticks, 5-8 tagged mobs |
| `worldsurface.huge_glow_shroom` | 2840 | huge shroom on deepslate (stem, cap, rings), single shroom otherwise |
| `worldsurface.glow_extras` | 2850 | extras feature puts lichen/lichen growth next to deepslate |
| `worldsurface.bonemeal` | 2855 | bone meal on lichen growth spreads; on glow shroom consumes |
| `worldsurface.shroom_faces` | 2860 | placing two shroom blocks side by side turns the touching faces to pores |
| `worldsurface.water_petals` | 2865 | petal patch on water (skipped without U25's block) |
| `worldsurface.composter` | 2870 | composting a glow shroom raises the level (seeded) |
| `worldsurface.pot` | 2875 | pot / un-pot a glow shroom |
| `worldsurface.climate` | 2880 | GW climate rule on synthetic targets; on a non-flat world `/quark locate` finds GW |
| `worldsurface.config` | 2885 | biome mapping, dimension / compound biome configs |
| `worldsurface.worldgen` | 2890 | non-flat worlds only: features were generated around spawn |

E2E: `tools/e2e/server/scenarios/worldsurface.ps1` (flat world: all selftests),
`worldsurface-gen.ps1` + `.prelaunch.ps1` (DEFAULT world: generation, locate, GW features),
`tools/e2e/client/scenarios/worldsurface.ps1` (screenshots of every block: monster box, glow
shroom, huge glow shroom, rings, lichen growth, pots).
