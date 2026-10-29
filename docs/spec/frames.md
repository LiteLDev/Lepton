# U24 frames: glass item frames, dyeable item frames, banner layers, map washing, sign editing

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules
`content/building/module/GlassItemFrameModule` (+ `building/entity/GlassItemFrame`,
`building/client/render/entity/GlassItemFrameRenderer`), `content/tweaks/module/DyeableItemFramesModule`
(+ `tweaks/entity/DyedItemFrame`, `tweaks/client/render/entity/DyedItemFrameRenderer`),
`content/tweaks/module/MoreBannerLayersModule` (+ the mixins `LoomMenuMixin`, `LoomScreenMixin`,
`BannerDuplicateRecipeMixin`, `BannerItemMixin`) and `content/tweaks/module/MapWashingModule`.
Behaviour only; no Quark code or assets are used.

## 1. Modules covered

### GlassItemFrame (building, enabled by default)
Two items, the Glass Item Frame and the Glow Glass Item Frame. They hang like item frames. An empty
glass frame shows a glass frame; once it holds an item the frame disappears and only the item is
shown, `itemRenderScale` (1.5) times as large as in an item frame and closer to the wall. Banners
show their flag over the whole frame, shields are shown 2.67 times larger. The glow variant shows
its contents at full brightness. A glass frame can be placed against any sign. When it hangs in
front of (or behind) a standing sign or a ceiling hanging sign at the sign's rotation, on the front
of a wall sign, or against a wall hanging sign, it stays even without a solid block and its item is
drawn small (0.32) and flush on the sign. Using a filled glass frame (not sneaking, the item is no
banner) uses the block behind it if that block has a block entity: a glass frame on a chest opens
the chest. With `glassItemFramesUpdateMaps` a filled map in a glass frame keeps updating its map
data around the frame (every 100 ticks, or every tick with `glassItemFramesUpdateMapsEveryTick`), as
if a player held it there. Everything else is an item frame's behaviour (insert one item, rotate in
8 steps, punch to drop the item, punch again to break, drops, sounds).

### DyeableItemFrames (tweaks, enabled by default)
Item frames and glow item frames can be dyed like leather armour. A dyed frame item places a dyed
item frame: the frame (wood and background) is tinted with the dye colour; with a map inside it uses
the map frame shape. It keeps the colour when broken (drops the dyed frame item) and otherwise
behaves like an item frame (a dyed glow frame like a glow item frame).

### MoreBannerLayers (tweaks, enabled by default)
Raises the banner layer limit from 6 to `layerLimit` (1..16, default 16) in the loom, the banner
copy recipe and the banner tooltip.

### MapWashing (tweaks, enabled by default)
Using a filled map on a water cauldron turns the whole stack into blank maps and lowers the water
by one level.

### Sign editing (no Quark module)
Quark 4.1-488 has no SignEditing module: vanilla Java 1.20 made signs editable (use a sign to edit
its front or back text, sneak to place items against it; waxed signs cannot be edited) and Quark
dropped its module. Bedrock already behaves the same way, so there is nothing to port; see §7.5 for
the parity check.

## 2. Blocks and items

| Id | English name | zh_CN | Kind | Recipe | Drops | Creative |
|---|---|---|---|---|---|---|
| `lepton:glass_item_frame` | Glass Item Frame | 玻璃物品展示框 | block + item (`minecraft:block_placer`) | shaped `###`/`#F#`/`###` (# glass pane, F item frame) -> 2 | itself (+ the held item) | Items, after the glow item frame |
| `lepton:glowing_glass_item_frame` | Glow Glass Item Frame | 荧光玻璃物品展示框 | block + item | shapeless glow ink sac + glass item frame -> 1 | itself (+ the held item) | Items, after the glass item frame |
| `lepton:dyed_item_frame` | Item Frame | 物品展示框 | item (`minecraft:dyeable`, colour in the stack) | none (dyed in a cauldron, see 3.2) | - | hidden (like Java's dyed frames, which are the vanilla item with a colour) |
| `lepton:dyed_glow_item_frame` | Glow Item Frame | 荧光物品展示框 | item (`minecraft:dyeable`) | none | - | hidden |

Glass frame blocks: states `minecraft:block_face` (trait `minecraft:placement_position`: the face the
frame was put on, i.e. the direction it faces) and `lepton:filled` (bool). Selection box 12x12x1 px
against the support (Java's 12x12x1 frame), no collision, destroy time 0 (breaks with one punch like
an item frame), wood-like `itemframe` sound, not flammable, light passes through. Empty: a glass
frame model (original glass-frame texture); filled: no model (the display entity shows the item).
Loot: itself. Glass pane = `minecraft:glass_pane` (Quark's tag `c:glass_panes/colorless`).
Tags: `lepton:glass_item_frame` on both blocks.

Entities (behaviour + client entity, not spawnable, no physics, invulnerable, tiny hit box so clicks
reach the block behind them):
- `lepton:glass_frame` ("Glass Item Frame", Quark's entity id): shows the item of a filled glass
  frame (held-item rendering of a display bone). Properties `lepton:facing` (0..5, Java order down,
  up, north, south, west, east), `lepton:rotation` (0..7), `lepton:kind` (0 item, 1 hand-held tool,
  2 block, 3 banner, 4 shield, 5 map), `lepton:sign` (0..6, Quark's SignAttachment),
  `lepton:sign_rotation` (0..15), `lepton:scale` (float, `itemRenderScale`), `lepton:shiny` (bool).
- `lepton:dyed_item_frame` ("Dyed Item Frame", Quark's entity id): the tinted frame drawn over a
  vanilla item frame block. Properties `lepton:facing`, `lepton:color` (0xRRGGBB), `lepton:map`
  (bool, map frame shape).

Textures (original, `tools/textures/frames.ps1`): glass item frame item, glow glass item frame item,
glass frame block texture, grey dyed frame item icons (`dyed`: tinted by the item colour; `default`:
untinted), grey dyed frame entity texture.

## 3. Mechanics

### 3.1 Glass item frames
- **Placing.** Using a glass frame item on a block face puts the frame block in front of that face
  (native block placement, `minecraft:block_face`) when the clicked block holds a hanging frame by
  Java's rule (HangingEntity#survives): a solid block in Java's sense (BlockState#isSolid: the
  collision box is at least 0.729 on average or a full block high: stone, chests, slabs, fences,
  glass, leaves; not carpets, flowers, liquids), a repeater / comparator, or a sign (standing, wall
  or hanging; Java signs are `forceSolidOn`). Otherwise nothing is placed. Sneak to place on blocks
  that react to use (chests, signs), as with every block.
- **Using** a glass frame (`PlayerInteractBlockEvent` on the frame block, server side):
  1. Filled, not sneaking, the item is no banner, the block behind (the support) has a block entity:
     the support block is used instead (chest, barrel, furnace, sign, ...).
  2. Empty frame, item in hand: one item goes in (`block.itemframe.add_item`), the hand loses one
     unless creative. Empty hand on an empty frame: nothing.
  3. Filled: the item rotates by 45 degrees (rotation 0..7, `block.itemframe.rotate_item`; a map
     shows rotation `% 4 * 90` degrees as in Java).
- **Punching** (`PlayerDestroyBlockEvent` on the frame block): a filled frame drops its item
  (`block.itemframe.remove_item`; nothing dropped for creative players) and stays; an empty frame
  breaks (`block.itemframe.break`) and drops itself unless creative.
- **Explosions / pistons / other removal** of the block: the frame drops itself (loot) and its item.
- **Survival.** When the support block changes (`BlockChangedEvent` next to a tracked frame) and on a
  100-tick sweep: if the support no longer holds a frame (same rule as placing), the frame breaks and
  drops itself and its item.
- **Sign attachment** (recomputed every 100 ticks and after each use, Quark's rule): support =
  standing sign or ceiling hanging sign (Bedrock: hanging sign with `hanging` = true; rotation from
  `ground_sign_direction`, or `facing_direction` when not `attached_bit`): ours = the yaw of the
  frame's back direction (south 0, north 180, west -90, east 90 as Quark's table, vertical frames
  never attach), sign = rotation / 16 x 360 wrapped to (-180, 180]; diff = ours - sign, |diff| (minus
  0.01 when diff < 0): < 45 -> STANDING/HANGING_IN_FRONT, [135, 225) -> STANDING/HANGING_BEHIND.
  Wall sign facing the frame's direction -> WALL_SIGN. Wall hanging sign (`hanging` = false) facing
  the frame's direction or its opposite -> HANGING_FROM_WALL. The display draws the item at 0.32
  scale on the sign face with Quark's offsets.
- **Display.** Filling a frame spawns its `lepton:glass_frame` display entity at the frame (held
  item = the frame item, name tag = the item's custom name); emptying or breaking despawns it. The
  block's `lepton:filled` state hides the glass frame model. The 100-tick sweep respawns a missing
  display and despawns orphans.
- **Maps** (`glassItemFramesUpdateMaps`, `glassItemFramesUpdateMapsEveryTick`): every 100 ticks
  (every tick with the second option) a filled, unlocked map in a loaded glass frame is updated at
  the frame's position with `MapItem::blockTick` (what Bedrock's item frames use).
- **Data.** Every glass frame is tracked in the world DB under
  `lp:frames:glass:<dim>:<x>:<y>:<z>` (`{"item": snbt, "rotation": n, "display": entityId}`);
  frames found without a record (placed by commands or structures) get one on first use.

### 3.2 Dyed item frames
- **Dyeing** (Bedrock's way to dye leather armour is the cauldron; Bedrock has no crafting dye
  recipe for it): using an item frame or glow item frame on a cauldron with dyed water (water, a dye
  colour, level > 0) turns one frame into `lepton:dyed_item_frame` / `lepton:dyed_glow_item_frame`
  with the water's colour (mixed dyes mix as in vanilla), lowers the water by one level (2 of 6 fill
  levels, Java's one of three) and plays `cauldron.dyearmor`. A dyed frame on dyed water takes the new
  colour. A dyed frame on plain water is washed back into the vanilla frame (`cauldron.cleanarmor`,
  one level). Sneaking places the frame against the cauldron instead, as with leather armour.
- **Placing** a dyed frame item on a block face puts a vanilla `minecraft:frame` /
  `minecraft:glow_frame` block (with `facing_direction`) where Bedrock allows one, plays
  `block.itemframe.place`, uses one item unless creative, spawns the `lepton:dyed_item_frame` overlay
  entity and records `lp:frames:dyed:<dim>:<x>:<y>:<z>` (`{"color": rgb, "glow": bool, "overlay": id}`).
  Holding, rotating, punching, comparators and map display are the vanilla frame's.
- **Overlay.** Tints the frame shape (border and background) with the colour; uses the map frame
  shape while the frame holds a map (`item_frame_map_bit`, checked every 10 ticks).
- **Breaking.** When the frame block disappears (any cause) the overlay is despawned, and the
  `minecraft:frame` / `minecraft:glow_frame` item that the block dropped next to it (within 1.5
  blocks, in the same or the next tick) becomes the dyed frame item of the recorded colour.

### 3.3 MoreBannerLayers
Bedrock draws at most 6 banner layers (the banner shader takes 6 pattern colours,
`BannerConstants`; a banner with more layers shows only the first 6, see §7.3), so more than 6
layers would be invisible. The effective limit is `min(layerLimit, 6)`: the loom
(`CraftHandlerLoom::_handleCraftAction`, server side) refuses to add a pattern to a banner that
already has that many layers, and so does the crafting-table pattern recipe
(`BannerAddPatternRecipe::matches`). With the default 16 this is vanilla Bedrock's 6.

### 3.4 MapWashing
Using a `minecraft:filled_map` (any count) on a cauldron with plain water (level > 0, no dye, no
potion; Java only knows plain water) replaces the held stack with as many `minecraft:empty_map`,
lowers the water by one level (2 fill levels) and plays `cauldron.cleanbanner`. Not while sneaking
(Java skips block interactions for a sneaking player holding an item). Other cauldron contents
(dyed water, potions, lava, powder snow, empty): no effect.

### 3.5 Disabled modules
`enableGlassItemFrame = false`: the two glass frame recipes are removed, using a glass frame item
places nothing, existing frames keep working as plain blocks (the item stays shown). Quark removes
the items; Bedrock cannot unregister pack items at run time.
`enableDyeableItemFrames = false`: cauldrons no longer dye frames, dyed frame items place nothing.
`enableMoreBannerLayers = false`: vanilla limit. `enableMapWashing = false`: no washing.

## 4. Screens
None. Glass frames on containers open the container's own screen (vanilla, also for vanilla
clients).

## 5. Config
Section `frames` (`config/frames.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableGlassItemFrame` | bool | true | GlassItemFrame module |
| `glassItemFramesUpdateMaps` | bool | true | glass frames update the maps they hold |
| `glassItemFramesUpdateMapsEveryTick` | bool | false | Set to true for faster map updates. Default is every 3s (Quark's code: every 100 ticks) |
| `itemRenderScale` | double | 1.5 | The scale at which items render in the Glass Item Frame. To match the vanilla Item Frame size, set to 1.0 |
| `enableDyeableItemFrames` | bool | true | DyeableItemFrames module |
| `enableMoreBannerLayers` | bool | true | MoreBannerLayers module |
| `layerLimit` | int | 16 | 1..16; effective limit `min(layerLimit, 6)` on Bedrock |
| `enableMapWashing` | bool | true | MapWashing module |

`itemRenderScale` changes apply to displays on the next sweep (100 ticks) after `/quark reload`.

## 6. Strings
No mod strings (Quark's only strings are the JEI hints, not ported per SPEC §8; their content is in
§1). Names in RP `texts.d`: `tile.lepton:glass_item_frame.name`, `item.lepton:glass_item_frame.name`
(and the glowing one), `item.lepton:dyed_item_frame.name` = "Item Frame",
`item.lepton:dyed_glow_item_frame.name` = "Glow Item Frame", `entity.lepton:glass_frame.name` =
"Glass Item Frame", `entity.lepton:dyed_item_frame.name` = "Dyed Item Frame".

## 7. Bedrock differences / not ported

1. **Frames are blocks.** Bedrock's item frames are blocks with a block entity, and custom blocks
   cannot have one. Glass frames are a custom block (shape, placement, selection, drops) plus a
   display entity for the item; dyed frames are vanilla frame blocks with a tinted overlay entity,
   so they keep every native frame behaviour (maps, comparators, rotation). Like Bedrock frames
   (and unlike Java's entities) they occupy their block cell.
2. **Glass frame item rendering** is Bedrock's held-item rendering on the display entity's item
   bone, oriented and scaled per item kind; it is the closest Bedrock can draw an arbitrary item
   for every client. A **map** in a glass frame is drawn as the map item, not the map picture:
   Bedrock draws map pictures only in vanilla item frame blocks and in the first-person hand
   (`ItemFrameRenderer` / map renderer are not reachable from an entity or a custom block). Glow
   glass frames cannot force full-bright item lighting (held items use the entity's light).
3. **More than 6 banner layers** cannot be shown on Bedrock (banner shader limit; minecraft.wiki:
   "Banners can display only up to 6 layers, even if more layers are added using external
   editors"), so the module's limit is capped at 6 (see 3.3); lower limits work as in Quark. The
   banner copy recipe and the tooltip have no 6-layer cap on Bedrock (the tooltip lists every layer),
   so Quark's patches to them have nothing to change.
4. **Comparators** do not read glass frames: Bedrock comparators read `BlockType::hasComparatorSignal`
   / `getComparatorSignal`, which custom blocks cannot provide and which is folded code (cannot be
   hooked safely). Dyed frames are vanilla frames and keep their comparator output.
5. **Sign editing parity.** Java 1.20+ and Bedrock: using a sign opens the editor for the side you
   face (front or back), dye / glow ink / honeycomb (wax) apply to the sign instead, waxed signs
   cannot be edited, sneak-use places blocks and items against signs. Both behave the same; nothing
   to port.
6. **Dyeing** uses the cauldron (Bedrock's leather-armour dyeing) instead of Java's crafting grid
   (Zeta's dye recipe); colours mix the same way (vanilla cauldron dye mixing). Dyed frame items are
   separate items (`lepton:dyed_item_frame`) because Bedrock's vanilla frame item cannot carry a
   colour; they are named and behave like the vanilla frames.
7. **Pick block** on a glass frame gives the glass frame item (the block's item), not the held item;
   on a dyed frame it gives the vanilla frame (Bedrock's block pick).
8. **Sounds.** Dyed frames use the vanilla frame block's sounds. (Quark's DyedItemFrame returns the
   remove-item sound for every non-glow sound: a Quark bug, not reproduced.) Bedrock has one sound
   set for both frames (`block.itemframe.*`).
9. Disabled GlassItemFrame keeps the (unobtainable) items registered (see 3.5).

## 8. Tests
Selftests `frames.*`, dx 2400..2499 (14 tests):
`frames.config`, `frames.sign_rule` (Quark's attachment rule), `frames.recipes` (incl. removal when
disabled), `frames.glass_use` (real placement, insert, display entity + properties, rotate, punch,
removal drops), `frames.glass_place_rules` (floor frame, disabled module), `frames.glass_survive`,
`frames.glass_container` (frame on a chest opens it; sneaking rotates), `frames.glass_sign`
(standing front/behind, wall sign, ceiling hanging, wall hanging), `frames.glass_maps` (sweep and
every-tick updates, option off), `frames.dye` (cauldron dyeing, re-dyeing, sneaking, disabled),
`frames.wash` (dyed frame washing, map washing, dyed water, emptying), `frames.dyed_frames` (real
placement, overlay properties, glow frame, dyed drop on break, disabled), `frames.banner_limit`, `frames.gallery` (the scene of the client screenshots).
E2E: `tools/e2e/server/scenarios/frames.ps1` (all selftests + log lines);
`tools/e2e/client/scenarios/frames.ps1` (a wall of glass frames with an item, a block, a tool, a
banner, a shield and a map, glow frames, a frame on standing/wall/hanging signs, and dyed frames of
several colours with and without a map: screenshots `frames-glass.png`, `frames-signs.png`,
`frames-dyed.png`).
