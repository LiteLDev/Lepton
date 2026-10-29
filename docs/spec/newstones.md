# U3 newstones: limestone, jasper, shale, myalite, permafrost and the big stone clusters

## 1. Modules covered

### NewStoneTypes (world, on by default)
Adds four natural stones: **limestone**, **jasper**, **shale** (Overworld) and **myalite** (End).
Each has a polished form; the raw stone gets stairs, slab, wall (and a vertical slab, VerticalSlabs);
the polished stone gets stairs and slab (and a vertical slab). Myalite is tinted by an invisible
"field": its colour depends on the block position (`MyaliteColorLogic`). With BigStoneClusters off
(or the stone's cluster config disabled) the stones generate as ore-like pockets instead
(`oregenLower` / `oregenUpper`). Journeyman masons can sell the raw stones and buy the polished ones
(`addNewStonesToMasonTrades`; limestone, jasper and shale only).

The MoreStoneVariants part for these four stones (bricks + stairs/slab/wall/vertical slab, chiseled
bricks, pillar; Quark registers them in `MoreStoneVariantsModule`) is owned by U3 because U3 owns the
material (SPEC §2.3). These blocks follow U2's options in `config/stones.json`
(`enableMoreStoneVariants`, `enableBricks`, `enableChiseledBricks`, `enablePillar`).

### Permafrost (world, on by default)
Adds **permafrost** and **permafrost bricks** (+ stairs, slab, wall, vertical slab each). Permafrost
"underground biomes" generate inside the upper part of Frozen Peaks: big blobs (y 105..140) whose
cave floors and ceilings become permafrost (with small permafrost pillars on floors) and whose
other stone becomes packed ice.

### BigStoneClusters (world, on by default)
Huge, noise-shaped blobs of stone (radius 14..22 blocks) that replace the base stone underground:
calcite in mountains, limestone in swamps and oceans, jasper in badlands and sandy biomes, shale in
snowy and cold biomes, and floating myalite blobs in the air over the outer End highlands.

### VanillaStoneClusters (experimental, off by default)
"Big Stone Clusters for Granite, Diorite, and Andesite, like Quark pre-1.18": the same generator for
granite (mountains), diorite (savannas, jungles, mushroom fields) and andesite (forests).

Not in this unit: `dusky_myalite` and `myalite_crystal` belong to SpiralSpires (U29); U29 builds its
spires from `lepton:myalite` (this unit's id) and owns the crystal's tint.

## 2. Blocks and items

All blocks: hardness 1.5, explosion resistance 6 (permafrost family: 1.5 / 10), stone sounds, mined
with any pickaxe (Quark `requiresCorrectToolForDrops`: no drop without a pickaxe), drop themselves
(slabs: 2 when double). Raw stones carry the `stone` tag (Bedrock's `base_stone_overworld`-like
uses). Creative inventory: Construction tab (raw/polished stones in the Stone group, bricks in the
Stone Bricks group, shapes in the stairs/slab/wall groups).

### NewStoneTypes (S = limestone, jasper, shale, myalite)

| Id | English | zh_CN | Map colour |
|---|---|---|---|
| `lepton:S` | Limestone / Jasper / Shale / Myalite | 石灰石 / 碧玉石 / 页岩 / 幻境石 | stone `#707070` / terracotta red `#8e3c2e` / ice `#a0a0ff` / purple `#7f3fb2` |
| `lepton:S_stairs`, `S_slab`, `S_wall`, `S_vertical_slab` | "<S> Stairs" ... | | as S |
| `lepton:polished_S` | Polished <S> | 磨制<S> | as S |
| `lepton:polished_S_stairs`, `polished_S_slab`, `polished_S_vertical_slab` | "Polished <S> Stairs" ... | | as S |

Recipes (crafting table unless noted, counts as Quark):
- `polished_S`: `##/##` of S -> 4. Stonecutter S -> polished_S x1.
- Shapes of S: stairs `#  /## /###` -> 4, slab `###` -> 6, wall `###/###` -> 6, vertical slab `#/#/#`
  of slabs -> 3 and back 1 vertical slab -> 1 slab. Stonecutter S -> stairs 1, slab 2, wall 1,
  vertical slab 2.
- Shapes of polished_S: stairs, slab, vertical slab as above. Stonecutter polished_S -> stairs 1,
  slab 2, vertical slab 2; and S -> polished stairs 1, slab 2, vertical slab 2 (limestone, jasper,
  shale only: Quark has no raw-myalite -> polished-myalite-shape stonecutter recipes).

### MoreStoneVariants part (S = limestone, jasper, shale, myalite)

| Id | English | zh_CN |
|---|---|---|
| `lepton:S_bricks` | <S> Bricks | <S>砖 |
| `lepton:S_bricks_stairs`, `_slab`, `_wall`, `_vertical_slab` | <S> Brick Stairs / Slab / Wall / Vertical Slab | <S>砖楼梯 ... |
| `lepton:chiseled_S_bricks` | Chiseled <S> Bricks | 雕纹<S>砖 |
| `lepton:S_pillar` | <S> Pillar (axis = clicked face, like logs) | <S>柱 |

Recipes: bricks `##/##` of polished_S -> 4; chiseled `#/#` of S_bricks_slab -> 1; pillar `#/#` of
polished_S_slab -> 1; brick shapes as above (+ stonecutter bricks -> stairs 1 / slab 2 / wall 1 /
vertical slab 2). Stonecutter from S: bricks 1, brick stairs 1, slab 2, wall 1, vertical slab 2,
chiseled bricks 1, pillar 1; from polished_S: bricks 1, brick stairs 1, slab 2, wall 1, vertical
slab 2.

### Myalite tint
Every myalite-family block (myalite, polished myalite, myalite bricks, chiseled myalite bricks,
myalite pillar and all their shapes; Quark's `MyaliteBlock`/`MyalitePillarBlock` and the shapes,
which inherit the parent's colour provider) has the state `lepton:tint` (int 0..15). Each value
selects a pre-tinted texture (grayscale art multiplied by the tint colour, as Java's block colour
does). See §3.

### Permafrost

| Id | English | zh_CN | |
|---|---|---|---|
| `lepton:permafrost` | Permafrost | 永冻石 | map colour light blue `#6699d8` |
| `lepton:permafrost_stairs`, `_slab`, `_wall`, `_vertical_slab` | Permafrost Stairs / ... | | |
| `lepton:permafrost_bricks` | Permafrost Bricks | 永冻石砖 | |
| `lepton:permafrost_bricks_stairs`, `_slab`, `_wall`, `_vertical_slab` | Permafrost Brick Stairs / ... | | |

Recipes: bricks `##/##` of permafrost -> 4; shapes as above for both; stonecutter permafrost ->
shapes, bricks 1, brick stairs 1, brick slab 2, brick wall 1, brick vertical slab 2; permafrost
bricks -> its shapes.

### Gates (what must be on for a block or recipe to exist)
`<S>` = `enableNewStoneTypes && enable<S>`; bricks = U2 `enableMoreStoneVariants && enableBricks`;
chiseled = also `enableChiseledBricks`; pillar = U2 `enableMoreStoneVariants && enablePillar`;
permafrost = `enablePermafrost`; every vertical slab also needs U1 `enableVerticalSlabs`
(`config/vslabs.json`). Other units' files count as "on" when missing. A gated-off block is left
out of the creative inventory and all recipes that make or use it are not loaded (as Quark's flag
conditions). Blocks already placed stay.

## 3. Mechanics

### Myalite colour (`MyaliteColorLogic`)
For a block position (x, y, z): `sp = 0.15f`; `fx = x*sp, fy = y*sp, fz = z*sp` (floats);
`xv = fx + sin(fz)*2`, `zv = fz + cos(fx)*2`, `yv = fy + sin(fy + PI/4)*2` (Minecraft's 65536-entry
float sine table); `n = PerlinSimplexNoise(LegacyRandomSource(4543543), octaves -4..4)
.getValue(xv + yv, zv + 2*yv, false)`; hue `h = n*0.15 - 0.3 + 0.05`; colour =
`HSBtoRGB(h, 0.7, 0.8)` (java.awt). Lepton computes the same value server-side and stores
`lepton:tint = clamp(floor((h + 0.4) / 0.3 * 16), 0, 15)` in the block: 16 hue steps of 6.75° over
the reachable hue range (blue -> purple -> magenta). The tinted textures use the colour at the
centre of each step.
- Placed blocks (by players, pistons, commands, other units' world generation) get their tint one
  tick later (or immediately during world generation); generated myalite is placed tinted.
- Items show step 8 (the item colour in Java follows the block the player looks at).

### Big stone clusters (Zeta `ClusterBasedGenerator` / `MultiChunkFeatureGenerator` / `ClusterShape`)
Run once per chunk in Bedrock's chunk post-processing (after the vanilla decoration of the chunk,
like Java's UNDERGROUND_DECORATION step), for the Overworld, Nether and End generators, filtered by
each config's `dimensions`. For a generator with name hash `hx` (Java `String.hashCode` of
`"minecraft:calcite"`, `"quark:limestone"`, `"quark:jasper"`, `"quark:shale"`, `"quark:myalite"`,
`"minecraft:granite"`, `"minecraft:diorite"`, `"minecraft:andesite"`):
- `radius = horizontalSize + horizontalVariation`; every chunk within `ceil(radius/16)` chunks may
  hold a source. `Random(seed)` gives `xSeed, zSeed`; a chunk's random is
  `java.util.Random((xSeed*cx + zSeed*cz) ^ seed ^ hx)`.
- Source (BigStoneClusterGenerator): `if rarity > 0 && rand.nextInt(rarity) == 0`:
  `pos = corner + (nextInt(16), nextInt(|maxY-minY|) + minY, nextInt(16))`; kept if the biome at
  the top of the world above it passes `biomes`.
- Shape: radii `horizontalSize + r.nextInt(horizontalVariation)` (x, z) and `verticalSize +
  r.nextInt(verticalVariation)` (y) with `r = Random(31*(31*(31+x)+y)+z)`; a position is inside if
  its normalised squared distance `<= 1` and `noise(phi + src.x, theta + src.z) + 0.5 > r` (2D
  Perlin simplex noise, octaves -4..4, seeded with `hx`; lerped back near `phi = PI`).
- Only the current chunk's 16x16 columns are written, y in `[max(minBuild+1, floor(src.y-ry)),
  min(maxBuild-1, ceil(src.y+ry)))`.
- Placement: `generateInAir` (myalite): only into air; otherwise only into blocks listed in
  `blocksToReplace` (`id` or `id|dimension`). `quark:` ids in the list mean `lepton:` ids.
- Order when clusters overlap: calcite, limestone, jasper, shale, myalite, granite, diorite,
  andesite (Quark sorts equal weights by identity hash, i.e. randomly per session).
- Defaults: `rarity 4`, y 20..80, size 14 + 0..8 (both axes); biomes: calcite `#is_mountain`;
  limestone `#c:is_swamp`, `#is_ocean`; jasper `#is_badlands`, `#c:is_sandy`; shale
  `#c:is_snowy`, `#c:is_cold`; myalite: End, biome `end_highlands`, in air, rarity 100, y 58..62,
  horizontal 20 + 0..5, vertical 40 + 0..9. A stone's cluster runs only if BigStoneClusters, the
  cluster's `enabled` and the stone itself are on (calcite: always).

### Ore pockets (`newStoneTypes<S>.oregenLower/oregenUpper`, Zeta `OreGenerator`)
Only when the stone's big clusters do not run (BigStoneClusters off or the cluster config's
`enabled` false) and the stone is enabled. Per chunk, in the stone's dimensions: `clusterCount`
pockets (a fraction is a chance for one pocket) at a random x/z in the chunk and y in
`[minHeight, maxHeight)`, shaped like Java's classic ore vein of `clusterSize` blocks (64), replacing
stone, deepslate, netherrack and end stone. Defaults: lower y 0..60, size 64, count 2; upper
y 64..128, size 64, count 0.1666666. Random: Java's `WorldgenRandom` decoration seed of the chunk,
feature seed per generator.

### Permafrost generation (`generationSettings`, Quark `UndergroundStyleGenerator` + `PermafrostStyle`)
Same multi-chunk scheme with name hash of `"permafrost"`; source: `rand.nextInt(rarity) == 0`, x/z in
the chunk, `y = nextInt(minYLevel, maxYLevel)`, kept if the biome passes (default `frozen_peaks`) and
the surface height at x/z is >= y. Inside the shape, only below the surface: blocks that are not
unbreakable are classified like Quark (`#quark:underground_biome_replaceable` = base overworld stone
incl. limestone/jasper/shale, netherrack, end stone): a solid replaceable block with air (or a
replaceable block) above is a floor -> permafrost, and with 1.5% chance a pillar of 3..5 permafrost
grows upward into air; below -> ceiling -> permafrost; else a border (empty/replaceable horizontal
neighbour) or any other replaceable block -> packed ice. Defaults: rarity 2, y 105..140, horizontal
72 + 0..21, vertical 15 + 0..3, Overworld only.

### Biome filters
Biomes are matched by Java name: Bedrock biome ids are mapped to Java ids (`swampland` ->
`swamp`, `mesa` -> `badlands`, `cold_taiga` -> `snowy_taiga`, `ice_plains` -> `snowy_plains`, ...),
and tags resolve to Java's tag contents (vanilla `minecraft:is_*` tags and NeoForge `c:is_*` tags
of 1.21.1; see `Biomes.cpp`). Bedrock's single End biome is split like Java's End biome source:
`the_end` within 64 chunks of the centre, else from the End island height of the chunk
(`TheEndGenerator::getIslandHeightValue(cx, cz, 1, 1)`): `> 40` end_highlands, `>= 0` end_midlands,
`< -20` small_end_islands, else end_barrens. Names in the config may be Java or Bedrock ids.

### Mason trades (`addNewStonesToMasonTrades`)
Java adds to the journeyman (level 3) pool of masons, per enabled stone (limestone, jasper, shale):
1 emerald -> 4 raw stone (max uses 16, villager XP 10) and 16 polished stone -> 1 emerald (max uses
16, XP 20), price multiplier 0.05. Java picks 2 offers per level from the pool (7 vanilla + 6). Bedrock
generates trades from its own trade table: when a stone mason's trades are generated, each new
journeyman (tier 2) offer is replaced with probability `Q / (7 + Q)` (Q = number of Quark offers) by
a distinct random Quark offer.

## 4. Screens
None. (Villager trades use the vanilla trade screen.)

## 5. Config
Section `newstones` (`config/newstones.json`).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableNewStoneTypes` | bool | true | NewStoneTypes module |
| `enableLimestone` / `enableJasper` / `enableShale` / `enableMyalite` | bool | true | per stone |
| `newStoneTypesLimestone` / `Jasper` / `Shale` | StoneTypeConfig | Overworld | ore pockets |
| `newStoneTypesMyalite` | StoneTypeConfig | End | ore pockets |
| `addNewStonesToMasonTrades` | bool | true | |
| `enablePermafrost` | bool | true | Permafrost module |
| `generationSettings` | ClusterConfig | see §3 | permafrost generation |
| `enableBigStoneClusters` | bool | true | BigStoneClusters module |
| `calcite`, `bigStoneClustersLimestone`, `bigStoneClustersJasper`, `bigStoneClustersShale` | BigStoneClusterConfig | see §3 | |
| `bigStoneClustersMyalite` | BigStoneClusterConfig (+ `generateInAir`) | see §3 | |
| `blocksToReplace` | string[] | `minecraft:stone, andesite, diorite, granite, netherrack\|minecraft:the_nether, end_stone\|minecraft:the_end, lepton:limestone, lepton:jasper, lepton:slate` | blocks clusters replace |
| `enableVanillaStoneClusters` | bool | false | VanillaStoneClusters module |
| `granite`, `diorite`, `andesite` | BigStoneClusterConfig | see §3 | |

JSON shapes:
```json
"newStoneTypesLimestone": {
  "dimensions": { "isBlacklist": false, "dimensions": ["minecraft:overworld"] },
  "oregenLower": { "minHeight": 0, "maxHeight": 60, "clusterSize": 64, "clusterCount": 2.0 },
  "oregenUpper": { "minHeight": 64, "maxHeight": 128, "clusterSize": 64, "clusterCount": 0.1666666 } },
"bigStoneClustersLimestone": {
  "enabled": true, "generateInAir": false,
  "dimensions": { "isBlacklist": false, "dimensions": ["minecraft:overworld"] },
  "biomes": { "tags": { "isBlacklist": false, "tags": ["c:is_swamp", "minecraft:is_ocean"] },
              "biomes": { "isBlacklist": true, "biomes": [] } },
  "rarity": 4, "minYLevel": 20, "maxYLevel": 80, "horizontalSize": 14, "verticalSize": 14,
  "horizontalVariation": 9, "verticalVariation": 9 },
"generationSettings": { "dimensions": {...}, "biomes": {...}, "rarity": 2, "minYLevel": 105,
  "maxYLevel": 140, "horizontalSize": 72, "verticalSize": 15, "horizontalVariation": 22,
  "verticalVariation": 4 }
```
(`generateInAir` is Quark's AirStoneClusterConfig field; it is read for every cluster config and is
`true` only for myalite by default.) Values are read when used, so `/quark reload` applies to chunks
generated afterwards.

## 6. Strings
Mod text (`assets/lang/<locale>/newstones.json`), Quark's hint wording, shown by `/quark newstones info`:

| Key | en_US |
|---|---|
| `quark.jei.hint.limestone` | Can be found in big clusters in swamps and oceans. |
| `quark.jei.hint.jasper` | Can be found in big clusters in mesas and deserts. |
| `quark.jei.hint.shale` | Can be found in big clusters in cold biomes. |
| `quark.jei.hint.myalite` | Can be found in big clusters in the outer end highlands. It reacts to an invisible field in the world, changing color depending on where it's placed with the field's intensity. |
| `quark.jei.hint.permafrost` | Can be found towards the top of Frozen Peaks. |
| `lp.newstones.info.header` | New stones ({}): |
| `lp.newstones.info.disabled` | This module is disabled. |
| `lp.newstones.sources.none` | No cluster sources within {} chunks. |
| `lp.newstones.sources.line` | {} at {} {} {} ({}) |
| `lp.newstones.players_only` | Only players can use this command. |
| `lp.newstones.admin_only` | You need operator permission for this command. |

Commands: `/quark newstones info` (everyone), `/quark newstones sources [radius]` (operators: the
cluster sources near the player, for testing). Block names come from `texts.d`.

## 7. Bedrock differences / not ported
- **Myalite tint** is quantised to 16 hue steps (a block state chooses a pre-tinted texture): Bedrock
  custom blocks have no per-position tint callback (`tint_method` only knows biome tints). The item
  shows a fixed middle hue instead of the colour of the looked-at block.
- **Terrain**: Bedrock terrain differs from Java's for the same seed, so cluster positions are
  deterministic per seed but not at Java's coordinates. The scheme (seeds, rarity, shapes) is Java's.
- **Generation order**: clusters run after the chunk's vanilla decoration (vanilla ores are kept,
  as in Java where ores come first); ore pockets also run then (Java: same step as vanilla ores).
  Overlap order is fixed (Java: random per session).
- **Quark bug not reproduced**: Zeta's `OrePocketConfig.forEach` overwrites a fractional
  `clusterCount` with 0 or 1 on its first use, so in Java the upper pocket either appears in every
  chunk or never for the rest of the session. Lepton rolls the chance per chunk as intended.
- **Mason trades**: Bedrock trade tables are not Java's pools; the replacement scheme in §3 keeps
  Java's odds per offer slot.
- **Hints** (JEI) are printed by `/quark newstones info` instead.
- **`quark:slate`** in `blocksToReplace` refers to a block that no longer exists (in Quark too); it is
  kept as `lepton:slate` and ignored.

## 8. Tests
Selftests (dx 300..399), `/quark selftest "newstones.*"`:
- `newstones.content` (300): every block registered, myalite family has 16 tints, gates table.
- `newstones.gates`: options switch blocks and recipes on/off (incl. other units' options).
- `newstones.noise`: Perlin simplex noise and the myalite colour are deterministic and in range.
- `newstones.myalite_tint` (310): placed myalite and shapes get the tint of their position.
- `newstones.cluster_shape`: shapes are deterministic, contain the source, stay within the radii.
- `newstones.biomes`: Bedrock -> Java biome names and tag filters.
- `newstones.cluster_place` (320): a cluster replaces only `blocksToReplace` blocks inside its shape.
- `newstones.air_cluster` (330): an air cluster fills only air.
- `newstones.permafrost_style` (340): floors/ceilings -> permafrost, walls -> packed ice.
- `newstones.ore_pocket` (350): pockets replace only stone.
- `newstones.trades`: mason journeyman offers.
- `newstones.recipes`: recipes load and disabled ones do not.
- `newstones.worldgen_live`: in a generated (DEFAULT) world, clusters exist around nearby sources
  (skipped on flat worlds).
- `newstones.gallery` (360..399): every block in rows, for the client screenshots.

E2E: `tools/e2e/server/scenarios/newstones.ps1` (selftests), `newstones-worldgen.ps1` (DEFAULT world),
`tools/e2e/client/scenarios/newstones.ps1` (gallery screenshots: every stone family, myalite tints).
