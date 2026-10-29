# U7 newwoods: azalea, trumpet (blossom) and ashen (ancient) woods

Ports three Quark 4.1-488 world modules: **AzaleaWood**, **AncientWood** ("Ashen Trees") and
**BlossomTrees** ("Trumpet Trees"). Each adds a full Quark wood set (Quark's `WoodSetHandler`); the
two tree modules add leaves, saplings and trees, and BlossomTrees generates its trees in the world.

The variants Quark gives every wood set (vertical planks, bookshelf, ladder, posts, hollow log,
hedges, leaf carpets) are generated with U6's generators (`tools/pack/woodvariants.ps1`) and carry
U6's block tags (`docs/spec/woodvariants.md` §2.5), so their behaviour (ladder climbing, post
connections, hedge connections, hollow log crawling, bookshelf enchanting power, ...) is U6's code;
they are disabled by U6's module keys (`enableWoodenPosts`, ...) **and** by this unit's module key.

## 1. Modules covered

### AzaleaWood
A wood set "Azalea" (`MapColor.COLOR_LIGHT_GREEN` planks, brown bark). With `replaceAzaleaTreeLog`
the vanilla azalea tree (grown from an azalea / flowering azalea bush with bone meal, and the azalea
trees generated above lush caves) gets azalea logs instead of oak logs; that is the only way to get
the wood. Quark also changes the azalea leaf hedges (U6's blocks) to be crafted from azalea logs.

### AncientWood (Ashen Trees)
A wood set "Ashen" (`ancient`, terracotta-white), **Ashen Leaves**, the **Ashen Sapling** and the
**Enchanted Fruit** (`ancient_fruit`). The sapling is found by sniffers (an extra entry in the sniffer
digging loot) and optionally in ancient city chests. It grows a tall straight tree with three
layered leaf discs and a leaf cap. The leaves drop saplings, sticks and (rarely) the fruit, which
restores hunger and gives experience when eaten.

### BlossomTrees (Trumpet Trees)
A wood set "Trumpet" (`blossom`) and five coloured trees: Frosty (blue), Serene (lavender), Warm
(orange), Sunny (yellow) and Fiery (red) Trumpet leaves and saplings. The saplings grow "fancy" (big
oak shaped) trees of trumpet logs, and the trees generate naturally, one colour per biome group
(snowy, swamp, savanna, plains, badlands). Leaves above air shed falling leaf particles.

## 2. Blocks and items

`<w>` is the wood: `azalea`, `blossom`, `ancient`. English names: Azalea / Trumpet / Ashen.
`<c>` is a blossom colour: `blue`, `lavender`, `orange`, `yellow`, `red`.

### 2.1 Wood sets (all three modules)

| Id | English name (azalea) | Notes |
|---|---|---|
| `lepton:<w>_log`, `lepton:stripped_<w>_log` | Azalea Log, Stripped Azalea Log | pillar (`minecraft:block_face`), axe strips (`lepton:strippable`) |
| `lepton:<w>_wood`, `lepton:stripped_<w>_wood` | Azalea Wood, Stripped Azalea Wood | pillar |
| `lepton:<w>_planks` | Azalea Planks | |
| `lepton:<w>_planks_stairs`, `lepton:<w>_planks_slab`, `lepton:<w>_planks_vertical_slab` | Azalea Stairs, Azalea Slab, Azalea Vertical Slab | Quark ids (`*_planks_*`) |
| `lepton:<w>_fence`, `lepton:<w>_fence_gate` | Azalea Fence, Azalea Fence Gate | |
| `lepton:<w>_door`, `lepton:<w>_trapdoor` | Azalea Door, Azalea Trapdoor | door item `lepton:<w>_door` |
| `lepton:<w>_button`, `lepton:<w>_pressure_plate` | Azalea Button, Azalea Pressure Plate | |
| `lepton:vertical_<w>_planks` | Vertical Azalea Planks | U6 VerticalPlanks |
| `lepton:<w>_bookshelf` | Azalea Bookshelf | U6 VariantBookshelves (3 books / silk touch) |
| `lepton:<w>_ladder` | Azalea Ladder | U6 VariantLadders |
| `lepton:<w>_post`, `lepton:stripped_<w>_post` | Azalea Post, Stripped Azalea Post | U6 WoodenPosts |
| `lepton:hollow_<w>_log` | Hollow Azalea Log | U6 HollowLogs |

Properties (Quark `WoodSetHandler`): logs/wood 2 / 2, planks and shapes 2 / 3, doors and trapdoors 3,
buttons and plates 0.5; sound wood; flammable; axe. Names in zh_CN from Quark's `zh_cn.json`.

Recipes (crafting table, Quark/vanilla counts): planks ×4 from any log/wood of the wood (shapeless);
wood ×3 from 2×2 logs; stripped wood ×3 from 2×2 stripped logs; stairs ×4, slab ×6, vertical slab ×3
(3 slabs in a column) and slab from 1 vertical slab; fence ×3 (`#S#`), fence gate ×1 (`S#S`);
door ×3, trapdoor ×2, button ×1, pressure plate ×1; vertical planks ×3 (3 planks in a column) and back
to planks; bookshelf (`###` `XXX` `###`); ladder ×4 (`# #` `#W#` `# #`, # stick, W planks); post ×8
from 3 wood (stripped post from stripped wood); hollow log ×4 (` L ` `L L` ` L `).

Item tags (Java `#minecraft:logs`, `#planks`, `#wooden_slabs`): the items of the logs / wood /
stripped logs / stripped wood carry `minecraft:logs` and `minecraft:logs_that_burn`, the planks and
vertical planks `minecraft:planks`, the slabs `minecraft:wooden_slabs` (an item with
`minecraft:block_placer` + `replace_block_item` replaces the block's own item), so the vanilla
recipes taking any planks / logs / wooden slabs (sticks, crafting table, chests, tools, ...) accept them.

Fuel (Java `#logs`, `#planks`, wooden blocks): logs/wood/planks/stairs/fences/gates/trapdoors/
pressure plates/bookshelves/vertical planks/posts 300 ticks, slabs and vertical slabs 150, doors 200,
buttons 100, saplings 100 (ladders: U6).

### 2.2 Leaves, saplings, hedges, carpets, fruit

| Module | Id | English name | States |
|---|---|---|---|
| AncientWood | `lepton:ancient_leaves` | Ashen Leaves | `lepton:persistent` (bool, default true) |
| AncientWood | `lepton:ancient_sapling` | Ashen Sapling | `lepton:stage` (0, 1) |
| AncientWood | `lepton:ancient_hedge` | Ashen Leaf Hedge | U6 hedge states |
| AncientWood | `lepton:ancient_leaf_carpet` | Ashen Leaf Carpet | - |
| AncientWood | `lepton:ancient_fruit` (item) | Enchanted Fruit | - |
| BlossomTrees | `lepton:<c>_blossom_leaves` | Frosty / Serene / Warm / Sunny / Fiery Trumpet Leaves | `lepton:persistent` |
| BlossomTrees | `lepton:<c>_blossom_sapling` | Frosty ... Trumpet Sapling | `lepton:stage` |
| BlossomTrees | `lepton:<c>_blossom_hedge` | Frosty ... Trumpet Leaf Hedge | U6 hedge states |
| BlossomTrees | `lepton:<c>_blossom_leaf_carpet` | Frosty ... Trumpet Leaf Carpet | - |

- **Leaves**: hardness 0.2, hoe/shears, flammable (Java: 30/60), light dampening 1, `alpha_test`,
  not tinted (Quark's trumpet and ashen leaf textures are coloured). Player-placed leaves are
  persistent (the default state); trees place them with `lepton:persistent` false.
- **Saplings**: no collision, selection box 2..14 × 0..12, break instantly, can be placed only on
  dirt-like blocks (grass block, dirt, coarse dirt, podzol, rooted dirt, moss block, mud, muddy
  mangrove roots, farmland: Java `#minecraft:dirt` + farmland) and break when that block goes; can
  be potted (below). Item: flat sapling icon.
- **Potted saplings** `lepton:potted_<tree>_sapling` (Quark's ids; Potted Ashen Sapling, Potted Frosty
  Trumpet Sapling, ...): using a sapling on an empty vanilla flower pot turns the pot into the potted
  sapling (one sapling used, not in creative); using the potted sapling with anything but a sapling
  gives the sapling back and puts the empty pot back; breaking it drops the flower pot and the sapling.
  Model: the vanilla pot's shape with dirt and the sapling's cross. Not in the creative inventory.
- **Hedges / leaf carpets**: U6's generators (`L` over `W` → 2 hedges, W any log/wood of the wood;
  `##` leaves → 3 carpets).
- **Enchanted Fruit**: food 4 (saturation modifier 0.3), always edible.

Drops (Java loot tables; fortune levels 0..4 / 0..3):

| Block | Drop |
|---|---|
| ashen leaves | shears or silk touch: itself; else sapling 5% / 6.25% / 8.33% / 10% (survives explosion), sticks 1-2 at 2% / 2.22% / 2.5% / 3.33% / 10%, Enchanted Fruit 0.5% / 0.56% / 0.625% / 0.83% / 2.5% |
| trumpet leaves | shears or silk touch: itself; else sapling 0.469% / 0.521% / 0.586% / 0.781% / 1.875%, sticks as above |
| sapling | itself |

Compost chance: leaves 30%, saplings 30%, fruit 65%.

Creative inventory: logs in nature (log group), everything else of the wood set in construction next
to the vanilla wood; leaves/saplings in nature; fruit in items (food).

### 2.3 Block tags

| Tag | Blocks | Meaning |
|---|---|---|
| `lepton:nw_azalea_wood`, `lepton:nw_ancient_wood`, `lepton:nw_blossom_trees` | every block of the module | config gate (disabled module: no recipes, placement cancelled) |
| `lepton:nw_leaves` | the six leaves | decay, drops, compost |
| `lepton:nw_sapling` | the six saplings | growth, bone meal |
| `log` | logs and wood (lib) | leaves within 6 blocks stay |

## 3. Mechanics

### 3.1 Leaf decay (Java `LeavesBlock`)
A leaf is supported when a log (any block with the `log` tag or a vanilla `*_log/_wood/_stem/_hyphae`)
is reachable from it in at most 6 steps through leaves (any leaves, Java `distance` < 7).
When a log or leaf next to our leaves is removed, the leaves around it are re-checked (the check
spreads through unsupported leaves like Java's distance update). Unsupported non-persistent leaves
get a block tick after a random delay with the mean of a Java random tick (1..2730 ticks at the
default random tick speed); then, if still unsupported, they break and drop their loot (no tool).

### 3.2 Leaf drops
`BlockType::spawnResources` is hooked for `lepton:nw_leaves`: Java's tables above, with Bedrock's
mining context (`isUsingSilkTouch`, `getMiningLootBonusLevel`, the used item for shears, the
explosion radius for "survives explosion": chance 1/radius).

### 3.3 Saplings (Java `SaplingBlock`)
- Random tick (block ticks with a random delay as above): if the light level above is at least 9,
  1 in 7: stage 0 → 1, stage 1 → grow the tree.
- Bone meal (player and dispenser, `FertilizerItem` hooks): consumed (not in creative), green
  particles; 45% chance to advance (stage 0 → 1 or grow).
- Growth removes the sapling and places the tree; if the tree doesn't fit, the sapling is put back.

### 3.4 Trees
Shared rules (Java `TreeFeature`): trunk and leaves only replace air, leaves, and replaceable plants
(`validTreePos`); the block under the trunk becomes dirt when it is grass or mycelium; the free
height above the root (logs count as free) must be at least the trunk height, or at least 4 (then
the trunk is clipped); leaves are placed non-persistent.

- **Ashen tree** (`ancient_tree`): straight trunk of `17 + rand(0..4) + rand(0..6)` (+1, Quark's
  `MultiFoliageStraightTrunkPlacer` places `height + 1` logs) ashen logs; leaf blobs at the top log
  and every 6 logs down, up to 3; each blob is Java's fancy blob 3 rows deep starting 3 below its
  log (radius r, r+1, r; r = rand(2..4) per tree; a cell is skipped when `(|dx|+0.5)² + (|dz|+0.5)² > radius²`).
  Then the top decorator puts ashen leaves on the free cells above and on the four sides of the
  highest log.
- **Trumpet tree** (`<c>_blossom`): Java's fancy trunk placer (big oak: height `8 + rand(0..10) +
  rand(0..10)`, branches from `treeShape`, limbs along the dominant axis) with trumpet logs and
  fancy foliage of the colour (radius 3, offset 1, height 4: rows +1 r3, 0..-2 r4, -3 r3 around each
  branch end).

### 3.5 Trumpet tree world generation (Quark `BlossomTreeGenerator`, TOP_LAYER_MODIFICATION)
Per generated overworld chunk and per colour (in the order blue, lavender, orange, yellow, red), with
a per-chunk random seeded from the world seed, the chunk and the colour (deterministic): pick a
column at a random x/z in the chunk; if the biome there passes the colour's biome filter and
`rand(rarity) == 0`, take the top block (motion-blocking heightmap); if it is dirt-like (sapling
soil / dirt / farmland), clear a replaceable block above it and place the tree there. Runs after
the game's own decoration (`OverworldGeneratorMultinoise::decorateWorldGenPostProcess`).

Biome filters (Quark's biome tags mapped to Bedrock biome names):

| Colour | Rarity | Java tag | Bedrock biomes |
|---|---|---|---|
| blue | 200 | `c:is_snowy` | ice_plains, ice_plains_spikes, cold_taiga (+ hills/mutated), cold_beach, grove, snowy_slopes, jagged_peaks, frozen_peaks |
| lavender | 100 | `c:is_swamp` | swampland (+ mutated), mangrove_swamp |
| orange | 100 | `minecraft:is_savanna` | savanna, savanna_plateau, savanna_mutated, savanna_plateau_mutated |
| yellow | 200 | `c:is_plains` | plains, sunflower_plains |
| red | 30 | `minecraft:is_badlands` | mesa, mesa_bryce, mesa_plateau, mesa_plateau_stone (+ mutated) |

### 3.6 Azalea trees (`replaceAzaleaTreeLog`)
With AzaleaWood and `replaceAzaleaTreeLog` on: `AzaleaBlock::onFertilized` is hooked; the oak logs
that the growth created are replaced by azalea logs (same axis). Generated azalea trees (above lush
caves): after a chunk is decorated, every oak log trunk standing on rooted dirt with azalea leaves
around it is turned into azalea logs.

### 3.7 Enchanted Fruit
`Player::eat` is hooked: eating `lepton:ancient_fruit` with `ancientFruitGivesExp` gives
`ancientFruitExpValue` experience points and plays the experience orb sound.

### 3.8 Loot
`LootTable::deserialize` is hooked (tables load when a world loads):
- `loot_tables/gameplay/entities/sniffer_seeds.json` (sniffer digging): with AncientWood and
  `sniffingLootWeight` > 0, an entry `lepton:ancient_sapling` (weight `sniffingLootWeight`, quality
  `sniffingLootQuality`) is added to the first pool.
- `loot_tables/chests/ancient_city.json`: with `ancientCityLootWeight` > 0, the same with
  `ancientCityLootWeight` / `ancientCityLootQuality`.

### 3.9 Falling leaves (`dropLeafParticles`)
Java `BlossomLeavesBlock.animateTick`: each tick, for every player, 667 random blocks within 16 and
667 within 32 blocks are sampled (Java `ClientLevel.animateTick`); a trumpet leaf with air below
spawns, 1 in 5, a leaf particle of its colour at its centre with velocity (windX, -1, windZ):
`strength = 5 + cos(time / 2000) * 2`, `windX = cos(time / 1200) * strength`,
`windZ = sin(time / 1000) * strength`. The server sends the particle (`lepton:<c>_blossom_leaf`) to
that player only, so vanilla clients see it too; the particle computes the wind itself from the world
time (`(query.day + query.time_of_day) * 24000`), and Java's particle motion (random spread, speed,
+0.1 upward, gravity 0.04 / tick, friction 0.98, lifetime 4..40 ticks, a 4x4 px piece of the leaves
texture tinted 0.6).

### 3.10 Disabled modules
- Recipes of a disabled module are not loaded (`Recipes::_loadRecipe` hook, at world load).
- Placing a block of a disabled module is cancelled; its trees don't grow, don't generate, and the
  loot additions are not made.

### 3.11 Fuel and composting
`FurnaceBlockActor::getBurnDuration` maps our wooden blocks to the vanilla equivalent's burn time;
`ComposterBlock::getFillChance` returns the chances of §2.2.

## 4. Screens

None.

## 5. Config

Section `newwoods` (`config/newwoods.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableAzaleaWood` | bool | true | AzaleaWood module |
| `replaceAzaleaTreeLog` | bool | true | azalea trees grow azalea logs (else the wood is unobtainable) |
| `enableAncientWood` | bool | true | AncientWood module |
| `ancientFruitGivesExp` | bool | true | |
| `ancientFruitExpValue` | int | 10 | experience points (min 1) |
| `ancientCityLootWeight` | int | 0 | ancient city chest entry weight (0 = off; 8 recommended) |
| `ancientCityLootQuality` | int | 1 | |
| `sniffingLootWeight` | int | 1 | sniffer entry weight (0 = off) |
| `sniffingLootQuality` | int | 0 | |
| `enableBlossomTrees` | bool | true | BlossomTrees module |
| `blue`, `lavender`, `orange`, `yellow`, `red` | object | see below | per colour generation |
| `dropLeafParticles` | bool | true | |

Blossom tree object (Quark `BlossomTreeConfig`: `DimensionConfig` + `CompoundBiomeConfig` + rarity):

```json
"blue": {
  "dimensions": { "isBlacklist": false, "dimensions": ["minecraft:overworld"] },
  "biomes": {
    "tags":   { "isBlacklist": false, "tags": ["c:is_snowy"] },
    "biomes": { "isBlacklist": true,  "biomes": [] }
  },
  "rarity": 200
}
```

A biome passes when both the tag part and the name part pass (Quark `CompoundBiomeConfig`). Tags are
the Java names of §3.5 (`c:is_snowy`, `c:is_swamp`, `minecraft:is_savanna`, `c:is_plains`,
`minecraft:is_badlands`, and also `c:is_forest`, `c:is_taiga`, `c:is_jungle`, `c:is_desert`,
`c:is_beach`, `c:is_ocean`, `c:is_river`, `c:is_mountain`); `biomes` holds Bedrock biome names.

## 6. Strings

| Key | en_US |
|---|---|
| `quark.jei.hint.ancient_fruit` | Falls from Ashen Trees when they're chopped down. Gives you a small amount of XP when eaten. |
| `quark.jei.hint.ancient_sapling` | Can be sometimes sniffed by a Sniffer. |
| `quark.jei.hint.blue_blossom_sapling` | Can be found in cold biomes. |
| `quark.jei.hint.lavender_blossom_sapling` | Can be found in swamps. |
| `quark.jei.hint.orange_blossom_sapling` | Can be found in savannas. |
| `quark.jei.hint.red_blossom_sapling` | Can be found in badlands. |
| `quark.jei.hint.yellow_blossom_sapling` | Can be found in plains. |

The client mod shows the hints in the item tooltips (Quark shows them in JEI).

## 7. Bedrock differences / not ported

- **Signs and hanging signs** (`<w>_sign`, `<w>_hanging_sign`): need a sign block entity; custom
  blocks can't have one (`tools/pack/README.md`). Not ported.
- **Boats and chest boats** (`<w>_boat`, `<w>_chest_boat`): Bedrock boats are a vanilla entity with a
  fixed set of wood variants rendered by the game; not ported.
- **Chests and trapped chests** (`<w>_chest`, `trapped_<w>_chest`): VariantChests is U9's
  behaviour (block tag contract); the chests of these woods are added once U9's generator exists.
- **Potted saplings** are custom blocks the vanilla pot is swapped for (§2.2), not the vanilla pot
  holding the sapling: `minecraft:flower_pottable` makes the 1.26.51 client crash while it builds the
  mesh of a pot holding a custom block.
- **Cross models** turn their planes by bones: a cube with its own `rotation` makes the 1.26.51 client
  drop every custom block of that sub-chunk from its mesh.
- **Wind of the falling leaves** follows the world (day) time instead of the game time; they are the
  same unless the time was changed.
- **Leaf decay** is computed when logs/leaves change instead of from a stored `distance` state;
  decay timing has Java's mean, uniform instead of geometric.
- **Biome tags** are mapped to Bedrock biome names (table in §3.5); Forge/NeoForge tags without a
  counterpart match by name.
- **Falling leaf particles** are sent by the server (Java draws them on the client).
- **Advancements** ("Blossoming", "Bit Fruity", balanced diet): Bedrock has no custom advancements.
- **Sniffer loot weight**: Quark 4.1-488 always adds the sapling with weight 1 (its code ignores
  `sniffingLootWeight`); Lepton honours the option as its description says (default 1 = same).
- The Ancient City loot is added to the table's first pool.

## 8. Tests

Selftests (`newwoods.*`, dx 700..799):

| Test | dx | Checks |
|---|---|---|
The tree tests grow their trees far out in z (dz 120..360) so the canopies stay clear of the other
units' test rows.

| Test | dx | Checks |
|---|---|---|
| `newwoods.blocks` | - | every block and item id is registered, module mapping, block classes, palette |
| `newwoods.item_tags` | - | planks / logs / slabs carry Java's item tags |
| `newwoods.recipes` | - | recipe gating by module (and the azalea hedge recipes need U6's hedge) |
| `newwoods.strip` | 700 | an axe strips azalea logs keeping the axis |
| `newwoods.leaf_drops` | - | the loot rules (shears, silk touch, fortune, explosion) by sampling |
| `newwoods.leaf_decay` | 704 | leaves next to a removed log decay; persistent ones stay; distance 6 / 7 |
| `newwoods.sapling_place` | 712 | saplings only on dirt, pop off without it; potting into a flower pot and taking the sapling out |
| `newwoods.sapling_bonemeal` | 716 | the player's bone meal advances the sapling; it grows a trumpet tree, dirt below |
| `newwoods.ancient_tree` | 730 | the ashen tree shape (trunk 18..28, blobs, cap, non-persistent leaves) |
| `newwoods.blossom_tree` | 750 | a trumpet tree of every colour grows from its sapling |
| `newwoods.tree_blocked` | 770 | a blocked sapling stays a sapling |
| `newwoods.azalea_tree` | 780 | bone meal on an azalea grows azalea logs, no oak logs |
| `newwoods.azalea_gen` | 784 | a generated azalea tree (rooted dirt, oak trunk, azalea leaves) gets azalea logs |
| `newwoods.worldgen_live` | - | (generated worlds only) trumpet trees stand at the rolled columns near the spawn |
| `newwoods.fruit_xp` | - | eating the fruit gives the configured XP |
| `newwoods.loot_tables` | - | loot additions by config; the game's sniffer table has 3 entries |
| `newwoods.worldgen_filters` | - | biome filters, dimensions, per-chunk determinism and rarity |
| `newwoods.fuel_compost` | - | burn times and compost chances (through the game's functions) |
| `newwoods.gallery` | 790 (+rows) | builds every block and the grown trees; with a real player, shows each part |

E2E: `tools/e2e/server/scenarios/newwoods.ps1` runs `newwoods.*`; `newwoods-worldgen.ps1` runs in a
generated (DEFAULT) world and checks that trumpet trees generate. The client scenario
`tools/e2e/client/scenarios/newwoods.ps1` runs the gallery and screenshots the three wood sets, the
tree blocks (leaves, saplings, hedges, carpets) and the grown trees with their
falling leaves.
