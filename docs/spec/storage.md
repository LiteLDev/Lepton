# U8 storage: compressed blocks, stools, rope, cut vines, potted plants, glass shards, utility recipes

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `building/CompressedBlocks`,
`building/Stools`, `building/Rope`, `building/ShearVines`, `building/MorePottedPlants`,
`tweaks/GlassShard` and `tweaks/UtilityRecipes`. Code: `src/feature/storage/`, pack:
`tools/pack/storage.ps1` (+ textures `tools/textures/storage.ps1`), selftests `storage.*` (dx 800..899).

## 1. Modules covered

### CompressedBlocks
Storage blocks for common items: 9 items <-> 1 block for charcoal, sugar cane, cactus, chorus fruit,
sticks, apples, golden apples, potatoes, carrots, golden carrots, beetroots, cocoa beans, nether wart,
gunpowder, sweet berries, glow berries, leather and rabbit hide, plus the Blaze Lantern (blaze rods and
powder). The Block of Charcoal and the Blaze Lantern burn forever (like netherrack) and are furnace fuel,
as is the Stick Stack; the Golden Apple Crate is a beacon base; the food blocks are compostable. Every
block has its own enable flag. With the nether wart sack enabled, 4 nether wart make a Nether Wart Block
(2x2) because the 3x3 pattern now makes the sack.

### Stools
16 coloured stools (wool + wooden slabs). Using a stool with an empty hand (nothing above it, player not
riding) sits the player on it through an invisible seat entity. The stool reports a comparator signal of
15 while someone sits on it. It halves fall damage and bounces entities like a bed. Sneak-using a block
item on its top makes it a "big" stool (a table) when the block above sits flush on it; it also becomes a
table whenever a block sits on top. A seated player stays seated when a piston moves the stool.

### Rope
A climbable rope coil that must hang from a ceiling (the block above must be rope or have a sturdy bottom
face). Using rope on rope extends the column at its bottom (the block below the end is pushed one down);
using it with any other item, an empty hand, or sneaking with rope pulls the bottom rope back up (the block
below the end comes up with it) and gives the rope back. A glass bottle / bucket used on the rope fills
from the water at the bottom. Dispensers place rope or extend a rope column in front of them. Moving a
block with block entity data follows the PistonsMoveTileEntities rules.

### ShearVines
Using shears on a vine turns it into a Cut Vine (same faces) that never grows, and removes every vine below
it; plays the sheep shear sound and damages the shears by 1 (not in creative).

### MorePottedPlants
24 more plants fit in a flower pot: beetroot, sweet berries, carrot, chorus flower, cocoa, short grass,
peony, large fern, lilac, melon stem, nether sprouts, nether wart, potato, pumpkin stem, rose bush, sea
pickle (light 3), sugar cane, sunflower, tall grass, twisting vines, vine, weeping vines, wheat, cave vines
(light 14), pitcher plant. (Quark's potted plants of other modules, e.g. blossom saplings or glow shrooms,
belong to the units that own those plants.)

### GlassShard
Glass and the 16 stained glass blocks drop 2-4 shards of their colour when broken without Silk Touch
(Fortune adds up to its level, capped at 4; explosions decay them). Four shards of one colour craft the
glass back; four mixed shards make Dirty Glass, which drops dirty shards, smelts back into glass and makes
Dirty Glass Panes.

### UtilityRecipes
A set of optional recipes and tweaks: stone tools from any stone-like material, dropper + bow (or sticks +
string) = dispenser, repeater from the redstone-torch pattern, minecarts crafted around their content,
chest boats directly, 8 logs = 4 chests, coral -> dye, 2x2 "bent" cookie/paper/bread recipes, compostable
poisonous potatoes and rotten flesh, dragon's breath that leaves no bottle, torches as fuel, bones smelt to
bone meal, charcoal -> black dye, 2 logs + iron = hopper, 2 logs = 16 sticks (2 bamboo blocks = 8),
raw ore blocks smelt 9x slower into metal blocks.

## 2. Blocks and items

Hardness/resistance are Quark's (`strength(h, r)`; `r` defaults to `h`). All blocks are in the
Construction tab unless noted; names come from `texts.d/*/storage-gen.lang`.

### CompressedBlocks

| Id | Name | Shape | Strength | Sound | Map colour | Extras | Recipe (both directions) |
|---|---|---|---|---|---|---|---|
| `lepton:charcoal_block` | Block of Charcoal | cube | 5 / 10, pickaxe, needs a pickaxe to drop | stone | black | infiniburn, fuel `charcoalBlockFuelTime` | 9 charcoal |
| `lepton:sugar_cane_block` | Sugar Cane Bundle | pillar (axis) | 0.5 | wood | light green | flammable 200, compost 100%, fuel 300 | 9 sugar cane |
| `lepton:cactus_block` | Cactus Batch | pillar | 0.5 | wood | green | flammable 50, compost 100%, fuel 300 | 9 cactus |
| `lepton:chorus_fruit_block` | Chorus Fruit Batch | pillar | 0.5 | wood | purple | flammable 10, fuel 300 | 9 chorus fruit |
| `lepton:stick_block` | Stick Stack | pillar | 0.5 | wood | wood | flammable 300, fuel `stickBlockFuelTime` | 9 sticks |
| `lepton:apple_crate` | Apple Crate | cube (top/bottom/side) | 1.5 | wood | red | flammable 150, compost 100%, fuel 300 | 9 apples |
| `lepton:golden_apple_crate` | Golden Apple Crate | cube | 1.5 | wood | gold | flammable 150, beacon base, fuel 300 | 9 golden apples |
| `lepton:potato_crate` | Potato Crate | cube | 1.5 | wood | orange | flammable 150, compost 100%, fuel 300 | 9 potatoes |
| `lepton:carrot_crate` | Carrot Crate | cube | 1.5 | wood | terracotta orange | flammable 150, compost 100%, fuel 300 | 9 carrots |
| `lepton:golden_carrot_crate` | Golden Carrot Crate | cube | 1.5 | wood | gold | flammable 150, fuel 300 | 9 golden carrots |
| `lepton:beetroot_crate` | Beetroot Crate | cube | 1.5 | wood | red | flammable 150, compost 100%, fuel 300 | 9 beetroots |
| `lepton:cocoa_beans_sack` | Cocoa Bean Sack | cube | 0.5 | wool (cloth) | brown | flammable 150, compost 100% | 9 cocoa beans |
| `lepton:nether_wart_sack` | Nether Wart Sack | cube | 0.5 | wool | red | flammable 150, compost 100% | 9 nether wart |
| `lepton:gunpowder_sack` | Gunpowder Sack | cube | 0.5 | wool | gray | flammable 150 | 9 gunpowder |
| `lepton:berry_sack` | Sweet Berry Sack | cube | 0.5 | wool | red | flammable 150, compost 100% | 9 sweet berries |
| `lepton:glowberry_sack` | Glow Berry Sack | cube | 0.5 | wool | yellow | light 14, flammable 150, compost 100% | 9 glow berries |
| `lepton:blaze_lantern` | Blaze Lantern | cube | 0.3 | glass | yellow | light 15, infiniburn, fuel `blazeLanternFuelTime` | `BPB/PPP/BPB` (B blaze rod, P blaze powder), no uncraft |
| `lepton:bonded_leather` | Bonded Leather | cube | 0.4 | wool | orange | - | 9 leather |
| `lepton:bonded_rabbit_hide` | Bonded Rabbit Hide | cube | 0.4 | wool | terracotta white | - | 9 rabbit hide |

`3x3` recipes: `###/###/###` -> 1 block; shapeless 1 block -> 9 items. Extra: shapeless 4 nether wart ->
1 `minecraft:nether_wart_block` (only while the nether wart sack is enabled; the game's own 3x3 nether
wart block recipe is then not loaded, since the sack takes that pattern). "fuel 300": Zeta gives every
Quark block with a wood sound 300 ticks of furnace fuel. Flammability values are Quark's (fire spread =
burn chance = the value). Drops: the block itself (charcoal block only with a pickaxe).

### Stools

`lepton:<colour>_stool` for the 16 dye colours (white, orange, magenta, light_blue, yellow, lime, pink,
gray, light_gray, cyan, purple, blue, brown, green, red, black), names "<Colour> Stool". Strength 0.2,
wood sound, no occlusion, coloured-blocks tab (after the beds). States: `lepton:big` (bool, table form),
`lepton:sat_in` (bool). Waterloggable. Shape: small: seat 0..16 x 1..9 x 0..16 + four 4x1x4 legs; big:
seat 8..16 + four 4x8x4 legs. Recipe: `###/WWW` (# wool of the colour, W any wooden slab) -> 4.
Tags: `lepton:stool`. Entity `lepton:stool` (seat; invisible, 0.375 x 0.5, rideable, not saved data).

### Rope

`lepton:rope` "Rope Coil": strength 0.5, wool sound, brown, flammable (spread 60 / burn 30), no collision,
selection box 6..10 x 0..16 x 6..10, cutout, climbable, waterloggable. Recipe `##/##/##` string -> 3.
Item tab: equipment / items (after the chain). Drops itself.

### ShearVines

`lepton:cut_vine` "Cut Vines": vine faces `lepton:north|south|east|west|up` (bool), no collision,
climbable, foliage-tinted vanilla vine texture, strength 0.2, vine sound, flammable 300. Drops nothing
(Quark's loot table is empty); not in the creative inventory.

### MorePottedPlants

`lepton:potted_<name>` (flower pot + plant), drops a flower pot + the plant item, picks back to a pot:

| Block | Plant item | Name |
|---|---|---|
| `potted_beetroot` | beetroot seeds | Potted Beetroots |
| `potted_berries` | sweet berries | Potted Sweet Berry Bush |
| `potted_carrot` | carrot | Potted Carrots |
| `potted_chorus` | chorus flower | Potted Chorus Tree |
| `potted_cocoa_bean` | cocoa beans | Potted Cocoa |
| `potted_short_grass` | short grass | Potted Grass |
| `potted_peony` | peony | Potted Peony |
| `potted_large_fern` | large fern | Potted Large Fern |
| `potted_lilac` | lilac | Potted Lilac |
| `potted_melon` | melon seeds | Potted Melon |
| `potted_nether_sprouts` | nether sprouts | Potted Nether Sprouts |
| `potted_nether_wart` | nether wart | Potted Nether Wart |
| `potted_potato` | potato | Potted Potatoes |
| `potted_pumpkin` | pumpkin seeds | Potted Pumpkin |
| `potted_rose` | rose bush | Potted Rose |
| `potted_sea_pickle` | sea pickle | Potted Sea Pickle (light 3) |
| `potted_sugar_cane` | sugar cane | Potted Sugar Cane |
| `potted_sunflower` | sunflower | Potted Sunflower |
| `potted_tall_grass` | tall grass | Potted Tall Grass |
| `potted_twisting_vines` | twisting vines | Potted Twisting Vines |
| `potted_vine` | vine | Potted Vines |
| `potted_weeping_vines` | weeping vines | Potted Weeping Vines |
| `potted_wheat` | wheat seeds | Potted Wheat |
| `potted_cave_vines` | glow berries | Potted Cave Vines (light 14) |
| `potted_pitcher_plant` | pitcher plant | Potted Pitcher Plant |

Shape: the flower pot box (5..11 x 0..6), instant break, no creative entry (as in Java). Grass, ferns,
tall grass, sugar cane and vines use biome tint.

### GlassShard

Items (ingredients tab, after the dyes): `lepton:clear_shard` "Glass Shard", `lepton:dirty_shard` "Dirty
Glass Shard", `lepton:<colour>_shard` "<Colour> Glass Shard"; item tag `lepton:shards` on all 18.
Blocks: `lepton:dirty_glass` "Dirty Glass" (strength 0.3, glass sound, brown, translucent, coloured
blocks tab after tinted glass), `lepton:dirty_glass_pane` "Dirty Glass Pane" (pane connections from
`game/Shapes`). Recipes: 2x2 shards of one colour -> glass of that colour (clear -> `minecraft:glass`,
dirty -> dirty glass); 2x2 of any shards -> dirty glass (lower priority); 6 dirty glass -> 16 panes;
smelting dirty glass -> glass (0.1 XP). Drops: glass/stained glass without Silk Touch: 2-4 shards of
the colour (+ uniform Fortune bonus 0..level, limited to 1..4, explosion decay); dirty glass: itself
with Silk Touch, else 2-4 dirty shards (same formula); dirty glass pane: only with Silk Touch.

### UtilityRecipes

Recipes (crafting table unless noted; each gated by its key in §5):

| Key | Recipes |
|---|---|
| `betterStoneToolCrafting` | stone sword/shovel/pickaxe/axe/hoe from stone, granite, diorite, andesite, deepslate, tuff, mossy cobblestone, flint, sandstone, calcite, and Lepton's limestone/jasper/shale/permafrost when U3 is present (cobblestone, cobbled deepslate and blackstone already work in Bedrock) |
| `enableDispenser` | shapeless dropper + bow -> dispenser; ` #X/#DX/ #X` (# stick, X string, D dropper) -> dispenser |
| `enableRepeater` | `X X/#X#/III` (X redstone, # stick, I stone) -> repeater |
| `enableMinecarts` | `#C#/###` iron + chest / trapped chest -> chest minecart; iron + furnace -> furnace minecart (Bedrock has none: see §7); iron + hopper -> hopper minecart; iron + TNT -> TNT minecart |
| `enableChestBoats` | `#X#/###` planks + chest -> chest boat of that wood (oak, spruce, birch, jungle, acacia, dark oak, mangrove, cherry, pale oak, bamboo raft) |
| `logsToChests` | `###/# #/###` any logs -> 4 chests |
| `coralToDye` | brain -> pink, bubble -> magenta, fire -> red, horn -> yellow, tube -> blue dye (coral or coral fan) |
| `bentRecipes` | `##/# ` wheat -> bread, `X#/# ` cocoa + wheat -> 8 cookies, `##/# ` sugar cane -> 3 paper |
| `boneMealUtility` | smelting bone -> 3 bone meal (0.05 XP) |
| `charcoalToBlackDye` | shapeless charcoal -> black dye |
| `easyHopper` | `IWI/IWI/ I ` iron + logs -> hopper |
| `easySticks` | `#/#` logs -> 16 sticks, `#/#` bamboo blocks -> 8 sticks |
| `smeltRawOreBlocks` | raw iron/copper/gold block -> iron/copper/gold block in a furnace (9x the time, 1800 ticks) and a blast furnace (900 ticks); XP 6.3 / 6.3 / 9 |

Tweaks: `compostableToxins` (poisonous potato 85%, rotten flesh 30%), `effectiveDragonBreath` (dragon's
breath has no crafting remainder; false: leaves a glass bottle), `torchesBurn` (torch / soul torch burn
400 ticks).

## 3. Mechanics

All hooks are installed at mod load (recipes and the creative inventory are built when a world loads)
and re-read the config on use. "Disabled" content: the recipes of a disabled module/flag are not loaded,
its blocks/items are left out of the creative inventory, and its native behaviour is off.

- **Recipe gating** (`Recipes::_loadRecipe` hook): every `lepton:storage_*` recipe has a gate in
  `src/feature/storage/Content.inc` (generated with the pack). A gated recipe is skipped while its gate
  is off, and recipes whose ingredients do not exist (another unit's blocks) are skipped silently.
  `minecraft:nether_wart_block` (3x3) is skipped while the nether wart sack is on.
- **Creative inventory** (`CreativeItemInitializer::_addCreativeItemServer`): items of disabled
  modules/flags are not added.
- **Fuel** (`FurnaceBlockActor::getItemBurnDuration` hook): charcoal block `charcoalBlockFuelTime`
  (16000), blaze lantern `blazeLanternFuelTime` (24000), stick stack `stickBlockFuelTime` (900), other
  wood-sound blocks of the unit (pillars, crates, stools) 300, torches/soul torches 400 (`torchesBurn`).
  Values are Java ticks per item in a furnace; the blast furnace/smoker halve them like vanilla. Fuel of
  disabled content is 0.
- **Burns forever**: the block types of the charcoal block and blaze lantern get the game's `InfiniBurn`
  property while `burnsForever` is on (fire on top never goes out, like netherrack).
- **Beacon base**: `BeaconBlockActor::$tick` hook. After the game's tick the pyramid is re-measured
  every 80 ticks (Java: 1..4 layers below, `x-l..x+l`, `z-l..z+l`) counting golden apple crates as
  base blocks; if that gives more levels than the game found, the beacon's level count is raised and
  its effects are applied with Java's numbers (range `10 * levels + 10`, duration `(9 + 2 * levels) * 20`
  ticks, amplifier 1 for level 4 with the same primary and secondary effect). Client side the level count
  is raised too so the beam shows.
- **Compost** (`ComposterBlock::getFillChance` hook): the compostable compressed blocks 100%;
  poisonous potato 85, rotten flesh 30 (`compostableToxins`).
- **Stools**: `PlayerInteractBlockEvent` on a stool with an empty main hand, nothing above (air) and the
  player not riding: spawn `lepton:stool` at (x+0.5, y+0.6, z+0.5), make the player ride it, set
  `sat_in`. Every tick each seat checks its block: a stool keeps it alive; a piston-moving block
  (`minecraft:moving_block` wrapping a stool) at or next to it keeps it alive (the seat follows the
  moving stool); otherwise, or without a rider, the seat is removed and the stool's `sat_in` is cleared.
  `big` = the block above has a collision box starting at its bottom (`minY == 0`), recomputed when the
  block above changes (`BlockChangedEvent`) and after a sneak-use with a block item on the stool's top.
  Comparator signal 15 while `sat_in` (hook on the comparator's input). Falling on a stool: fall
  damage halved and the entity bounces like on a bed (`getBounciness` / fall hooks).
- **Rope**: right-click handling in `PlayerInteractBlockEvent` (deferred to the next tick when the
  event is cancelled). Pull down: walk down the column; the first non-rope position must be in the
  world; if it is air/liquid ("was air at end") rope goes there, otherwise the block there is moved one
  down when the position below it is air or replaceable, and the rope takes its place. Pull up: the
  bottom rope is removed (water kept), the block under it is moved up into its place, the rope item is
  given (dropped when the inventory is full, nothing in creative). Moving refuses: unbreakable blocks,
  air, liquids, obsidian / crying obsidian / respawn anchor, blocks the game marks immovable or
  destroyed by pistons, blocks that cannot survive at the destination, and block entities blacklisted
  by PistonsMoveTileEntities (`movementBlacklist`: spawner, trial spawner, vault, ender chest, enchanting
  table, ceiling hanging signs, trapped chests; jukebox playing a disc; piston heads) — all block
  entities are refused when U10's `enablePistonsMoveTileEntities` is false unless
  `forceEnableMoveTileEntities`. Block entity data is moved with the block. Entities standing on top
  of the moved block move with it. Survival: rope pops (drops) when the block above is neither rope nor
  sturdy at its bottom (checked when blocks change and on placement). Climbing: the block type has the
  game's `Climbable` property. Sounds: wool place/break at volume 0.5.
- **Rope dispenser** (`Item::$dispense` hook, `enableDispenserBehavior`): rope in front -> pull down
  (consume one on success); air in front and rope can hang there -> place a rope (consume one);
  otherwise nothing happens (the rope stays in the dispenser).
- **Shear vines**: `PlayerInteractBlockEvent` with shears on `minecraft:vine`: the vine becomes a cut
  vine with the same faces (Bedrock's `vine_direction_bits`), all vines / cut vines straight below are
  removed, sheep-shear sound, shears take 1 damage (not in creative). Cut vines lose faces without
  support like vines and are removed with none left (checked on block changes).
- **Potting**: `PlayerInteractBlockEvent` on an empty `minecraft:flower_pot` with one of the plant items
  -> the pot becomes `lepton:potted_<plant>` and one item is used (not in creative). Using a potted block
  gives the plant back (or drops it) and leaves an empty flower pot. Breaking drops the pot and the plant.
- **Glass shards**: `BlockType::spawnResources` hook for glass, the 16 stained glass blocks and dirty
  glass (see §2 for the formula); Silk Touch keeps the vanilla behaviour.
- **Dragon's breath**: `Item::mCraftingRemainingItem` of `minecraft:dragon_breath` is set to nothing
  (`effectiveDragonBreath`) or a glass bottle.
- **Raw ore blocks**: the furnace tick is slowed for raw iron/copper/gold blocks (progress advances on
  one of 9 ticks: 1800 ticks in a furnace); `_getXPRewardMultiplier` gives 6.3/6.3/9.

## 4. Screens

None. Sitting uses the game's riding (works for every client). No form fallback is needed.

## 5. Config

Section `storage` (`config/storage.json`), `version` 1. Every key of SPEC §5.1 U8, Quark defaults:

| Key | Default | Meaning |
|---|---|---|
| `enableCompressedBlocks` | true | CompressedBlocks module |
| `burnsForever` | true | charcoal block / blaze lantern keep fire burning |
| `charcoalBlockFuelTime` | 16000 | ticks (min 0) |
| `blazeLanternFuelTime` | 24000 | ticks (min 0) |
| `stickBlockFuelTime` | 900 | ticks (min 0) |
| `enableCharcoalBlock`, `enableSugarCaneBlock`, `enableCactusBlock`, `enableChorusFruitBlock`, `enableStickBlock`, `enableAppleCrate`, `enableGoldenAppleCrate`, `enablePotatoCrate`, `enableCarrotCrate`, `enableGoldenCarrotCrate`, `enableBeetrootCrate`, `enableCocoaBeanSack`, `enableNetherWartSack`, `enableGunpowderSack`, `enableBerrySack`, `enableGlowBerrySack`, `enableBlazeLantern`, `enableBondedLeather`, `enableBondedRabbitHide` | true | one block each |
| `enableStools` | true | Stools module |
| `enableRope` | true | Rope module |
| `forceEnableMoveTileEntities` | false | ropes move block entities even if PistonsMoveTileEntities is off |
| `enableDispenserBehavior` | true | dispensers place / extend rope |
| `enableShearVines` | true | ShearVines module |
| `enableMorePottedPlants` | true | MorePottedPlants module |
| `enableGlassShard` | true | GlassShard module |
| `enableUtilityRecipes` | true | UtilityRecipes module |
| `betterStoneToolCrafting`, `enableDispenser`, `enableRepeater`, `enableMinecarts`, `enableChestBoats`, `logsToChests`, `coralToDye`, `bentRecipes`, `compostableToxins`, `effectiveDragonBreath`, `torchesBurn`, `boneMealUtility`, `charcoalToBlackDye`, `easyHopper`, `easySticks`, `smeltRawOreBlocks` | true | UtilityRecipes options (§2) |

Recipe gates take effect when a world loads (the game builds its recipe table then); everything else
applies at once after `/quark reload`.

## 6. Strings

The unit has no chat/screen text of Quark. Lepton-only log lines (for e2e): `storage: ...`.
Block/item names are in §2 (Quark's English; zh_CN from Quark's zh_cn.json, "Potted Pitcher Plant" /
"瓶子草盆栽" written for Lepton since Quark has no name for it). Entity: `entity.lepton:stool.name`
"Stool".

## 7. Bedrock differences / not ported

- **Furnace minecart**: Bedrock has no furnace minecart; that recipe is not ported.
- **Chest boats of U7's woods** (azalea, blossom, ancient): boats are not generated in Lepton
  (tools/pack/README.md), so those recipes don't exist.
- **Mixed shards -> dirty glass**: Bedrock recipes cannot exclude "all the same colour"; the tag recipe
  has a lower priority than the single-colour recipes, which win when all four shards match.
- **Blaze lantern animation**: the texture is static (Bedrock's flipbook table is a single shared file).
- **Dirty glass beacon tint** (Quark: beacon colour multiplier 0x3F1F00) needs U12's beam colouring
  (`lepton:dirty_glass` is listed for it in the PR); without U12 the beam passes through untinted.
- **Fuel in vanilla clients' furnace screen**: the fuel slot check runs on the server for hoppers and
  item requests; a vanilla client may refuse to drag a Lepton block into the fuel slot by hand (the
  client mod has the same hook).
- **Vanilla clients and climbing**: rope and cut vine climbing is predicted by the client; without the
  client mod the server corrects the movement, so climbing is less smooth.
- **Potted plants** use the game's own plant textures in a flower pot (Quark draws miniature versions).

## 8. Tests

Selftests (dx 800..899): `storage.content` (blocks/items registered), `storage.recipes`,
`storage.fuel`, `storage.compost`, `storage.infiniburn`, `storage.beacon`, `storage.stool_sit`,
`storage.stool_table`, `storage.stool_piston`, `storage.rope_down_up`, `storage.rope_move_block`,
`storage.rope_survive`, `storage.rope_dispenser`, `storage.shear_vines`, `storage.potting`,
`storage.glass_shards`, `storage.dragon_breath`, `storage.gallery` (client screenshots).
E2E: `tools/e2e/server/scenarios/storage.ps1` (all `storage.*`), `tools/e2e/client/scenarios/storage.ps1`
(gallery screenshots of every block, a seated player, rope column, potted plants, shards).
