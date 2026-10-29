# U12 crystals: corundum, crystal lamps, beacon redirection and magnets

## 1. Modules covered

### Corundum (world)
Nine colours of corundum (red, orange, yellow, green, blue, indigo, violet, white, black): a glowing,
translucent crystal block, its waxed variant, a "sheet" (pane) and a cluster. Corundum generates
underground in large blobs ("underground styles") of two colours each (one on the floor, one on the
ceiling). Unwaxed corundum placed below y 24 with air above grows upwards (up to 4 high) and sprouts
clusters around the new block; it emits coloured dust particles. Honeycomb waxes it (no growth, no
particles), an axe scrapes the wax off. Clusters hang on the face they were placed on, break when
their support goes, and (`enableCollateralMovement`) move together with their corundum when a piston
moves it. Corundum, sheets and clusters tint beacon beams with their colour.

### RainbowLamps (building)
Nine crystal lamps, one per corundum colour, lit while powered by redstone (light level
`lightLevel`). With `useCorundum` and Corundum enabled they are "Corundum Lamps" crafted from
corundum + redstone; otherwise "Crystal Lamps" crafted from an amethyst block, dye, redstone and
glowstone. A lit lamp tints a beacon beam; an unlit one does not.

### BeaconRedirection (tools)
Corundum clusters (amethyst clusters when Corundum is disabled) placed in a beacon beam bend it in
the direction they face and mix their colour into it. Tinted glass in the beam dims it
(`allowTintedGlassTransparency`). The beam can travel at most `horizontalMoveLimit` blocks
horizontally per run; the last segment must reach the sky, otherwise the beacon does nothing.
Bedrock blocks (`#quark:beacon_transparent`) let the beam through.

### Magnets (oddities)
The magnet block: when powered with signal strength P its front (facing) pushes and its back pulls
magnetic blocks in a line up to P blocks away along its axis. A block is magnetic if it is crafted
(recursively) from iron or copper (`magneticDerivationList`), forced by `magneticWhitelist`, not in
`magneticBlacklist`, and (with `usePistonLogic`) pushable by a piston. The force on a block is
P - distance + 1; when several magnets act on a block the strongest net direction wins. Moving blocks
travel one block per two ticks as a "magnetized block". Hoppers drop/plant one item, stonecutters cut
the block above them when moved. Magnets also push/pull magnetic entities (items made of magnetic
material, iron golems, minecarts, entities in magnetic armour) with an inverse-square force.
Honeycomb waxes a magnet (no particles), an axe unwaxes it.

## 2. Blocks and items

`<c>` is one of `red orange yellow green blue indigo violet white black` (Quark's `CorundumColor`
order). Beacon colours: red `#ff0000`, orange `#ff8000`, yellow `#ffff00`, green `#00ff00`, blue
`#00ffff`, indigo `#0000ff`, violet `#ff00ff`, white `#ffffff`, black `#000000`. Map colours: red,
orange, yellow, green, light blue, blue, magenta, snow, black.

| Id | English name | States | Properties | Drops | Creative |
|---|---|---|---|---|---|
| `lepton:<c>_corundum` | `<C> Corundum` | - | hardness 0.3, blast 0, light 11, translucent (blend), amethyst sounds, pickaxe required for drops | itself | Construction, `itemGroup.name.lepton_corundum` |
| `lepton:waxed_<c>_corundum` | `Waxed <C> Corundum` | - | as above | itself | same group |
| `lepton:<c>_corundum_pane` | `<C> Corundum Sheet` | pane connections (`lepton:pane` contract) | as corundum, light 11 | itself with Silk Touch only | Construction |
| `lepton:<c>_corundum_cluster` | `<C> Corundum Cluster` | `minecraft:block_face` (the face it points to; default down) | hardness 0.3, light 11, cutout, amethyst cluster sounds, box 10x7 px, waterloggable | itself | Nature |
| `lepton:<c>_crystal_lamp` | `<C> Corundum Lamp` (Quark name with `useCorundum`) | `lepton:lit` (bool) | hardness 0.3, blast 0, light 15 when lit (`lightLevel`), translucent, amethyst sounds | itself | Construction, redstone group |
| `lepton:magnet` | `Magnet` | `minecraft:facing_direction` (the pushing face), `lepton:powered`, `lepton:waxed` | iron block copy: hardness 5, blast 6, pickaxe; light 3 when powered | itself | Construction (redstone) |
| `lepton:magnetized_block` | `Magnetized Block` | - | invisible placeholder while a block moves (no collision, unbreakable, no drop) | nothing | hidden |

Recipes (crafting table):
- `<c>_corundum_pane`: `###`/`###` of `<c>_corundum` -> 16.
- `waxed_<c>_corundum`: shapeless `<c>_corundum` + `minecraft:honeycomb`.
- `<c>_corundum_lamp` (`useCorundum` and Corundum enabled): ` R `/`RCR`/` R ` (C = `<c>_corundum`, R = redstone) -> `<c>_crystal_lamp`.
- `<c>_crystal_lamp` (otherwise): ` D `/`RAR`/` G ` (D = `<c>` dye, A = amethyst block, R = redstone,
  G = glowstone dust) -> `<c>_crystal_lamp`. Dyes: red/orange/yellow/lime(green)/light blue(blue)/blue(indigo)/magenta(violet)/white/black, as Quark's recipes.
- `magnet`: `CIC`/`BFR`/`CIC` (C cobblestone, I iron ingot, B blue dye, F chorus fruit, R redstone);
  `magnet_pre_end` (`usePreEndRecipe`): copper ingot instead of chorus fruit.

Tags: block tags `lepton:corundum` (the 9 unwaxed corundum, Quark `quark:corundum`), `lepton:crystal_lamp`,
`lepton:corundum_cluster`; item tag-equivalents for other units: the `lepton:corundum` block tag
(Bedrock has no data-driven item tags for native code; units read the block id list in
`crystals::corundumIds()`). `#quark:beacon_transparent` = `minecraft:bedrock`.

## 3. Mechanics

### Corundum growth (`caveCrystalGrowthChance` = 5)
Java random ticks hit a block with probability 3/4096 per tick (`randomTickSpeed` 3). Lepton keeps
the positions of unwaxed corundum (world DB key `lp:crystals:corundum`) and "random-ticks" each loaded
one with probability `3 * randomtickspeed / 4096` per tick (Bedrock's `randomtickspeed` default is 1,
so the default rate equals Java's). On a random tick, if growth chance >= 1, y < 24, the block above
is air and the column of the same corundum below is shorter than 4 (counting itself, `i < 4`), then
with 1/`caveCrystalGrowthChance`: the same corundum is placed above; if the block below is Myalite
Crystal and SpiralSpires' `renewableMyalite` is on, the grown-from block becomes Myalite Crystal
(U29's `lepton:myalite_crystal`, skipped if missing); otherwise each of the 6 neighbours of the new
block that is air gets a cluster of the colour facing away (1/3 each).

### Corundum particles (client effect)
Unwaxed corundum emits 4 dust particles of its colour within 2.5 blocks (size 0.4..0.9), 10 % with an
end-rod spark flying to the block; a growable one also emits an "entity effect" particle. Lepton
sends these from the server (custom particle `lepton:corundum_dust`, colour in molang variables) at
Java's expected animate-tick rate (a block within 16 blocks of a player gets ~1/48 animate ticks per
tick), to players within 32 blocks.

### Waxing
Using honeycomb on unwaxed corundum turns it into waxed corundum (honeycomb consumed outside creative,
`minecraft:block.copper.wax_on`-like sound + wax particles); using an axe on waxed corundum turns it
back (axe takes 1 damage). Same for the magnet's `lepton:waxed` state.

### Clusters
Placed facing the clicked face. A cluster survives only while the block behind it provides a full
face (`Block::canProvideFullSupport`); otherwise it breaks with drops (checked on neighbour changes).
Waterlogged when placed in water. **Collateral movement**: when a piston moves corundum, clusters of
the same colour attached to it (cluster at `corundum + facing`) are added to the piston's moved
blocks (`enableCollateralMovement`).

### Cave generation (`generationSettings`)
Port of Zeta's `ClusterBasedGenerator` + Quark's `UndergroundStyleGenerator` / `CorundumStyle`, run in
Bedrock's chunk post-processing (`OverworldGeneratorMultinoise::decorateWorldGenPostProcess`):
- Feature radius = horizontalSize + horizontalVariation (44); for every chunk within
  ceil(44/16) chunks, a `java.util.Random((xSeed*cx + zSeed*cz) ^ seed ^ "corundum".hashCode())`
  decides a source: 1 in `rarity` (400) chunks, at a random x/z in the chunk and y in
  [minYLevel, maxYLevel) (0..64), only if the biome at the top of the world passes `biomes`
  (default: not ocean) and the surface is above y.
- The blob is an ellipsoid with radii horizontalSize + rand(horizontalVariation) (x, z) and
  verticalSize + rand(verticalVariation) (y), its surface perturbed by a 2D Perlin simplex noise
  (octaves -4..4) as in Zeta's `ClusterShape`; only positions below the surface height count.
- Inside the blob: base-stone blocks (stone, granite, diorite, andesite, tuff, deepslate,
  netherrack, end stone) that are floors (air above) get a crystal with `crystalChance` (0.16)
  above them in the floor colour; ceilings (air below) get one below in the ceiling colour; walls
  (air beside) become stone. A crystal needs an occluding block on one of its 4 horizontal sides.
  It is a cluster with `crystalClusterChance` (0.2), else a corundum block with clusters on air
  sides (`crystalClusterOnSidesChance`, 0.6); with `doubleCrystalChance` (0.2) a second
  crystal (never a cluster) stacks on it.
- Colours: `Random(source.hashCode())`: floor = nextInt(9), ceiling = nextInt(8) (+1 if >= floor).

### Rainbow lamps
Each lamp is a redstone consumer (`minecraft:redstone_consumer`); Lepton reads its circuit strength
every tick. Unpowered -> powered lights it at once; powered -> unpowered turns it off 4 ticks later
(if still unpowered), like Java. Placement takes the current power. `lightLevel` is applied to the
lit permutations at world load (server lighting; remote clients use the pack's 15).

### Beacon redirection (`horizontalMoveLimit` 64, `allowTintedGlassTransparency` true)
Every beacon refresh (Bedrock's beacon tick) Lepton walks Quark's beam from the beacon: up while
blocks are transparent; a redirecting cluster (`<c>_corundum_cluster`, or `minecraft:amethyst_cluster`
when Corundum is disabled) facing another direction starts a new segment in its facing, mixing the
colour 1:3 with the cluster's colour (amethyst: Quark's constant 0x1000000, i.e. the colour /4);
corundum, sheets and lit lamps (`getBeaconColorMultiplier`) and stained glass start a new segment
with the colour averaged 1:1 (the first coloured block sets the colour outright); tinted glass sets
alpha to 0 if alpha < 77 else 2/3 alpha. Horizontal moves count down from `horizontalMoveLimit`
(reset by vertical moves); visiting a position twice after a colour change, leaving the world,
reaching the limit or hitting an opaque block (light block >= 15, except bedrock) breaks the beam.
A beam that ends with an upward segment reaching the heightmap succeeds.
- Effects: a broken beam disables the beacon (levels are set to 0); a successful redirected beam
  makes it work even if the column above the beacon is blocked (Lepton applies the effects).
- Rendering: when the beam differs from vanilla's straight beam (redirected, tinted by Quark
  blocks or dimmed), Lepton draws it with `lepton:beacon_beam` helper entities (one per segment
  piece, rotated and coloured through entity properties, emissive rotating beam + glow); every client
  sees them. The client mod hides the vanilla beam of those beacons; vanilla clients also see their
  own straight beam.

### Magnets
- Magnet state: `lepton:powered` follows the circuit strength (> 0); facing = the face towards the
  placer's look direction reversed (Java `getNearestLookingDirection().getOpposite()`).
- Each tick, each powered magnet with power P scans along facing (pushing) and opposite (pulling),
  distance 1..P: for each block, the push action (`getPushAction`) is MOVE when it can be moved and the
  block in front (in the move direction) is air, BREAK when the front block is destroyed by pistons,
  SKIP otherwise; collateral movers (U10's iron rod, tag `lepton:collateral_mover`) give PREVENT when
  the block beyond can't be pushed. A MOVE/BREAK registers a force P - d + 1 in the move direction;
  PREVENT stops the scan; non-air blocks with collision stop the flux.
- At the end of the tick each block's forces are summed, the dominant direction is chosen and every
  magnet whose force points that way moves the block one step (BREAK destroys the front block with
  drops first). The block is replaced by `lepton:magnetized_block` at the destination for 2 ticks,
  then placed (block-entity data, e.g. hopper contents, is kept) with its placing sound
  (volume (v+1)*0.05, pitch p*0.8), then hoppers drop/plant one item and stonecutters cut the block
  above (hardness < `stoneCutterMaxHardness`, silk touch with `stoneCutterSilkTouch`).
- Entities (`affectEntities`): magnetic items, iron golems, minecarts, falling magnetic blocks and
  (`affectsArmor`) mobs wearing magnetic armour within the flux line get a velocity
  `dir * magnitude * entitiesPullForce / distance²` (magnitude +1 push, -1 pull); fall distance reset.
- Particles (not waxed): repulsor/attractor particles along the flux, 20 % per block per tick.
- Magnetism: at world load Lepton walks every crafting recipe: an item is magnetic if it is in
  `magneticDerivationList`, in `magneticWhitelist`, or crafted from a magnetic item (recursively),
  unless in `magneticBlacklist`. Blocks are magnetic when their item is; hoppers and stonecutters
  always are (move actions). `#minecraft:cauldrons` = cauldron; hanging signs = every `*_hanging_sign`.

## 4. Screens
None. (Quark's JEI hints are not ported, see SPEC §8.)

## 5. Config
Section `crystals` (`config/crystals.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableCorundum` | bool | true | Corundum module |
| `generationSettings` | object | see below | Corundum cave settings |
| `crystalChance` | double | 0.16 | |
| `crystalClusterChance` | double | 0.2 | |
| `crystalClusterOnSidesChance` | double | 0.6 | |
| `doubleCrystalChance` | double | 0.2 | |
| `caveCrystalGrowthChance` | int | 5 | 1 in X random ticks grows; 0 disables growth |
| `crystalsCraftRunes` | bool | true | Quark's flag; no recipe in 4.1-488 reads it (kept for ColorRunes, U18) |
| `enableCollateralMovement` | bool | true | clusters move with their corundum |
| `enableRainbowLamps` | bool | true | RainbowLamps module |
| `lightLevel` | int | 15 | light of lit lamps |
| `useCorundum` | bool | true | corundum recipes/names for the lamps |
| `enableBeaconRedirection` | bool | true | |
| `horizontalMoveLimit` | int | 64 | |
| `allowTintedGlassTransparency` | bool | true | |
| `enableMagnets` | bool | true | |
| `magneticDerivationList` | string[] | Quark's list (iron/copper ingots, oxidised copper, raw metals, ores, `quark:gravisand` as `lepton:gravisand`) | |
| `magneticWhitelist` | string[] | Quark's list | |
| `magneticBlacklist` | string[] | Quark's list | |
| `usePreEndRecipe` | bool | false | |
| `usePistonLogic` | bool | true | |
| `affectEntities` | bool | true | |
| `affectsArmor` | bool | true | |
| `entitiesPullForce` | double | 0.18 | |
| `stoneCutterSilkTouch` | bool | true | |
| `stoneCutterMaxHardness` | double | 3 | |

`generationSettings`:
```json
{ "dimensions": { "isBlacklist": false, "dimensions": ["minecraft:overworld"] },
  "biomes": { "tags": { "isBlacklist": true, "tags": ["minecraft:is_ocean"] },
              "biomes": { "isBlacklist": true, "biomes": [] } },
  "rarity": 400, "minYLevel": 0, "maxYLevel": 64, "horizontalSize": 36, "verticalSize": 14,
  "horizontalVariation": 8, "verticalVariation": 6 }
```
Biome tags map to Bedrock biome names (`minecraft:is_ocean` -> names containing `ocean`, also
`is_deep_ocean`, `is_beach`, `is_river`, `is_mountain`, `is_badlands`, `is_jungle`, `is_taiga`,
`is_forest`, `is_savanna`, `is_hill`); biome names are Bedrock ids with or without `minecraft:`.

Disabled modules: their recipes are removed from the world's recipe list at world load; magnets,
lamps and corundum stop reacting; generation and growth stop.

## 6. Strings
Mod text (`assets/lang/<locale>/crystals.json`), Quark's hint wording (shown by `/quark crystals info`):

| Key | en_US |
|---|---|
| `quark.jei.hint.corundum` | Can be found underground in large clusters of two colors each. Will slowly grow upwards if placed deep underground. Can be waxed to prevent growth and particle emission. |
| `quark.jei.hint.crystal_lamp` | These lamps will only change the color of a beacon beam when turned on. |
| `quark.jei.hint.magnet` | Can push and pull any blocks made of Copper or Iron when powered, ... (full Quark text) |
| `quark.jei.hint.beacon_redirection` | The beacon beam can be redirected with %s by placing it in the beam's path. ... |
| `quark.jei.hint.beacon_amethyst` / `beacon_corundum` | Amethyst / Corundum |
| `quark.jei.hint.corundum_cluster_grow` | Grows alongside the respective Corundum, either naturally spawned or when left to grow. |
| `quark.jei.hint.corundum_cluster_redirect` | Can redirect Beacon beams, check the Beacon item for info. |
| `quark.jei.hint.minecraft.tinted_glass` | Can be used to make Beacon beams dimmer by placing the block in the way. Multiple will stack. |
| `lp.crystals.redirect` | Beacon at {} redirected ({} segments) |

## 7. Bedrock differences / not ported
- **Random ticks**: data-driven blocks cannot random-tick natively; Lepton emulates them for tracked
  corundum at Java's rate (see 3).
- **Particles** are sent by the server (custom particle effects) instead of client animate ticks.
- **Magnetized block rendering**: Java renders the moving block sliding over 2 ticks; Lepton shows
  nothing at the destination for those 2 ticks (invisible placeholder), then the block. Entities
  standing on a moving block are carried by one block when it lands, not continuously.
- **Lamp names** follow the default config (`Corundum Lamp`); with `useCorundum: false` the recipes
  switch, the item names stay (names live in the resource pack).
- **Beam rendering** uses helper entities (render distance of entities, not of block entities);
  vanilla clients still see the vanilla straight beam in addition. Beam colours mix in sRGB like Java.
- **Advancements** (`redirect_beacon`, `get_all_corundum`) are not ported (SPEC §8); JEI hints are
  available as text through `/quark crystals info`.
- `crystalsCraftRunes` has no effect in Quark 4.1-488 either.

## 8. Tests
Selftests (dx 1200..1299): `crystals.corundum_blocks`, `crystals.waxing`, `crystals.growth`,
`crystals.cluster_support`, `crystals.collateral`, `crystals.worldgen_shape`, `crystals.lamp_power`,
`crystals.beam_path`, `crystals.beam_entities`, `crystals.magnet_push`, `crystals.magnet_pull`,
`crystals.magnet_conflict`, `crystals.magnetism_crawl`, `crystals.magnet_hopper`, `crystals.magnet_entity`.
E2E: `tools/e2e/server/scenarios/crystals.ps1` (all selftests + log lines);
`tools/e2e/client/scenarios/crystals.ps1` (a gallery of every block, lit/unlit lamps, clusters on all
faces, a redirected coloured beacon beam, a magnet; screenshots `crystals-*.png`).
