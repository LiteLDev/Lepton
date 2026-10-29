# U10 redstone: chains, iron rods, piston block entities, gravisand, metal buttons, obsidian plates, randomizers, ender watchers, block-placing dispensers

## 1. Modules covered

### ChainsConnectBlocks (`enableChainsConnectBlocks`)
Chains are "indirectly sticky" for pistons (Zeta's piston structure resolver): when a piston moves a
block that a chain is attached to along the chain's axis, the chain moves too, and so does whatever
is attached to the chain's other end (again only along the chain's axis). Two chains only link when
their axes are equal. A chain next to a moving block but lying across it (another axis) is ignored.

### IronRod (`enableIronRod`)
`lepton:iron_rod`, an end-rod shaped iron block. A piston that pushes an iron rod facing the push
direction breaks (and drops) the block in front of the rod instead of pushing it, like a drill. Blocks
the piston couldn't move stop the piston (Quark: PREVENT). The `#quark:iron_rod_immune` tag (only
Lootr containers in Quark) becomes `lepton:iron_rod_immune` (empty, other units may add blocks):
in front of an immune block the rod behaves like a normal block. A moving iron rod also mines
U20's oretoises (the moving block carries `lepton:breaks_toretoise_ore`).

### PistonsMoveTileEntities (`enablePistonsMoveTileEntities`)
Pistons move blocks with block entities (chests, furnaces, ...), except the movement blacklist. Double
chests move together (`enableChestsMovingTogether`). Dispensers and droppers (`delayedUpdateList`) get
their neighbour / comparator update one tick after they land. Bedrock pistons already move block
entities natively; see §7 for the exact parity rules. The piston push limit is Zeta's
`pistonPushLimit` (12).

### Gravisand (`enableGravisand`)
`lepton:gravisand`, a sand block that only falls when it gets a redstone signal (2 ticks later). When
the block below can't be fallen into, it falls **up** instead. When it starts falling, every gravisand
block next to it (6 sides) also tries to fall 2 ticks later, whether powered or not, so a connected
body of gravisand moves as a whole. Falling gravisand is the entity `lepton:gravisand` ("Floating
Gravisand"); it never deals fall damage and becomes the block again where it lands (or drops as an
item when it can't). It gives comparators a signal of 15. U12's magnets treat the entity as magnetic.

### MetalButtons (`enableMetalButtons`, `enableIron`, `enableGold`)
`lepton:iron_button` (100 tick = 5 s pulse) and `lepton:gold_button` (4 tick = 2 redstone tick pulse).
Not pressable by arrows (iron block set). Metal break sounds, stone click sounds.

### ObsidianPlate (`enableObsidianPlate`)
`lepton:obsidian_pressure_plate`: a stone-type pressure plate (signal 15, released 20 ticks after the
last press) that only players (not spectators) press.

### RedstoneRandomizer (`enableRedstoneRandomizer`)
`lepton:redstone_randomizer`, a diode the shape of a repeater. When its input (back side) gets power
it picks left or right at random (2 tick delay) and outputs a strong signal of 15 to that side only;
when the input goes off it turns off (2 ticks). A new random side is chosen each time it turns on.
Needs a sturdy block below (otherwise it breaks and drops), breaks when pushed by pistons, and shows
redstone dust particles while on.

### EnderWatcher (`enableEnderWatcher`)
`lepton:ender_watcher`: emits a weak redstone signal 0-15 depending on how close to the centre of the
face a player looks (ray from the eyes, 64 blocks, player within 80 blocks per axis). Players wearing
a carved pumpkin are ignored. While watched the eye opens (`lepton:watched`) and red dust particles
appear around it.

### DispensersPlaceBlocks (`enableDispensersPlaceBlocks`)
Dispensers place block items (and plantable items such as seeds, potatoes, carrots, sweet berries,
redstone dust, string) in front of them, facing like a player placing it while looking along the
dispenser's direction (stairs face back towards the dispenser, slabs go to the bottom half). Items in
`blacklist` keep their vanilla behaviour. Blocks that already have a dispense behaviour (TNT, shulker
boxes, heads, ...) keep it; with `wrapExistingBehaviors` the placement is tried when that behaviour
fails. A block that can't be placed is not thrown out: the dispenser fails (click).

## 2. Blocks and items

All in the creative "Items" category (Quark: Redstone Blocks tab, buttons and plates in their vanilla
groups).

| Id | English name | States | Hardness / resistance | Tool, drops | Sound |
|---|---|---|---|---|---|
| `lepton:iron_rod` | Iron Rod | `minecraft:block_face` (trait, the rod's facing) | 5 / 10 | pickaxe; drops itself | metal |
| `lepton:gravisand` | Gravisand | - | 0.5 / 0.5 | shovel; drops itself | sand |
| `lepton:iron_button` | Iron Button | `minecraft:block_face`, `lepton:pressed` | 0.5 / 0.5 | any; itself | metal |
| `lepton:gold_button` | Gold Button | `minecraft:block_face`, `lepton:pressed` | 0.5 / 0.5 | any; itself | metal |
| `lepton:obsidian_pressure_plate` | Obsidian Pressure Plate | `lepton:pressed` | 2 / 1200 | pickaxe required for drops | stone |
| `lepton:redstone_randomizer` | Redstone Randomizer | `minecraft:cardinal_direction` (trait; the player's look direction, the input is on the opposite side), `lepton:powered` off/left/right | 0 / 0 | instant; itself | stone |
| `lepton:ender_watcher` | Ender Watcher | `lepton:watched` (bool), `lepton:power` 0..15 | 3 / 10 | pickaxe; itself | metal |

Entity `lepton:gravisand` ("Floating Gravisand"): 0.98 x 0.98, not summonable from spawn eggs.

Recipes (shaped unless noted):

| Result | Pattern / ingredients |
|---|---|
| Iron Rod | `I` / `I` / `R`: iron ingot x2, end rod (`usePreEndRecipe`: `I`/`I`/`I`, 3 iron ingots) |
| Gravisand x8 | `SSS` / `SES` / `SSS`: sand, ender pearl |
| Iron Button | shapeless: any wooden button + iron ingot (one recipe per vanilla wooden button) |
| Gold Button | shapeless: any wooden button + gold ingot |
| Obsidian Pressure Plate | `OO`: obsidian or crying obsidian (`c:obsidians`) |
| Redstone Randomizer | ` X ` / `XBX` / `III`: redstone torch, prismarine crystals, stone |
| Ender Watcher | `BRB` / `RER` / `BRB`: obsidian, redstone, eye of ender |

Tags: iron rod `lepton:collateral_mover`, `lepton:collateral_breaker` (U12 magnets),
`lepton:breaks_toretoise_ore` (U20); buttons `lepton:button`, `lepton:press_ticks_100` /
`lepton:press_ticks_4`; plate `lepton:pressure_plate`, `lepton:players_only`,
`lepton:press_ticks_20`. Block tag `lepton:iron_rod_immune` (none of Lepton's blocks).
Movement: buttons, plate and randomizer break when pushed (`minecraft:movable` popped).

## 3. Mechanics

### Piston structure (ChainsConnectBlocks, IronRod, PistonsMoveTileEntities)
Lepton ports Zeta's `ZetaPistonStructureResolver` (the replacement of Java's piston structure
resolver) and runs it in `PistonBlockActor::_checkAttachedBlocks` on the server:
- The resolver walks the push line from the first block, pulls sticky lines behind it, branches from
  sticky blocks (slime, honey; they never stick to each other) and from chains (indirect connectors),
  adds blocks that break (`popped`) to the break list, and fails when it meets an immovable block or
  exceeds `pistonPushLimit` blocks.
- Pushability follows Bedrock's rules (`minecraft:movable` / the block's movement type, world height,
  extended pistons and piston arms can't move, the piston itself). With PistonsMoveTileEntities on,
  blocks with block entities in `movementBlacklist` / `namespaceMovementBlacklist` (and jukeboxes
  playing a disc) can't be moved, block entities Bedrock refuses but Java Quark moves are moved, and
  double chests move together (`enableChestsMovingTogether`; when off a pushed half becomes single).
- Iron rods facing the move direction (and not in front of a `lepton:iron_rod_immune` block) end their
  line: the block in front of the rod is added to the break list if pistons could move it, otherwise
  the piston fails.
- When the resolved structure uses none of these features and the push limit is 12, the game's own
  result is kept, so vanilla machines behave exactly as in vanilla Bedrock.

### Gravisand
- Power: the block has `minecraft:redstone_consumer` (any side), so the circuit knows which wires and
  sources power it. When the circuit evaluates it powered (`ConsumerComponent::$evaluate` hook), or a
  neighbour changes while it is powered (`Block::neighborChanged` hook; Java `neighborChanged` +
  `hasNeighborSignal`), a tick is scheduled in 2 ticks. The tick tries to fall down, else up; when it falls it schedules a tick in 2
  ticks for every adjacent gravisand.
- A fall direction is free when the target is air or a block falling blocks fall through (air, fire,
  water, lava, replaceable plants, ...).
- Entity motion: Java FallingBlockEntity: velocity y -= 0.04 per tick, moved by velocity (mirrored
  for up), then velocity *= 0.98. Down: lands on the first block with a collision box below; up: when
  the block above has a collision box. It is then placed if the spot is replaceable, otherwise dropped
  as an item. It is dropped after 600 ticks, or 100 ticks when outside the build height.
- Comparators read 15 from gravisand (also through a solid block, like vanilla analog sources).

### Buttons and plates
Native behaviour from `game/Shapes` (`lepton:press_ticks_<n>`, `lepton:players_only`, added to the
shared contract by this unit).

### Redstone Randomizer
- Input: the signal the block behind it (`pos + facing`, Java `FACING`, i.e. the player's side when
  placing) sends towards it: redstone wire power, producers (torches, levers, blocks of redstone,
  buttons, plates, observers facing away), repeaters / comparators pointing at it, powered blocks,
  and Lepton's randomizers / ender watchers.
- Checked every tick (`minecraft:tick`; blocks that arrive without a pending tick, e.g. from
  `/setblock`, get one queued). A change is applied 2 ticks later if it still holds; turning
  on picks left (`facing` clockwise, the player's left) or right with 50 % each.
- Output: `minecraft:redstone_producer` power 15, strongly powering the chosen side only. A data-driven
  producer keeps the faces of the permutation it was created for, so the circuit component is removed
  when it turns off and set up again for the new side when it turns on.

### Ender Watcher
Every tick (`minecraft:tick`): for each player within 80 blocks (per axis) of the block centre whose
head isn't a carved pumpkin, ray-trace 64 blocks from the eyes along the player's look rotation
(block outlines, no liquids). When the
hit block is the watcher: `fract = 1 - |offset in the face plane from the face centre| / 0.7071`,
`watch = ceil(fract * 15)`; the strongest player wins. `lepton:watched` = any player looking,
`lepton:power` = the strongest watch (state changes only when they differ). Red dust particles while
watched.

### Dispensers place blocks
`Item::dispense` is hooked: when the item's own behaviour fails and the item places a block (or plants
one), the block is placed at the dispenser's front if that spot can be replaced and the block may be
placed there (`mayPlace` / survival: seeds need farmland, ...). Orientation: logs and other pillars
along the dispenser axis; `facing_direction` blocks face along the dispenser; horizontally-facing
blocks face back towards the dispenser (stairs: their back to the dispenser); slabs bottom half;
doors place both halves; tall plants both halves. Items without a block of their own on Bedrock use a
table (seeds -> crops, carrot -> carrots, redstone -> redstone wire, string -> trip wire, sugar cane
-> reeds, sweet / glow berries, kelp, ...) or the block with the item's id (doors, cakes, pots). The
item count goes down by one. Failure plays the dispenser fail sound and keeps the item.

## 4. Screens
None. Everything works the same for vanilla clients (block states, entities and vanilla sounds).

## 5. Config
Section `redstone` (`config/redstone.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableChainsConnectBlocks` | bool | true | ChainsConnectBlocks |
| `enableIronRod` | bool | true | IronRod |
| `usePreEndRecipe` | bool | false | iron rod from 3 iron ingots instead of 2 + end rod |
| `enablePistonsMoveTileEntities` | bool | true | PistonsMoveTileEntities |
| `enableChestsMovingTogether` | bool | true | double chests move as one |
| `renderBlacklist` | string[] | `["psi:programmer", "botania:starfield"]` | kept for Quark parity; Bedrock renders every moving block entity itself |
| `movementBlacklist` | string[] | Quark's list (§SPEC 5.1) | ids or `#tags` of blocks pistons can't move |
| `namespaceMovementBlacklist` | string[] | `[]` | namespaces pistons can't move |
| `delayedUpdateList` | string[] | `["minecraft:dispenser", "minecraft:dropper"]` | blocks updated a tick after landing |
| `pistonPushLimit` | int | 12 | Zeta's general config "Changes the piston push limit": blocks a piston can move (min 1) |
| `enableGravisand` | bool | true | Gravisand |
| `enableMetalButtons` | bool | true | MetalButtons |
| `enableIron` | bool | true | iron button |
| `enableGold` | bool | true | gold button |
| `enableObsidianPlate` | bool | true | ObsidianPlate |
| `enableRedstoneRandomizer` | bool | true | RedstoneRandomizer |
| `enableEnderWatcher` | bool | true | EnderWatcher |
| `enableDispensersPlaceBlocks` | bool | true | DispensersPlaceBlocks |
| `blacklist` | string[] | `["minecraft:water", "minecraft:lava", "minecraft:fire"]` | blocks dispensers never place |
| `wrapExistingBehaviors` | bool | true | try the placement when an item's own dispense behaviour fails |

Tags in `movementBlacklist`: `#minecraft:ceiling_hanging_signs` = hanging signs hanging from a ceiling
(`hanging` state), `#c:chests/trapped` = `minecraft:trapped_chest`; other `#tag`s are block tags.
A disabled module's recipes are removed when a world loads; its blocks stay (Bedrock can't unregister
blocks) but do nothing special.

## 6. Strings
Lang (`assets/lang/<locale>/redstone.json`), Quark's hint texts (shown nowhere in Quark except JEI;
kept so other units / docs can reference them):

| Key | en_US |
|---|---|
| `quark.jei.hint.ender_watcher` | Emits a redstone signal based on how close to the center of the eye a player is looking. |
| `quark.jei.hint.gold_button` | Emits a very short, 2 redstone tick, pulse. |
| `quark.jei.hint.gravisand` | Only falls when given a redstone signal, taking adjacent ones along with it. If it can't fall, it'll float upwards. Emits a comparator signal of 15. |
| `quark.jei.hint.iron_button` | Emits a very long, 5 second, pulse. |
| `quark.jei.hint.iron_rod` | Will break any blocks in front of it when pushed with a piston. |
| `quark.jei.hint.redstone_randomizer` | Randomly repeats the given signal either left or right. |
| `quark.jei.hint.piston_te` | Can now push complex blocks (aka Tile Entities), except Monster Spawners. |
| `quark.jei.hint.piston_max_blocks` | The max amount of blocks that can be moved has also been changed to %d. |
| `quark.jei.hint.minecraft.chain` | When connected to a block, if that block is moved by a piston, so will the Chain and anything else on the other end. |

Names (RP `texts.d`): the block names of §2, `entity.lepton:gravisand.name` = Floating Gravisand.

## 7. Bedrock differences / not ported
- **Block entities and pistons.** Bedrock pistons already move most block entities (chests, furnaces,
  hoppers, dispensers, ...). Lepton keeps that and adds Quark's rules on top: the movement blacklist
  (Bedrock moves e.g. trapped chests, Quark doesn't), jukeboxes with a disc stay, and block-entity
  blocks that Bedrock refuses only because of their block entity are moved. Unbreakable blocks
  (command blocks, structure blocks, end portal frames, ...) stay immovable in both games. With the
  module off pistons behave like vanilla Bedrock (which moves more than vanilla Java).
- `renderBlacklist` has no effect: Bedrock renders moving block entities itself and the listed blocks
  are Java mod blocks.
- `IPistonCallback` (a Java API for other mods) is not ported.
- Iron rod: Quark's `connected` state is never set by Quark (only the plain rod model is used), so
  Lepton has no such state.
- Comparators: Bedrock data-driven blocks can't declare a comparator output. Lepton gives
  `lepton:gravisand` its own copy of the block type's virtual table with `hasComparatorSignal` /
  `getComparatorSignal` replaced (installed per world on the block type), so vanilla comparators read 15.
- Dispensers: Bedrock has no "optional behaviour" flag; items whose own Bedrock dispense behaviour
  succeeds keep it, the placement runs when it fails (`wrapExistingBehaviors`), and with
  `wrapExistingBehaviors` false the placement is skipped for shulker boxes, carved pumpkins and heads
  (Java's optional block behaviours). Beds keep Bedrock's behaviour (thrown out): their head part and
  colour need a placing player. Banners, signs and other blocks that need a placer's rotation use their
  default rotation.
- Bedrock 1.26.51 marks beacons, jukeboxes and the sculk sensor / calibrated sculk sensor / catalyst /
  shrieker immovable; on Java only their block entity stops pistons, so with PistonsMoveTileEntities on
  Lepton gives them the push_pull movement type (restored when the module is off). Lodestones are
  immovable on Bedrock for their own reasons (compass links) and stay so.
- A recipe disabled by the config is removed when a world loads; turning the module back on brings it
  back after a restart (the world's recipe list is built once).
- Recipes: Bedrock has no wooden-button item tag, so the metal buttons have one recipe per vanilla
  wooden button (Lepton's own wooden buttons are not ingredients).
- Advancement triggers (`watcher_center`) and JEI hints are not ported (SPEC §8).

## 8. Tests
Selftests (dx 1000..1099): `redstone.blocks` (ids, states, tags, Bedrock's immovable block entities),
`redstone.recipes`, `redstone.iron_rod_breaks`, `redstone.iron_rod_prevent`, `redstone.chain_drags`,
`redstone.chain_axis`, `redstone.push_limit`, `redstone.te_blacklist`, `redstone.te_movable`,
`redstone.chests_together`, `redstone.gravisand_falls`, `redstone.gravisand_floats`,
`redstone.gravisand_chain`, `redstone.gravisand_comparator`, `redstone.metal_buttons`,
`redstone.obsidian_plate`, `redstone.randomizer`, `redstone.ender_watcher`,
`redstone.dispenser_places`, `redstone.dispenser_seeds`, `redstone.dispenser_door`,
`redstone.dispenser_blacklist`, `redstone.gallery` (builds the row for the client screenshots).
E2E: `tools/e2e/server/scenarios/redstone.ps1` (all selftests + log lines);
`tools/e2e/client/scenarios/redstone.ps1` (screenshots `watcher`: the watched ender watcher next to an
idle one; `row`: iron rods in three facings, gravisand, watchers, idle / powered randomizers, buttons,
plate; `floating`: a gravisand entity floating up).
