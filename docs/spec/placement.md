# U17 placement: block placement helpers, the trowel and enhanced ladders

Ports seven Quark 4.1-488 modules: ReacharoundPlacing, LockRotation, VariantSelector (experimental,
off by default, with its Hammer), Trowel, ReplaceScaffolding, LongRangePickBlock and
EnhancedLadders. Server logic runs for every client; HUD, keybinds, the radial menu and the
client-side movement tweaks need the Lepton client mod, and vanilla clients get commands/forms where
Quark's feature is driven from the client.

Quark 4.1-488 has no netherite trowel (only `quark:trowel`); none is added.

## 1. Modules covered

### ReacharoundPlacing (tweaks)
Right-clicking with a block while looking at nothing places the block "around the edge" the
player stands on: below the block under the player's feet (vertical), or in front of the edge the
player looks over (horizontal). A crosshair indicator (vertical / horizontal arrows) shows when a
reacharound placement is possible.

### LockRotation (tweaks)
A keybind (K) locks the orientation of every block the player places to the direction and half
(top/bottom) they were looking at when they pressed it; pressing it again in the same situation
unlocks. A HUD icon next to the crosshair shows the locked direction and half plus the key. The
first time a player locks, a one-time chat message explains the feature.

### VariantSelector (experimental, disabled by default)
A keybind (R) opens a radial menu listing the variants (slab, stairs, wall, fence, fence gate,
vertical slab) of the held block; picking one makes every placement of that block (and any other
block with that variant) place the variant instead. HUD (held block -> variant), tooltip variant
list, pick-block integration, optional conversion of dropped variant items and the optional
Hammer item that swaps placed blocks between variants.

### Trowel (tools)
The Trowel places a random block from the hotbar where it is used. Durability optional.

### ReplaceScaffolding (tweaks)
Right-clicking scaffolding with a block (not sneaking) replaces the far end of the scaffolding line
with the block and gives the scaffolding back.

### LongRangePickBlock (client)
Pick block (middle click) on nothing picks the block the player looks at up to 200 blocks away.

### EnhancedLadders (tweaks)
Free-standing ladders (a ladder can hang under a ladder of the same facing), right-clicking a ladder
with the same ladder drops a new one down to the bottom of the column, looking down while climbing
slides down faster, and opening a screen while on a ladder sneaks automatically.

## 2. Blocks and items

### Trowel

| id | name | stack | durability | recipe | other |
|---|---|---|---|---|---|
| `lepton:trowel` | Trowel / 抹泥铲 | 1 | 255 (only used when `maxDamage` > 0) | shaped `S  ` / ` II` (S stick, I iron ingot) -> 1 | smelting and blasting -> 1 iron nugget (0.1 xp); creative: equipment, next to shears |

Item tags: `lepton:trowel_blacklist` / `lepton:trowel_whitelist` (Quark's `quark:trowel_*` item
tags; Bedrock items can't be tagged from outside, so the lists are config keys `trowelBlacklist` /
`trowelWhitelist`, see §5). `quark:reacharound_able` (the trowel) becomes the built-in rule "the
trowel can reacharound".

### Hammer (VariantSelector)

| id | name | stack | recipe | condition |
|---|---|---|---|---|
| `lepton:hammer` | Hammer / 锤子 | 1 | shaped `III` / `ISI` / ` S ` (I iron ingot, S stick) -> 1 | recipe present only when `enableVariantSelector` and `enableHammer` |

Textures: `textures/items/trowel.png`, `textures/items/hammer.png`, original pixel art drawn by
`tools/textures/placement.ps1`.

### Block tags used
- Ladders (Quark `quark:ladders`): `minecraft:ladder` and every block tagged `lepton:ladder`
  (U6's variant ladders, U5's iron ladder).
- `cannot_replace_scaffolding` (Quark block tag): doors (`*_door`, `lepton:door` tag), beds,
  tall flowers (sunflower, lilac, rose bush, peony, pitcher plant), tall grass, large fern, barrier.

No blocks are added.

## 3. Mechanics

### ReacharoundPlacing
- Item check: main hand (Bedrock offhands can't hold blocks to place): a block item, or the trowel
  (Quark's `reacharound_able` tag), or an id in `whitelist`; never an id in `blacklist`.
- Only when the player's normal ray (eye, look vector, reach) hits no block (outline shapes, no
  fluids). Reach = the game's pick range for the player (Bedrock's equivalent of Java's block
  interaction range: creative and survival differ like vanilla).
- Vertical: only when pitch >= 0 (looking level or down). Re-cast the same ray from the eye moved
  up by `leniency` (0.5). If it hits block B, the target is B's position minus one in y, valid when
  `player.y - target.y > 1` and the target is air or replaceable; face = DOWN.
- Horizontal (when vertical fails): `dir` = the player's horizontal facing; re-cast from the eye
  moved back by `leniency` along `dir`. If it hits block B, the target is B + dir, valid when the
  target is air/replaceable and the distance between the hit point and
  `player.position - (look.x, -2, look.z)` is > 0.9; face = opposite(dir).
- Using the item in the air (Bedrock: the client sends "use item" without a block) places the item
  at the target as if the target's face was clicked (hit point = target centre + 0.5 y), through the
  normal game placement (events, permissions, creative keeps the stack). On success the block's
  place sound is played for everyone (volume (v+1)/2, pitch p*0.8 of the block's sound type). The
  game may refuse (protected area, no permission): nothing happens.
- Mod clients run the same target computation every client tick and draw the indicator while a
  target exists and the crosshair is not on a block (not in spectator, not with the HUD hidden).

### LockRotation
- Profile = (facing, half). On the key: if the crosshair hits a block face F at hit point h:
  facing = opposite(F), `half = |int((h.y - int(h.y)) * 2)|`, -1 for an up/down face,
  `1 - half` when h.y < 0. Otherwise facing = the nearest direction of the look vector, half -1.
  Pressing with the same profile as the current one clears it; anything else sets it. Only in game
  (no screen), not in spectator.
- Server: per-player profiles (memory, by UUID), dropped on logout and on `/quark reload`.
  The first time a player locks, the chat message `quark.misc.rotation_lock` with the key name in
  aqua; a per-player persistent flag (world DB `lp:placement:locked_once:<uuid>`) keeps it from
  appearing again.
- Placement: after any block placement by a player with a profile, the placed block is re-oriented
  as if it had been placed by clicking face `F = opposite(facing)` while looking along `facing`:
  - vanilla blocks: the block's own placement rule is evaluated again with that face, a click
    height from `half` (top 0.75 / bottom 0.25; with half -1 an up face places bottom, a down face
    top, a side face keeps the original half) and the player's rotation pointing along `facing`.
    This covers Java's FACING, HORIZONTAL_FACING (stairs opposite), AXIS, hopper FACING, slab type
    and stairs half rules. The result is only used when it is the same block type and can stay
    there (support), like Quark's `updateFromNeighbourShapes` check.
  - Lepton shapes: stairs `minecraft:cardinal_direction` = facing (horizontal F only), gates,
    doors, trapdoors `= F`; vertical slabs `lepton:type` = Quark's `VerticalSlabType.fromDirection(F)`
    (horizontal F only); `minecraft:vertical_half` from half (or F up/down); `minecraft:block_face`
    (pillars, buttons, U6 posts, custom facing blocks) = F; other custom blocks with a
    `minecraft:cardinal_direction` state = opposite(F) (Bedrock's trait value is the look direction).
    Applied after the shapes finished their own placement (next tick).
- ReplaceScaffolding and the Hammer apply the lock to the states they compute (as in Quark).
- Vanilla clients: `/quark lockrotation` toggles a profile computed on the server from the player's
  current look (the same rules), `/quark lockrotation clear` removes it; the command replies with
  the locked direction.

### VariantSelector (only with `enableVariantSelector`)
- Variant types (`variants.variantTypes`, default slab, stairs, wall, fence, fence_gate,
  vertical_slab; `!type` = shown but not scanned). For each registered block `ns:name`, each type
  `t` is looked up as `ns:name_t`, then with each `stripCandidates` suffix removed from `name`
  (`_planks`, `_wool`, `_block`, `s`), then aliases (`carpet=slab`, `pane=fence`: a failed
  `slab` search tries `_carpet`, a failed `fence` search tries `_pane`), then in each namespace of
  `testedMods` (Lepton: `lepton`, so `minecraft:stone_bricks` finds `lepton:stone_brick_vertical_slab`).
  `manualVariants` (`type,block,output`) add entries; `blacklist` excludes a block (`id`), a target
  (`=id`) or a pair (`id=id`). A block with variants is an "original"; each variant maps back to it.
  `printVariantMapToLog` logs the map.
- Saved variant per player ("" = original), server: world DB `lp:placement:variant:<uuid>`
  (Quark's persistent player data); the client keeps its own copy, sends changes, and the server
  sends it back on join (`placement:variant_restore`). Unknown variant names are ignored.
- Placement: when a player places a block item whose block has the saved variant, the variant's
  own item placement runs instead (its own orientation rules), consuming the held item.
- Keybind R (mod clients): with a block item that has variants (or the hammer: the looked-at block's
  original) opens the radial menu: segment 0 = the original at the bottom, then each visible
  variant type (missing ones dimmed); segments grow in (radius 50, animated), the hovered one
  (mouse farther than 8 from the centre) is cyan, the current one green. Releasing the key picks
  the hovered segment (or the original when released within 10 frames without hovering), plays the
  click sound and sends the choice.
- HUD (`showHud`): below the crosshair (y + 12; `alignHudToHotbar`: next to the hotbar), the held
  item, an arrow, and the item it will place, offset by `hudOffsetX/Y`; `showSimpleHud` draws only
  the result (`enableGreenTint` tints the hotbar-aligned frame); `smallerArrow` uses the small
  arrow, `variantSelectorRenderLikeCrossHair` draws the arrow like the crosshair (white, opaque)
  instead of translucent grey. Hidden when the held block and the result are the same.
- Tooltip (`showTooltip`): blocks with variants get "(SHIFT for Variants)", with Shift held
  "Variants:" and the list of variant names.
- Held item (`overrideHeldItemRender`): the first-person hand renders the variant that will be
  placed.
- Pick block: picking a variant of the held block selects that variant instead of picking; picking
  any variant block picks its original (if in the inventory or in creative) and selects the variant.
- `convertVariantItems`: dropped variant items turn into their originals.
- Hammer (`enableHammer`): using it on a block replaces the block with the saved variant of its
  original (or the original when the saved variant is ""), oriented by the placement rules and the
  rotation lock; same block: nothing.
- Vanilla clients: `/quark variant` opens a form with the variants of the held block (and
  "Original"); `/quark variant <name>` sets it directly.

### Trowel
- Use on a block: candidates = hotbar slots holding a block item (whitelist ids always, blacklist
  ids never). None: nothing. Otherwise a random candidate (uniform over slots, so duplicates raise
  the odds) is put in the hand and used on the clicked block/face exactly like a normal use; the
  result goes back to its slot and the trowel back to the hand. On success, with `maxDamage` > 0 the
  trowel takes 1 damage (the item has 255 durability; at 0 it is unbreakable). The last placed
  item is stored on the trowel (`LastStack` user data, item id) for the usage ticker.

### ReplaceScaffolding
- Right-click scaffolding (not sneaking) with a block item whose block is not scaffolding, has no
  block entity and is not in `cannot_replace_scaffolding`.
- Last-in-line: walk from the clicked scaffolding along the clicked face's direction while the next
  block is scaffolding; then up to `maxBounces` (1) times "bounce": from the end, for every direction
  of another axis whose neighbour is scaffolding, walk that line recursively and take the last
  direction's end (Quark compares distances against a value it never updates, so the last matching
  direction in the order down, up, north, south, west, east wins). If the line is empty, try up,
  the other horizontal directions, then down. If all are empty, the clicked block itself.
- The block's placement state at that position (rotation lock applied) must be able to stay there;
  it replaces the scaffolding. If scaffolding is above it and the new block's top face isn't sturdy,
  the scaffolding is put back and nothing happens. Otherwise: place sound, and in survival one item
  is used and a scaffolding item is given (dropped when the inventory is full).

### LongRangePickBlock
- Mod clients: pick block with the crosshair on nothing (air) or out of reach ray-traces 200 blocks
  (outline shapes, no fluids) and picks that block. Entities and real block hits are unchanged.
- The pick is done by the server (`placement:pick`, validated: within 200 blocks + 2 of the eye,
  loaded, not air): creative gets the block's item into the hotbar, survival moves it from the
  inventory to the hotbar, like vanilla pick block.

### EnhancedLadders
- Freestanding (`allowFreestanding`): a ladder survives (and may be placed) without a sturdy wall
  when the block above is a ladder with the same facing. Placement into such a spot picks the
  facing as Java (`getNearestLookingDirections`, horizontal ones, facing = opposite). A ladder in
  front of another ladder (clicked face = that ladder's facing) can't be placed. Breaking the
  ladder above makes free-standing ladders below fall one tick after another (like bamboo).
  `minecraft:ladder` natively; `lepton:ladder` blocks via U6 (it reads this config).
- Dropping down (`allowDroppingDown`): using a ladder item on a ladder of the same block finds the
  lowest ladder of the column; the block below it, if air or water, gets a copy of it (waterlogged
  in water) when the copy can survive there; ladder place sound; survival uses one item. The click
  is consumed whenever the clicked block is that ladder.
- Sliding (`allowSliding`, mod clients): while on a ladder (or scaffolding while sneaking), with no
  movement input, pitch > 70, not jumping, not flying, and the block below is a ladder, the player
  moves down by `fallSpeed` (-0.2) per tick when nothing blocks that move.
- Inventory sneak (`allowInventorySneak`, mod clients): while a screen is open and the player is on
  a ladder (not scaffolding), not flying, not on the ground and not "looking down with no forward
  input" (pitch > 70), sneaking is forced so the player stays on the ladder.

## 4. Screens

- **Reacharound indicator** (mod clients, HUD): 16x16 at the centre of the screen, vertical
  arrows for a vertical target, horizontal arrows for a horizontal one, drawn white like the
  crosshair. Vanilla clients: none (client-only in Quark); the placement itself works for them.
- **Rotation lock indicator** (mod clients, HUD): at (centre x + 20, centre y - 8): an arrow icon
  for the locked direction (6 icons), the half icon (top/bottom) next to it when half >= 0, and
  `[K]` (the bound key) at x + 20 when `showKeybind`. `lockRotationRenderLikeCrossHair` false:
  translucent (alpha 0.5). Vanilla clients: `/quark lockrotation` + chat replies.
- **Variant selector** (mod clients): the radial custom screen described above, HUD and tooltip.
  Vanilla clients: `/quark variant` form (SimpleForm: one button per variant with its name, the
  current one marked).
- No container screens.

## 5. Config

Section `placement` (`config/placement.json`), version 1.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enableReacharoundPlacing` | bool | true | module |
| `leniency` | double | 0.5 | 0..1, how far the second ray is shifted |
| `whitelist` | string[] | [] | extra item ids that can reacharound |
| `blacklist` | string[] | [] | item ids that never reacharound |
| `enableLockRotation` | bool | true | module |
| `lockRotationRenderLikeCrossHair` | bool | true | indicator like the crosshair (opaque) |
| `showKeybind` | bool | true | `[key]` next to the indicator |
| `enableVariantSelector` | bool | false | module (experimental) |
| `convertVariantItems` | bool | false | dropped variant items become their originals |
| `enableHammer` | bool | false | the Hammer recipe/use |
| `showTooltip` | bool | true | variant tooltip |
| `alignHudToHotbar` | bool | false | HUD next to the hotbar |
| `showSimpleHud` | bool | false | only the result item |
| `showHud` | bool | true | variant HUD |
| `enableGreenTint` | bool | true | green frame in the simple hotbar HUD |
| `overrideHeldItemRender` | bool | true | hand shows the variant |
| `hudOffsetX` / `hudOffsetY` | int | 0 | HUD offset |
| `variantSelectorRenderLikeCrossHair` | bool | true | arrow like the crosshair |
| `smallerArrow` | bool | false | small arrow |
| `variants` | object | see below | VariantsConfig |
| `enableTrowel` | bool | true | module |
| `maxDamage` | int | 0 | >= 0; 0 = unbreakable, otherwise 1 damage per placement |
| `trowelBlacklist` | string[] | `["minecraft:air"]` | Quark's `trowel_blacklist` tag |
| `trowelWhitelist` | string[] | [] | Quark's `trowel_whitelist` tag |
| `enableReplaceScaffolding` | bool | true | module |
| `maxBounces` | int | 1 | how often the line search may turn |
| `enableLongRangePickBlock` | bool | true | module |
| `enableEnhancedLadders` | bool | true | module |
| `fallSpeed` | double | -0.2 | <= 0, slide speed per tick |
| `allowFreestanding` | bool | true | |
| `allowDroppingDown` | bool | true | |
| `allowSliding` | bool | true | |
| `allowInventorySneak` | bool | true | |

`variants`:

```json
{
  "variantTypes": ["slab", "stairs", "wall", "fence", "fence_gate", "vertical_slab"],
  "testedMods": ["lepton"],
  "printVariantMapToLog": false,
  "aliases": ["carpet=slab", "pane=fence"],
  "stripCandidates": ["_planks", "_wool", "_block", "s"],
  "manualVariants": ["fence_gate,minecraft:oak_planks,minecraft:fence_gate"],
  "blacklist": ["minecraft:snow", "minecraft:bamboo", "minecraft:bamboo_block"]
}
```

`testedMods` is Quark's list of mod namespaces (`quark`, `everycomp`, `v_slab_compat`) mapped to
Lepton's namespace. The manual entry maps Bedrock's oak fence gate (`minecraft:fence_gate`, an id
the suffix rules can't find).

## 6. Strings

| Key | en_US |
|---|---|
| `quark.misc.rotation_lock` | You have enabled Rotation Lock. Any blocks you place will be oriented in the direction you were looking. Press [{}] to change or reset it. This message won't appear again. |
| `quark.keybind.lock_rotation` | Rotation Lock |
| `quark.keybind.variant_selector` | Variant Selector |
| `quark.misc.variant_tooltip_hold_shift` | (SHIFT for Variants) |
| `quark.misc.variant_tooltip_header` | Variants: |
| `quark.jei.hint.trowel` | Randomly places a block from your hotbar when used. You can have the same block in multiple slots to increase its odds. |
| `quark.jei.hint.ladder_dropping` / `_freestanding` / `_sliding` / `_sneak` | Quark's hint texts (joined with spaces, enabled options only) |
| `quark.jei.hint.minecraft.scaffolding` | Right clicking the Scaffolding with any block will replace the furthermost piece of Scaffolding with the block. Sneak to use the normal placing. |
| `lp.placement.rotation_lock_command` | the one-time message for vanilla clients (`/quark lockrotation` instead of a key) |
| `lp.placement.lock_set` / `lp.placement.lock_cleared` / `lp.placement.lock_disabled` | command replies |
| `lp.placement.dir.<down/up/north/south/west/east>`, `lp.placement.half.<top/bottom>` | direction names |
| `lp.placement.variant_form_title` / `_content` / `_original` / `_none` / `_set` / `_unknown` / `_disabled` | the vanilla variant form and replies |
| `lp.placement.variant.<type>` | variant type names (Slab, Stairs, Wall, Fence, Fence Gate, Vertical Slab) |

Names: `item.lepton:trowel` Trowel, `item.lepton:hammer` Hammer, `key.lepton.lock_rotation`
Rotation Lock, `key.lepton.variant_selector` Variant Selector (RP `texts.d/*/placement.lang`).

## 7. Bedrock differences / not ported

- Offhand: Bedrock can't place blocks from the offhand; reacharound uses the main hand only.
- Reach: the game's pick range (Bedrock) instead of Java's 4.5/5 block interaction range.
- Tag-driven lists (`reacharound_able`, `trowel_*`, `ladders`, `cannot_replace_scaffolding`) are
  built in or config lists: Bedrock items have no data-pack tags.
- Indicators are drawn with the UI toolkit's rectangles (original pixel shapes, white like the
  crosshair); Java's inverted "crosshair" blend isn't available.
- Variant tooltip lists the variant names instead of item icons (tooltip lines are text).
- Rotation lock on vanilla blocks re-evaluates Bedrock's own placement rule for the locked face
  instead of setting Java properties, so every vanilla block orients "as if placed looking that
  way"; a few blocks whose Java FACING differs from that rule (e.g. observers) follow Bedrock.
- The client-predicted block appears in its natural orientation for a moment before the server's
  locked/variant block replaces it.
- Sliding and inventory sneak move the player on the mod client (as Quark does on the Java client);
  vanilla clients keep normal ladder physics (movement is client-driven on Bedrock).
- Ladders falling "like bamboo" use a one-tick delay per ladder like Quark's scheduled tick.
- JEI hints become tooltip lines on mod clients (trowel, ladders, scaffolding).
- Long-range pick on a server without Lepton sends the vanilla pick request for the far block
  (the server may refuse it).

## 8. Tests

Selftests (dx 1700..1799):

| Name | dx | Checks |
|---|---|---|
| `placement.config` | - | defaults match Quark |
| `placement.reacharound_vertical` | 1700 | target below the edge from a simulated player looking down; placement by use-in-air |
| `placement.reacharound_horizontal` | 1705 | horizontal target, face, and no target when looking at a block |
| `placement.reacharound_items` | - | block items yes, sticks no, whitelist/blacklist, trowel |
| `placement.lock_profile` | - | profile from hit faces / look, half rule, toggle |
| `placement.lock_vanilla` | 1710 | locked placement of a log (axis), stairs (direction + half), furnace |
| `placement.lock_lepton` | 1715 | locked placement of a Lepton stairs / vertical slab |
| `placement.lock_message` | - | one-time message only once |
| `placement.variants_map` | - | stone bricks -> slab/stairs/wall/vertical slab, oak planks -> fence gate, blacklist |
| `placement.variant_place` | 1720 | saved "stairs" places stairs from a stone brick item |
| `placement.variant_net` | - | client message saves, restore message on join |
| `placement.hammer` | 1725 | the hammer swaps a block to its variant |
| `placement.convert_items` | 1730 | dropped stairs item becomes the original |
| `placement.trowel` | 1735 | places one of the hotbar blocks, damage with maxDamage |
| `placement.scaffolding` | 1740 | replaces the top of a scaffolding tower, gives scaffolding |
| `placement.scaffolding_bounce` | 1745 | bounce rule |
| `placement.pick` | 1750 | server pick (creative / survival hotbar move) |
| `placement.ladder_freestanding` | 1755 | ladder stays under a ladder, falls when the top one is broken |
| `placement.ladder_drop` | 1760 | ladder item on ladder drops one to the bottom (and into water) |
| `placement.disabled` | 1765 | disabled modules do nothing |

E2E: server scenario `tools/e2e/server/scenarios/placement.ps1` runs `placement.*`. Client scenario
`tools/e2e/client/scenarios/placement.ps1` (screenshots): reacharound indicator, rotation lock HUD,
variant radial menu and HUD, long-range pick, trowel item icon.
