# U29 worldbeyond: Nether and End world generation

Quark 4.1-488 modules (category `world`): **NetherObsidianSpikes**, **NoMoreLavaPockets**,
**ChorusVegetation**, **SpiralSpires**. SPEC §9 lists "remaining Nether/End world modules": there
are none (every other Quark world module is assigned elsewhere, SPEC §9 notes).

## 1. Modules covered

### NetherObsidianSpikes
Obsidian spikes rising out of the Nether lava sea. Per chunk, with `chancePerChunk`, the generator
makes `triesPerChunk` tries: a random column of the chunk is scanned from y 50 down to y 11 for the
first lava block; a spike is placed there if the 5x5 column above it (spike height + 2) holds only
air or lava. With `bigSpikeChance` the spike is big, and big spikes hold a blaze spawner, a chest
with the Nether fortress loot and a glowstone (or Blaze Lantern) cap when `bigSpikeSpawners`.

### NoMoreLavaPockets
Removes the Nether lava springs that do not need a block below (Java `spring_open`, `spring_closed`,
`spring_closed_double`: the hidden lava pockets and the lava falls out of netherrack walls).

### ChorusVegetation
Two plants of the outer End, **Chorus Weeds** and **Chorus Twist**, generated in patches on end
stone in the End highlands (and less in the midlands). They teleport around: on random ticks, when
trampled by a living entity (weeds only; this may also spawn an endermite), next to water, and
when bone-mealed (which also multiplies them).

### SpiralSpires
Rare clusters of tall **Dusky Myalite** spires in the outer End highlands, each topped with a
spiral of **Myalite Crystal**. Crystals in a line form a *viaduct*: an entity teleporting next to
one end (ender pearl, enderman, chorus fruit, `/tp`) arrives at the other end instead. Crystals are
renewable: a Corundum crystal growing on top of a myalite crystal turns into myalite crystal.

## 2. Blocks and items

| Id | Name (en) | States | Recipe | Drops | Creative |
|---|---|---|---|---|---|
| `lepton:chorus_weeds` | Chorus Weeds | none | none | itself with shears only | Nature, after chorus plant |
| `lepton:chorus_twist` | Chorus Twist | none | none | itself with shears only | Nature, after chorus weeds |
| `lepton:dusky_myalite` | Dusky Myalite | none | none | itself (pickaxe needed) | Construction, after purpur |
| `lepton:myalite_crystal` | Myalite Crystal | `lepton:hue` 0..15 (default 0) | none | itself (pickaxe needed) | Nature |

- **Chorus weeds / twist**: no collision, selection box 2..14 x 0..13 x 2..14 px, breaks instantly,
  replaceable, grass sounds, random XZ render offset of up to 4 px (Java `OffsetType.XZ`), flammable,
  survive only on top of `minecraft:end_stone` (they pop off otherwise), emit portal particles
  (Bedrock: the vanilla `minecraft:portal` ambient particles cannot be attached to a custom
  block, so the server spawns portal particles above weeds near players). Pistons destroy them,
  liquids pop them. Both fit in a vanilla flower pot (`minecraft:flower_pottable`): the potted
  forms Quark registers (`quark:potted_chorus_weeds` / `_twist`, dropping the pot and the plant)
  are the vanilla pot holding the plant on Bedrock.
- **Dusky myalite**: stone, hardness 1.5, resistance 6, map colour terracotta purple (`#7a4958`).
- **Myalite crystal**: glass-like, translucent, light level 14, hardness 0.5, resistance 1200,
  glass sounds, map colour purple. Its colour depends on the block position (Quark's
  `MyaliteColorLogic`: a fixed-seed simplex noise field, hue between about 0.6 and 0.9, saturation
  0.7, brightness 0.8). Bedrock has no per-position block tint, so the colour is quantised into 16
  hue steps stored in `lepton:hue` and set from the position whenever a crystal is placed, moved or
  generated; the item shows the colour of position (0, 0, 0) as in Quark.
- Tags: crystals are `lepton:myalite_crystal` (block tag on the block itself) for other units.

## 3. Mechanics

### 3.1 World generation framework
Quark's generators run as features of a decoration step in every chunk of a configured dimension.
Lepton hooks the post-processing decoration of the Bedrock generators
(`OverworldGeneratorMultinoise`, `NetherGenerator`, `TheEndGenerator`::`decorateWorldGenPostProcess`)
and runs its generators after vanilla's for the chunk being decorated, in Quark's stage order
(spires: surface structures; spikes: underground decoration; chorus: vegetal decoration). Writes
are limited to the chunk and its 8 neighbours (Java's `WorldGenRegion` write radius 1).

Randomness is Java's `java.util.Random` LCG, reimplemented, seeded from the 64-bit world seed like
Zeta: single-chunk generators use `WorldgenRandom.setDecorationSeed(seed, chunkX*16, chunkZ*16)`
then `setFeatureSeed(decorationSeed, stage*10000, stage)`; multi-chunk generators (spires, chorus)
use `Random(seed)`'s first two longs `xSeed`, `zSeed` and seed each chunk with
`(xSeed*cx + zSeed*cz) ^ seed ^ salt` (salt 1892 spires, 2093 chorus). For every chunk within
`ceil(radius/16)` chunks of the chunk being decorated, `getSourcesInChunk` decides with that
chunk's random whether it is a source; each source then decorates the current chunk with the
current chunk's random. Same seed, same world.

**End biomes.** Bedrock's End has one biome; Java's are derived from the End island density. Lepton
computes them the Java way from Bedrock's own island height
(`TheEndGenerator::getIslandHeightValue(chunkX, chunkZ, 1, 1)`, the same function that shapes the
islands): chunks within 64 chunks of the origin are `minecraft:the_end`; otherwise with
`d = (h - 8) / 128`: `d > 0.25` highlands, `d >= -0.0625` midlands, `d < -0.21875` small islands,
else barrens (`minecraft:end_highlands`, `end_midlands`, `small_end_islands`, `end_barrens`).
Other dimensions use the Bedrock biome name with a `minecraft:` prefix.

### 3.2 NetherObsidianSpikes
- Chunk: `nextFloat() < chancePerChunk` (0.1); then `triesPerChunk` (4) times: column
  `(nextInt(16), 50, nextInt(16))`, scan down while y > 10 for the first lava block.
- Spike at the lava block P: `heightBelow = 10`, `bottom = 3 + nextInt(3)`, `middle = 2 + nextInt(4)`,
  `top = 2 + nextInt(3)`. With `nextFloat() < bigSpikeChance` (0.03): bottom +7, middle +8, top +4,
  spawner = `bigSpikeSpawners`.
- Check: every block of the 5x5 area around P from P.y up to `bottom+middle+top+2` blocks must be
  air or lava, else nothing is placed.
- Obsidian: 3x3 from P.y-10 to P.y+bottom-1 (skipping unbreakable blocks such as bedrock), a 5-block
  plus shape for `middle` blocks above, a single column for `top` blocks above that.
- Spawner (big spikes): the first top block becomes glowstone (or `lepton:blaze_lantern` when U8's
  CompressedBlocks and its Blaze Lantern are enabled and the block exists), the block below it a
  blaze spawner, the block below that a chest with `loot_tables/chests/nether_bridge.json`.
- Config: `chancePerChunk`, `bigSpikeChance`, `triesPerChunk`, `bigSpikeSpawners`,
  `netherObsidianSpikesDimensions`.

### 3.3 NoMoreLavaPockets
Hook `NetherSpringFeature::place`: when the module is enabled and the spring's block is lava, the
spring is not placed. Bedrock places both kinds of Nether lava springs (hidden in netherrack and
open on one side) with this feature, matching Java's springs without `requiresBlockBelow`. The
basalt delta lava (Java `spring_delta`, which needs a block below) is a different Bedrock feature
and stays.

### 3.4 ChorusVegetation
- **Generation** (End, multi-chunk, radius `chorusVegetationRadius` 7 -> 1 chunk): a chunk is a
  source if its corner is at least 1050 blocks from (0,0), `rarity > 0`, `nextInt(rarity) == 0`
  (150) and its biome is the highlands. For each chunk around a source: `chunkAttempts` (120)
  tries at `(nextInt(16), 100, nextInt(16))`; chance by biome (`highlandsChance` 1.0,
  `midlandsChance` 0.2, `otherEndBiomesChance` 0); beyond 6 blocks from the source corner the
  chance is multiplied by `1 - atan((d² - 36) / 50) / (π/2)` (d² = squared horizontal distance);
  on success scan down while y > 40 to end stone; if the block above it is air, place chorus twist
  with 10 % (`nextDouble() < 0.1`), else chorus weeds.
- **Teleport** (range R, growth chance G): pick an offset with `nextInt(R+1) - R/2` on x and z
  (not both 0), start 10 blocks above, scan down up to 20 blocks for end stone; if found with air
  above, place a copy there; with probability G remove the original (portal particles, enderman
  teleport sound at volume 0.1, pitch 5 + random); reverse-portal particles at the new spot.
- **Random tick**: with `passiveTeleportChance` (0.2): teleport (R 4, G = 1 - `teleportDuplicationChance`).
  Bedrock: custom blocks get no Java-style random ticks, so the blocks carry `minecraft:tick` with a
  uniform interval of 1..2730 ticks (mean 1365.5 = Java's 4096/3 ticks between random ticks at the
  default `randomTickSpeed` 3); the tick runs the random-tick logic.
- **Trampling** (weeds only, not twist): a living entity that is not an enderman or endermite inside
  the block teleports the weeds (R 4, G 1 - dup chance); then with `endermiteSpawnChance` (0.01) an
  endermite spawns at the new spot. Bedrock: checked every tick for living entities overlapping
  the block.
- **Water**: on a neighbour change (`Block::neighborChanged` of the plant), if water is next to it
  (any of the 6 sides, including waterlogged blocks): teleport (R 8, G 1).
- **Bone meal** (always succeeds): 3..5 teleports with R 10, G 0 (copies), then one with R 4, G 1.
  Works for players (bone meal used on the plant) and dispensers (Bedrock's fertilizer dispense,
  `FertilizerItem::dispense`, aimed at a plant: one bone meal is used).

### 3.5 SpiralSpires
- **Generation** (End, multi-chunk, radius `spiralSpiresRadius` 15 -> 1 chunk): source chunk:
  corner at least 1050 blocks from (0,0), `nextInt(rarity) == 0` (200). For each chunk around it:
  `dist = distSqr(chunk corner, source) / (16*radius)²`; skip if `dist > 0.5` and
  `nextDouble() < 1.5 - dist`; position `(nextInt(16), 256, nextInt(16))` must be in `biomes`
  (default `minecraft:end_highlands`); scan down to end stone (give up below y 10); make a spire.
- **Spire**: `height = 50 + nextInt(20)`, `hc = 5 + nextDouble()`. If any block from y-5 to height
  is not air, end stone, obsidian, crying obsidian or myalite crystal: nothing. For y from -5 to
  height-1 (below 0 only where the block is not solid): radius
  `ri = ceil(|(π/2 - atan((max(0,y)+0.5) / (height/hc))) * 4|)`; fill the disc i,j in (-ri, ri),
  i²+j² <= ri² with dusky myalite, edge cells (|i| or |j| = ri-1) with 20 % myalite (`lepton:myalite`
  of U3; dusky myalite when that block is not registered).
- **Crystal spiral**: steps `80 + nextInt(30)`, 10 substeps each; deterioration start
  `(0.5 + nextDouble()*0.3) * steps*10`; `spin = ±(0.12 + nextDouble()*0.16)`,
  `spread = 0.12 + nextDouble()*0.04`, `upward = nextDouble()*0.2`. Step i:
  `x = (int)(sin(i*spin/10) * i*spread/10)`, `z = (int)(cos(...) * ...)`,
  `y = top + round(i/10 * upward)`; placed with chance 1, decreasing linearly to 0 after the
  deterioration start.
- **Viaducts** (`enableMyaliteViaducts`): when an entity teleports (`Actor::teleportTo` with a cause
  of projectile/ender pearl, chorus fruit, command or behaviour, e.g. endermen) to a loaded spot P
  (the target's feet position):
  look for crystals adjacent (6 sides) to P, else to P-1, else to P+facing. Walk: pick a random
  adjacent crystal not yet visited, repeat until a crystal has no unvisited neighbours; abort if
  more than `myaliteConduitDistance` (24) moves, or if a dead end touches two or more visited
  crystals (a loop). Target = the last crystal + (last - previous); if that is not air, the first
  air neighbour of the last crystal (for up/down one more block in that direction); none: abort.
  The entity arrives at the centre of the target block, with block-break particles along the path.
- **Renewable myalite** (`renewableMyalite`, needs U12's Corundum): when a non-waxed corundum
  (`lepton:<colour>_corundum`) grows on top of a corundum that stands on a myalite crystal, the
  middle corundum turns into myalite crystal (watched natively through block changes, ignoring
  player placements).

## 4. Screens
None. Quark's JEI hints are not shown (SPEC §8); their text is kept in the lang files.

## 5. Config
Section `worldbeyond` (`config/worldbeyond.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableNetherObsidianSpikes` | bool | true | module |
| `chancePerChunk` | double | 0.1 | chance for a chunk to contain spikes |
| `bigSpikeChance` | double | 0.03 | chance for a spike to be big |
| `triesPerChunk` | int | 4 | tries per spike chunk |
| `bigSpikeSpawners` | bool | true | big spikes hold a blaze spawner + chest |
| `netherObsidianSpikesDimensions` | object | `{"isBlacklist": false, "dimensions": ["minecraft:the_nether"]}` | DimensionConfig |
| `enableNoMoreLavaPockets` | bool | true | module |
| `enableChorusVegetation` | bool | true | module |
| `chorusVegetationRarity` | int | 150 | 1 in N chunks is a source |
| `chorusVegetationRadius` | int | 7 | feature radius (blocks; 1 chunk) |
| `chunkAttempts` | int | 120 | tries per chunk |
| `highlandsChance` / `midlandsChance` / `otherEndBiomesChance` | double | 1.0 / 0.2 / 0 | placement chance by biome |
| `passiveTeleportChance` | double | 0.2 | random-tick teleport chance |
| `endermiteSpawnChance` | double | 0.01 | endermite on trampling |
| `teleportDuplicationChance` | double | 0.01 | a teleport leaves the original in place |
| `enableSpiralSpires` | bool | true | module |
| `spiralSpiresDimensions` | object | `{"isBlacklist": false, "dimensions": ["minecraft:the_end"]}` | DimensionConfig |
| `biomes` | object | `{"tags": {"isBlacklist": true, "tags": []}, "biomes": {"isBlacklist": false, "biomes": ["minecraft:end_highlands"]}}` | CompoundBiomeConfig (must match both) |
| `spiralSpiresRarity` | int | 200 | 1 in N chunks is a source |
| `spiralSpiresRadius` | int | 15 | feature radius |
| `enableMyaliteViaducts` | bool | true | viaducts |
| `myaliteConduitDistance` | int | 24 | max viaduct length (2..1024) |
| `renewableMyalite` | bool | true | corundum on crystal grows crystal |

DimensionConfig dimension names: `minecraft:overworld`, `minecraft:the_nether`, `minecraft:the_end`.
Biome tags understood: `minecraft:is_end`, `minecraft:is_nether`, `minecraft:is_overworld`,
`c:is_end`, `c:is_nether`, `c:is_overworld`. Disabled modules: no generation, no behaviour; the
chorus and myalite blocks stay registered (the behaviour pack has no load conditions) but are
neither generated nor teleporting.

## 6. Strings
Quark's JEI hints (kept for reference): `quark.jei.hint.chorus_weeds`, `quark.jei.hint.dusky_myalite`,
`quark.jei.hint.myalite_crystal_get`, `quark.jei.hint.myalite_crystal_viaduct`,
`quark.jei.hint.myalite_crystal_grow` with Quark's English text. Advancement `use_viaduct`
("Chase Me") is not ported (SPEC §8: no custom advancements).

## 7. Bedrock differences / not ported
- Myalite crystal colour: 16 hue steps (block state) instead of a continuous per-block tint.
  Beacon beams do not take the crystal's colour (Bedrock beacons only tint with vanilla glass).
- Random ticks of chorus plants: scheduled ticks with the Java mean interval (see 3.4).
- Chorus ambient portal particles are spawned by the server near players instead of the client's
  `animateTick`.
- Potted chorus plants are the vanilla flower pot holding the plant (no separate block ids).
- The `use_viaduct` advancement trigger is not ported.
- Generation is decorated where Bedrock post-processes chunks; since the terrain differs from Java,
  feature positions differ from a Java world with the same seed (deterministic per Bedrock seed).

## 8. Tests
Selftests (dx 2900..2999; generation tests run in the real Nether / End far from spawn):
`worldbeyond.config`, `worldbeyond.random`, `worldbeyond.myalite_color`, `worldbeyond.spike`,
`worldbeyond.spike_blocked`, `worldbeyond.spike_spawner`, `worldbeyond.chorus_teleport`,
`worldbeyond.chorus_survive`, `worldbeyond.chorus_water`, `worldbeyond.chorus_trample`,
`worldbeyond.chorus_bonemeal`, `worldbeyond.chorus_tick`, `worldbeyond.crystal_hue`,
`worldbeyond.chorus_dispenser`, `worldbeyond.viaduct`, `worldbeyond.viaduct_command` (a real `/tp`),
`worldbeyond.viaduct_pearl` (a thrown ender pearl), `worldbeyond.viaduct_loop`, `worldbeyond.spire`, `worldbeyond.renewable`,
`worldbeyond.end_gen`, `worldbeyond.nether_gen`.
E2E: `tools/e2e/server/scenarios/worldbeyond.ps1` (all selftests, generation log lines);
`tools/e2e/client/scenarios/worldbeyond.ps1` (screenshots of the blocks, a spire and chorus patch).
