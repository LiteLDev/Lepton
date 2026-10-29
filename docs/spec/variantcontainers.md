# U9 variantcontainers: chests of every wood and stone, deepslate and blackstone furnaces

Quark 4.1-488 modules `building/VariantChests` and `building/VariantFurnaces`. Selftest dx range
900..999.

## 1. Modules covered

### VariantChests

Adds a chest and a trapped chest for every vanilla wood (oak, spruce, birch, jungle, acacia,
dark oak, mangrove, bamboo, cherry, crimson, warped) and for nether bricks, purpur and prismarine.
They behave exactly like the vanilla chest and trapped chest: 27 slots, double chests when two
equal chests stand side by side, lid animation and sounds, hopper / dropper / comparator
interaction, trapped chests emit redstone power while open. Eight planks of one kind make that
kind's chest (planks of mixed kinds still make a vanilla chest), a variant chest crafts back into
a vanilla chest (`enableRevertingWoodenChests`), and chests that generate in structures use the
structure's material (`replaceWorldgenChests`: oak in plains villages and strongholds, birch in
desert pyramids and villages, spruce in igloos and snowy / taiga villages, ...). A wooden variant
chest can be put on a donkey, mule or llama and drops back as itself ("donk chest").

Woods that other units add (U7: azalea, blossom, ancient) add their chests with the block tags
below; the behaviour here applies to every block with the tags.

### VariantFurnaces

Adds the Deepslate Furnace (from cobbled deepslate) and the Blackstone Furnace (from blackstone).
They are vanilla furnaces in every respect (smelting, fuel, XP, recipe book screen, hoppers,
comparators, light level 13 when lit). The Blackstone Furnace burns with soul fire (blue flame
particles and front texture) when the block below it is a soul fire base block (soul sand, soul
soil). Each variant furnace also crafts into a blast furnace, a smoker and a furnace minecart.

## 2. Blocks and items

### 2.1 VariantChests

| Material | Chest id | Trapped chest id | English names (Quark) | Planks / base item | Sound | Hardness / resistance | Tool |
|---|---|---|---|---|---|---|---|
| oak | `lepton:oak_chest` | `lepton:trapped_oak_chest` | Oak Chest / Oak Trapped Chest | `minecraft:oak_planks` | wood | 2.5 / 2.5 | axe |
| spruce | `lepton:spruce_chest` | `lepton:trapped_spruce_chest` | Spruce Chest / Spruce Trapped Chest | `minecraft:spruce_planks` | wood | 2.5 / 2.5 | axe |
| birch | `lepton:birch_chest` | `lepton:trapped_birch_chest` | Birch Chest / Birch Trapped Chest | `minecraft:birch_planks` | wood | 2.5 / 2.5 | axe |
| jungle | `lepton:jungle_chest` | `lepton:trapped_jungle_chest` | Jungle Chest / Jungle Trapped Chest | `minecraft:jungle_planks` | wood | 2.5 / 2.5 | axe |
| acacia | `lepton:acacia_chest` | `lepton:trapped_acacia_chest` | Acacia Chest / Acacia Trapped Chest | `minecraft:acacia_planks` | wood | 2.5 / 2.5 | axe |
| dark oak | `lepton:dark_oak_chest` | `lepton:trapped_dark_oak_chest` | Dark Oak Chest / Dark Oak Trapped Chest | `minecraft:dark_oak_planks` | wood | 2.5 / 2.5 | axe |
| mangrove | `lepton:mangrove_chest` | `lepton:trapped_mangrove_chest` | Mangrove Chest / Mangrove Trapped Chest | `minecraft:mangrove_planks` | wood | 2.5 / 2.5 | axe |
| bamboo | `lepton:bamboo_chest` | `lepton:trapped_bamboo_chest` | Bamboo Chest / Bamboo Trapped Chest | `minecraft:bamboo_planks` | bamboo_wood | 2.5 / 2.5 | axe |
| cherry | `lepton:cherry_chest` | `lepton:trapped_cherry_chest` | Cherry Chest / Cherry Trapped Chest | `minecraft:cherry_planks` | cherry_wood | 2.5 / 2.5 | axe |
| crimson | `lepton:crimson_chest` | `lepton:trapped_crimson_chest` | Crimson Chest / Crimson Trapped Chest | `minecraft:crimson_planks` | nether_wood | 2.5 / 2.5 | axe |
| warped | `lepton:warped_chest` | `lepton:trapped_warped_chest` | Warped Chest / Warped Trapped Chest | `minecraft:warped_planks` | nether_wood | 2.5 / 2.5 | axe |
| nether brick | `lepton:nether_brick_chest` | `lepton:trapped_nether_brick_chest` | Nether Brick Chest / Nether Brick Trapped Chest | `minecraft:nether_brick` (block) | nether_brick | 2.0 / 6 | pickaxe |
| purpur | `lepton:purpur_chest` | `lepton:trapped_purpur_chest` | Purpur Chest / Purpur Trapped Chest | `minecraft:purpur_block` | stone | 1.5 / 6 | pickaxe |
| prismarine | `lepton:prismarine_chest` | `lepton:trapped_prismarine_chest` | Prismarine Chest / Prismarine Trapped Chest | `minecraft:prismarine` | stone | 1.5 / 6 | pickaxe |

Quark copies the block properties of the vanilla chest (wood) or of the base block (stone kinds);
chests never burn (Quark overrides their flammability to 0).

**States** (every chest):

| State | Values | Default | Meaning |
|---|---|---|---|
| `minecraft:cardinal_direction` (trait `minecraft:placement_direction`) | north, south, east, west | south | the direction the placing player looked; the front faces the opposite way (Java `FACING` = opposite) |
| `lepton:type` | single, left, right | single | Java `ChestType`: `left` = the partner is clockwise of the front (Java `FACING.getClockWise()`), `right` = counter-clockwise |
| `lepton:lid` | 0..4 | 0 | lid frame: 0 closed .. 4 fully open (0, 22.5, 45, 67.5, 90 degrees) |
| `lepton:power` (trapped only) | 0..15 | 0 | redstone output = viewers (clamped to 15) |

**Tags** (other units add them to their own chests): `lepton:variant_chest` (every regular variant
chest), `lepton:variant_trapped_chest` (every trapped one), `lepton:wooden_chest` (wooden ones, both
kinds: donk chests and reverting). A chest's partner must be the same block id.

**Recipes** (crafting table):

| Recipe id | Pattern / ingredients | Result |
|---|---|---|
| `lepton:<mat>_chest` | `###` `# #` `###`, `#` = the material's planks / base block | 1 `<mat>_chest` (priority over the vanilla chest recipe) |
| `lepton:<wood>_chest_wood_<log>` | `###` `# #` `###`, `#` = one log / wood / stripped log / stripped wood (stems, hyphae, bamboo blocks) of the wood | 4 `<wood>_chest` (only while U8's `logsToChests` is on, Quark flag `wood_to_chest_recipes`) |
| `lepton:trapped_<mat>_chest` | shapeless `<mat>_chest` + `minecraft:tripwire_hook` | 1 `trapped_<mat>_chest` |
| `lepton:<wood>_chest_revert` | shapeless `<wood>_chest` | 1 `minecraft:chest` (`enableRevertingWoodenChests`, wooden chests only = Quark tag `quark:revertable_chests`) |
| `lepton:trapped_<wood>_chest_revert` | shapeless `trapped_<wood>_chest` | 1 `minecraft:trapped_chest` (same) |

Vanilla's `chest + tripwire hook -> trapped chest` already exists on Bedrock. Mixed planks keep
making the vanilla chest (vanilla Bedrock recipe on the `minecraft:planks` tag).

**Drops:** the chest itself (any tool; explosions drop it too, like vanilla), plus its contents.

**Creative inventory:** category Items, after the vanilla chests (regular chests first, then the
trapped ones).

### 2.2 VariantFurnaces

| Id | English name | Base | Sound | Hardness / resistance | Tool | Light |
|---|---|---|---|---|---|---|
| `lepton:deepslate_furnace` | Deepslate Furnace | `minecraft:cobbled_deepslate` | deepslate | 3.5 / 6 (vanilla deepslate 3 / 6 copied; Bedrock furnace mining time 3.5 kept) | pickaxe | 13 when lit |
| `lepton:blackstone_furnace` | Blackstone Furnace | `minecraft:blackstone` | stone | 3.5 / 6 | pickaxe | 13 when lit |

States: `minecraft:cardinal_direction` (trait), `lepton:lit` (bool), `lepton:soul` (bool, blackstone
only: the block below is a soul fire base). Tag `lepton:variant_furnace` (both), `lepton:soul_furnace`
(blackstone).

Textures: side, top, front, lit front; the blackstone furnace also has a soul-fire lit front.

| Recipe id | Pattern | Result |
|---|---|---|
| `lepton:deepslate_furnace` | `###` `# #` `###`, `#` = cobbled deepslate | 1 (priority over the vanilla furnace recipe, which still takes mixed stones) |
| `lepton:blackstone_furnace` | `#` = blackstone | 1 |
| `lepton:<kind>_blast_furnace` | `III` `IXI` `###`, I = iron ingot, X = the furnace, # = smooth stone | `minecraft:blast_furnace` |
| `lepton:<kind>_smoker` | ` # ` `#X#` ` # `, # = any log (`minecraft:logs` tag) | `minecraft:smoker` |
| `lepton:<kind>_minecart` | shapeless furnace + minecart | `minecraft:furnace_minecart` (Bedrock has no furnace minecart: **not ported**, see §7) |

Drops itself, its items and its stored experience.

## 3. Mechanics

### 3.1 Chests

- **Placement** (Java `ChestBlock#getStateForPlacement`): facing = towards the player. Not
  sneaking: if the block on the front's clockwise side is the same chest, single and facing the
  same way, the new chest becomes `left`; else the counter-clockwise side makes it `right`.
  Sneaking while clicking the side of a single equal chest pairs with that chest when the axes
  allow (the new chest takes its facing). Sneaking otherwise places a single chest. The partner
  turns into the opposite half at once (Java `updateShape`).
- **Unpairing:** when a half disappears (broken, exploded, replaced), the other turns single and
  keeps its own 27 slots; the broken half's 27 slots drop.
- **Opening** (use, not while sneaking with an item): blocked when a solid block (redstone
  conductor) is above the chest or above its partner, or a sitting cat is on top. Opens the
  vanilla chest screen: 27 slots ("Chest") or 54 for a double chest ("Large Chest"; the half on the
  player's left when facing the front holds the top three rows, as in Java). A chest renamed in an
  anvil shows its name. Several players can use one chest at once.
- **Lid and sounds:** the first viewer opens the lid (frames 1..4 over 4 ticks, Java's cubic
  ease-out sampled at 22.5 degree steps) and plays `random.chestopen` (volume 0.5, pitch 0.9..1.0);
  when the last viewer leaves the lid closes (frames 3..0 over 5 ticks) and `random.chestclosed`
  plays. Both halves of a double chest animate together; sounds play at the middle of the chest.
- **Trapped chests:** power = number of viewers, clamped to 15; weak power to every side and strong
  power downwards (Java `getSignal` / `getDirectSignal(UP)`); both halves.
- **Comparators** read the fill level of the whole (single or double) inventory (Java
  `AbstractContainerMenu#getRedstoneSignalFromContainer`), also through a solid block.
- **Hoppers, hopper minecarts and droppers** insert into and pull from the chest like a vanilla
  chest (54 slots for a double chest). Pistons cannot move chests (vanilla Java).
- **Piglins** within 16 blocks that see the player get angry when a chest opens (Java
  `PiglinAi.angerNearbyPiglins(player, true)`).
- **Donk chest:** using a wooden variant chest (`lepton:wooden_chest`, regular or trapped) on a
  tamed adult donkey, mule or llama without a chest (player not sneaking) equips a chest (the
  vanilla `minecraft:on_chest` event, sound `armor.equip_generic`), consumes one item and remembers
  it; when the animal dies the chest it drops is that variant chest.
- **Structure chests** (`replaceWorldgenChests`): every vanilla chest / trapped chest that world
  generation gives a loot table becomes the variant of the structure, keeping facing, pairing and
  loot table (the loot is rolled when the chest is first opened, broken or touched by a hopper or
  comparator). Quark's structure tags, mapped to Bedrock loot tables:

  | Variant | Quark structures | Bedrock chest loot tables |
  |---|---|---|
  | oak | village_plains, stronghold | `chests/village/village_plains_house`, generic village chests in other biomes, `chests/stronghold_*` |
  | spruce | igloo, village_snowy, village_taiga | `chests/igloo_chest`, `village_snowy_house`, `village_taiga_house`, generic village chests in taiga / snowy biomes |
  | birch | desert_pyramid, village_desert | `chests/desert_pyramid`, `village_desert_house`, generic village chests in deserts |
  | acacia | village_savanna | `village_savanna_house`, generic village chests in savannas |
  | jungle | jungle_pyramid | `chests/jungle_temple`, `chests/jungle_temple_dispenser` excluded |
  | dark oak | mansion, pillager_outpost | `chests/woodland_mansion`, `chests/pillager_outpost` |
  | crimson | ruined_portal, bastion_remnant | `chests/ruined_portal`, `chests/bastion_*` |
  | nether brick | fortress | `chests/nether_bridge` |
  | prismarine | ocean_ruin, monument | `chests/underwater_ruin_big`, `chests/underwater_ruin_small` |
  | purpur | end_city | `chests/end_city_treasure` |
  | ancient (U7) | ancient_city | `chests/ancient_city`, `chests/ancient_city_ice_box` (only if `lepton:ancient_chest` exists) |

  "Generic village chests" are the village loot tables without a biome in the name (toolsmith,
  weaponsmith, temple, ...); the biome at the chest picks the village kind. `structureChests` adds
  entries `"<structure>=<chest block>"` where `<structure>` is one of the structure ids above
  (`minecraft:village_plains`, ...) or a loot table path (`loot_tables/chests/shipwreck.json`), and
  `<chest block>` is a regular variant chest id (the trapped variant is derived as in Quark).
- **Module off:** no recipes, no creative entries; existing blocks keep working.

### 3.2 Furnaces

- The block is a real vanilla furnace on the server: the block actor is the vanilla
  `FurnaceBlockActor` (smelting recipes, fuel times, XP, recipe book, hopper faces: top = input,
  sides = fuel, bottom pulls the result, comparator output = fill level of its 3 slots).
- Lit state: `lepton:lit` follows the furnace, light 13; the front texture shows the fire. Lit
  furnaces emit smoke + flame particles at the front (blackstone with soul state: soul fire flame)
  and crackle (`block.furnace.lit`) with chance 0.1 per particle tick, like Java's `animateTick`.
- Blackstone soul state: set at placement and whenever the block below changes: soul sand or soul
  soil (Java tag `soul_fire_base_blocks`).
- Breaking drops the items and the stored XP like a vanilla furnace.
- **Module off:** no recipes, no creative entries.

## 4. Screens

- Chests: the vanilla chest screen through `game/VirtualContainer` (every client, mod or vanilla);
  title "Chest" / "Large Chest" or the custom name.
- Furnaces: the vanilla furnace screen (recipe book, progress arrow, flame) for every client. The
  client needs a furnace block actor at the position to show the screen: mod clients patch their
  block registry like the server; vanilla clients are sent a vanilla furnace block at that position
  while the screen is open (it renders as a vanilla furnace for them until they close it).

## 5. Config

Section `variantcontainers` (`config/variantcontainers.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableVariantChests` | bool | true | VariantChests module |
| `enableRevertingWoodenChests` | bool | true | variant -> vanilla chest recipes |
| `replaceWorldgenChests` | bool | true | structure chests use the structure's variant |
| `structureChests` | string[] | `[]` | extra `"structure=chest"` mappings (see §3.1) |
| `enableVariantFurnaces` | bool | true | VariantFurnaces module |

Recipe changes take effect on the next world load (recipes are sent to clients on join).

## 6. Strings

| Key | en_US |
|---|---|
| `container.chest` | Chest |
| `container.chestDouble` | Large Chest |

Block names are in §2 (RP `texts.d/*/variantcontainers-gen.lang`).

## 7. Bedrock differences / not ported

- Custom blocks cannot have block-entity renderers, so the chest is block geometry: the lid opens
  in 4 visible steps (22.5 degree multiples, the finest rotation Bedrock block geometry allows)
  instead of a smooth per-frame animation.
- Chests are immovable by pistons (as in Java).
- The furnace-minecart recipes are not ported: Bedrock has no furnace minecart.
- Quark's JEI hint for the blackstone furnace is not ported (no JEI).
- Lootr integration is not ported (no Lootr on Bedrock).
- Structure identification uses the chest's loot table (and the biome for generic village chests)
  because Bedrock does not expose the generating structure to a chest.

## 8. Tests

Selftests (`variantcontainers.*`, dx 900..999), run on BDS by
`tools/e2e/server/scenarios/variantcontainers.ps1` and in the `full` scenario:

- `variantcontainers.registry`: every block, item and recipe exists; config defaults.
- `variantcontainers.pairing`: placement rules, pairing, unpairing keeps / drops the right slots.
- `variantcontainers.open`: chest screen 27 / 54 slots, blocked by a solid block above, lid frames
  and sounds, trapped chest power.
- `variantcontainers.hopper`: a hopper fills a chest; a hopper below empties a double chest.
- `variantcontainers.comparator`: comparator output from a variant chest and a furnace.
- `variantcontainers.furnace`: smelting with fuel through hoppers, lit state, soul state, drops.
- `variantcontainers.donk`: a donkey takes a variant chest and drops it on death.
- `variantcontainers.worldgen`: a generated loot chest is replaced and its loot rolls on open.

Client scenario `tools/e2e/client/scenarios/variantcontainers.ps1`: screenshots of every chest
(single, double, open lid frames, trapped), the furnaces (unlit, lit, soul) and both screens.
