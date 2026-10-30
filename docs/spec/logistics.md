# U11 logistics: chutes, copper pipes, feeding troughs and storage crates

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules
`content/automation/module/ChuteModule`, `content/automation/module/FeedingTroughModule`,
`addons/oddities/module/PipesModule`, `addons/oddities/module/CrateModule` and their block,
block entity, menu and screen classes. Behaviour only; no Quark code or assets are used.

## 1. Modules covered

### Chute (automation, enabled by default)
A wooden funnel block. Hoppers, droppers (and pipes) can put items into it from any side but the
bottom; every item put in drops straight out below it, without motion, centred under the block. If
the block below is a pipe, the item goes into the pipe's top instead. It takes nothing while it is
powered by redstone or when the space below is blocked (see 3.1). It never holds items itself.

### Pipes (oddities, enabled by default)
Copper pipes and encased copper pipes move items. Items are put in by hoppers, droppers, chutes and
other pipes (any side), or are absorbed from item entities moving into an open end. Each item takes
`pipeSpeed * 2` ticks to cross a pipe; half way it picks its exit: down first, then straight on, then
a random side, then up; a full target makes it turn back. Pipes output into any container next to
them (through that container's side) and shoot items out of open ends. A redstone signal disables
a pipe (it accepts and absorbs nothing, ejects items slowly and shows red dust). More than
`maxPipeItems` items break it. A stick re-checks the connections of a pipe network; glass encases a
copper pipe (keeping its items).

### FeedingTrough (automation, enabled by default)
A 9-slot container. Breedable animals within `range` look for it (chance `lookChance` x 20 per
tick), walk to it while they can see it, eat one of their breeding items from it and, with
`loveChance`, fall in love, unless more than `maxAnimals` animals are around. Breeding caused by a
trough gives no XP. The trough shows its contents (`full`) and makes falls onto a full trough
softer.

### Crate (oddities, enabled by default)
The Storage Crate holds `maxItems` items in total, whatever their stack sizes (640 sticks or 640
swords). Its screen shows 6 rows of 9 stacks and scrolls by rows; the count "`total/maxItems`" is
shown. Hoppers and pipes insert into it (up to the cap) and hoppers below extract from it. Breaking
it spills its contents (stacks merged).

## 2. Blocks and items

| Id | English name | zh_CN | States | Recipe | Drops | Creative |
|---|---|---|---|---|---|---|
| `lepton:chute` | Chute | 溜槽 | `lepton:enabled` (bool, default `true`) | shaped `WWW`/`SWS`/` S ` (W any planks, S stick) -> 1 | itself | Items, redstone group |
| `lepton:pipe` | Copper Pipe | 铜管道 | `lepton:conn_<down/up/north/south/west/east>` (int 0..3, see 3.2) | shaped `I`/`G`/`I` (copper ingot, glass) -> 6 | itself | Items, redstone group |
| `lepton:encased_pipe` | Encased Copper Pipe | 铜管道玻璃箱 | same as the pipe | shapeless pipe + glass -> 1; shapeless encased pipe -> 1 pipe | itself | Items, redstone group |
| `lepton:feeding_trough` | Feeding Trough | 饲料槽 | `lepton:full` (bool) | shaped `#W#`/`###` (# any planks, W wheat) -> 1 | itself (+ contents) | Items |
| `lepton:crate` | Storage Crate | 板条箱 | `lepton:open` (bool) | shaped `IWI`/`WCW`/`IWI` (iron ingot, any planks, chest) -> 1 | itself (+ contents spilled) | Items |

Block properties (Quark's): chute strength 2.5, wood sound, flammable (fire spread 5, flammability
20), axe; pipes strength 3 / resistance 10, copper (glass for encased) sound, pickaxe, the copper
pipe can be waterlogged; trough strength 0.6, wood, axe; crate as a barrel (2.5, wood, axe). Chute,
trough and pipes let light through (light dampening 0), as Quark's non-occluding shapes.
Textures are original pixel art (`tools/textures/logistics.ps1`); the trough's full state shows the
vanilla hay block texture as its contents, the encased pipe the vanilla glass texture.

Tags: `lepton:pipes` (Quark's `quark:pipes` block/item tag) on both pipes; `lepton:logistics` on all
blocks of the unit.

Entity: `lepton:pipe_item` (behaviour + client entity, no physics, invulnerable, not spawnable):
the display of an item travelling in a pipe.

Sounds: `lepton.pipe.shoot` (pipe ejects), `lepton.pipe.pickup` (pipe absorbs), original sounds
synthesised by `tools/pack/logistics.ps1`.

## 3. Mechanics

### 3.1 Chute
- Port (see 3.5): one slot, always empty; accepts through every face but the bottom.
- `canDropItem`: the chute is enabled and the block below is air, a grate (`lepton:grate`, U5), a
  pipe, has no collision, or its collision box is at most 1 block high and stays out of the
  clearance box (x/z 0.25..0.75, y 0.5..1.0 of the block below).
- An item put in drops at (x + 0.5, y - 0.5, z + 0.5) with zero motion, or enters the pipe below
  through its top face.
- `lepton:enabled` = not powered; recomputed one tick after any neighbouring block changes.

### 3.2 Pipes
- Connection kinds per face (state value): 0 none, 1 pipe (neighbour is a pipe), 3 terminal
  (neighbour has a container: block entity container or a Lepton port), 2 opening (no
  container/pipe on this side, the opposite side is a pipe or terminal and no perpendicular side
  is). Copper pipes show arms for pipe/terminal sides and flares on openings and terminals; encased
  pipes show arms for every connected side, the glass and the flares. Collision of a copper pipe =
  centre (5..11) plus the solid arms. States are recomputed one tick after a neighbour changes;
  using a stick re-checks the whole connected network of that pipe kind.
- Item speed: `effectivePipeSpeed = pipeSpeed * 2` ticks per pipe. At tick
  `effectivePipeSpeed / 2 - 1` the exit is chosen (Quark's `getTargetFace`): down unless it came in
  from below (or was sent back from below) and the target fits; straight on for horizontal travel;
  the two other horizontal sides in random order (per-item seeded RNG); up; the face it bounced from;
  else none. "Fits": an enabled pipe, or a container that takes at least one of the items through
  that face.
- At `effectivePipeSpeed` ticks the item leaves: into the next pipe, or into the container (the rest
  bounces back with the face it came from as "backlog"), or bounces back when nothing was taken.
- An item without an exit is dropped where it came from: shot out with motion 0.5 (0.125 when the
  pipe is disabled) in its travel direction, position shifted 0.4 (0.7 for encased pipes) towards
  the exit, the shoot sound (pitch 1, 0.025 when disabled) and a `projectile_shoot` vibration.
- Open ends of enabled pipes absorb item entities in the 0.5-block box in front of them whose motion
  points into the pipe (Java `Direction.getNearest`; a motionless item counts as moving north),
  with the pickup sound and a `projectile_land` vibration.
- More than `maxPipeItems` (0 = no limit) items in one pipe: the pipe breaks (drops itself and all
  items).
- Disabled pipes (redstone next to them) accept nothing, absorb nothing, show red dust every 10
  ticks; items already inside keep moving.
- Breaking a pipe drops every item inside. Glass used on a copper pipe (`enableEncasedPipes`) turns
  it into an encased pipe with the same items, uses one glass (not in creative), glass sound.
- Items are shown by `lepton:pipe_item` entities holding the item (`renderPipeItems`), moving from
  the entry face through the centre to the exit face, turning 4 degrees per tick.
- Persistence: `lp:logistics:pipe:<dim:x:y:z>` (items with ticks, faces, seed), `lp:logistics:pipes`
  (index of pipe positions, for open-end pickup).

### 3.3 Feeding trough
- Container: 9 slots, stored by `game/VirtualContainer`, opened as the vanilla dispenser screen,
  titled "Feeding Trough". Hoppers/pipes insert and hoppers extract (port, 3.5). `lepton:full` is
  true while any slot holds an item.
- Every tick, for every loaded trough with items, breedable animals within `range` that can fall in
  love (adult, not in love, no breed cooldown) and have no trough yet roll `lookChance * 20`; on
  success they take the closest trough in `range` holding one of their breeding items.
- Per animal (Quark's `TroughPointer`): it gives up after 400 ticks; while it is not on cooldown and
  sees the trough (line from its eyes to the trough floor), it walks there (speed 1.25, every 10
  ticks). Within `0.5 + width * 1.8` of the trough centre it eats: one breeding item is removed,
  eating sound and particles; with `loveChance` (per-trough seeded RNG) it falls in love (love timer
  600, hearts) if at most `maxAnimals` breedable animals are within `range`; after a meal the
  cooldown is `cooldown` ticks, after a love roll the pointer is dropped (the animal's own love and
  breeding cooldown take over).
- Animals put in love by a trough give no XP when they breed (hook on
  `BreedableComponent::_spawnExperienceOrbs`).
- Falls onto a full trough deal 20% damage (hook on `Actor::handleFallDamage`).
- Index of troughs: `lp:logistics:troughs`.

### 3.4 Crate
- Contents: a compact list of stacks (`lp:logistics:crate:<pos>`, plus the custom name of the placed
  item). Total capped at `maxItems`; each slot takes at most `clamp(count + maxItems - total, 0, 64)`.
- Port: the stacks plus one empty slot; insert anywhere while room is left, extract from any slot.
- Screen: a window of 54 stacks (rows of 9) starting at `scroll`; `maxScroll = ((stacks - 1) / 9) * 9`.
  The window is the vanilla double chest screen (`game/VirtualContainer`); edits are written back to
  the crate (excess over `maxItems` goes back to the viewer), which is then compacted and the window
  rewritten. Opening sets `lepton:open` and plays the barrel open sound; the last viewer closing
  plays the barrel close sound.
- Breaking: the stacks are merged and dropped.

### 3.5 Ports (how custom blocks act as containers)
Custom blocks have no block entity. Lepton hooks `Hopper::_getContainerInBlock` and
`DropperBlockActor::getContainerAt`: for a chute, pipe, trough or crate position they return a
`Container` subclass forwarding to the block's port, so vanilla hoppers and droppers insert into
and extract from them with their own rules (cooldowns, one item per transfer, faces). Pipes use the
same lookups for their outputs.

## 4. Screens

| Screen | Mod client | Vanilla client |
|---|---|---|
| Feeding trough | vanilla dispenser screen (Quark: dispenser menu with a trough texture), title "Feeding Trough" | same |
| Crate | vanilla double chest screen showing 54 stacks, with the overlay of `src/client/logistics/CrateOverlay.cpp`: "total/max" above the window, a scroll bar right of it with up/down buttons (one row per click; Quark: wheel and dragging) | same screen without the overlay; when the crate holds more than 54 stacks a form ("Storage Crate", "N/640 items. Choose the rows to show:") lists row ranges (rows 1-6, 7-12, ...) and opens the window there |

Messages: `logistics:crate_view` (server -> client: open, total, max, stacks, scroll) and
`logistics:crate_scroll` (client -> server: down), Quark's `ScrollCrateMessage`.

## 5. Config

Section `logistics` (`config/logistics.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableChute` | bool | true | Chute module |
| `enablePipes` | bool | true | Pipes module |
| `pipeSpeed` | int | 5 | How long it takes for an item to cross a pipe. Bigger = slower. |
| `maxPipeItems` | int | 16 | Set to 0 if you don't want pipes to have a max amount of items |
| `doPipesWhoosh` | bool | true | When items eject or are absorbed by pipes, should they make sounds? |
| `enableEncasedPipes` | bool | true | Encased pipes (recipes, glass on pipes) |
| `renderPipeItems` | bool | true | Show items travelling in pipes |
| `emitVibrations` | bool | true | Pipes emit `projectile_shoot` / `projectile_land` vibrations |
| `enableFeedingTrough` | bool | true | FeedingTrough module |
| `cooldown` | int | 30 | How long, in game ticks, between animals being able to eat from the trough |
| `maxAnimals` | int | 32 | The maximum amount of animals allowed around the trough's range for an animal to enter love mode |
| `loveChance` | double | 0.333333333 | The chance (between 0 and 1) for an animal to enter love mode when eating from the trough |
| `range` | double | 10 | Search / count radius |
| `lookChance` | double | 0.015 | Chance that an animal decides to look for a trough (x 20 per tick) |
| `enableCrate` | bool | true | Crate module |
| `maxItems` | int | 640 | Crate capacity |

A disabled module stops working at once (no ports, no screens, no ticking); its recipes are removed
from the world's recipe table when a world loads.

## 6. Strings

| Key | en_US |
|---|---|
| `quark.container.feeding_trough` | Feeding Trough |
| `block.quark.crate` | Storage Crate |
| `lp.logistics.crate.form` | {}/{} items. Choose the rows to show: |
| `lp.logistics.crate.rows` | Rows {}-{} |
| `lp.logistics.crate.count` | {}/{} |
| `lp.logistics.crate.scroll_up` | Scroll up |
| `lp.logistics.crate.scroll_down` | Scroll down |

Block and entity names: RP `texts.d/*/logistics-gen.lang` (see 2).

## 7. Bedrock differences / not ported

- **Comparators** do not read pipes, crates or troughs: Bedrock comparators only read block entities
  or blocks whose type overrides `getComparatorSignal`; the base implementation is an identical-code
  folded function (`MCFOLD`) shared with unrelated blocks, so hooking it is unsafe.
- **Animal-only collision** of the trough (1.5 blocks high for animals in Quark) is not possible:
  Bedrock block collision cannot depend on the colliding entity.
- **Tempting**: Quark injects a fake player into the animals' tempt goals; Lepton drives the animal's
  navigation towards the trough directly (same range, chances, cooldowns and line-of-sight check).
  Animals whose own goals override the path may take longer.
- **Piglins** are not angered by opening a crate (Bedrock has no hookable piglin "container opened"
  reaction for custom blocks).
- **Crate screen**: scrolling uses buttons (the overlay toolkit has no mouse wheel/drag events);
  several players viewing one crate share its scroll position (one helper container per crate).
- **Terminal offset flares**: Quark draws the flare 1 px further out for containers whose box does
  not reach the shared face (chests). A Bedrock block has at most 16 bits of states; 6 faces x 5
  connection kinds need 18, so such sides use the normal terminal flare.
- **Pipe items** are drawn by a display entity holding the item, not by a block entity renderer;
  Quark's April-fools sound variants and the tiny potato offset are not ported.
- **Subtitles** for the pipe sounds are not shown (Bedrock sound definitions have no subtitles).
- **Recipes** use the `minecraft:planks` item tag (Quark: `#minecraft:planks`) and plain glass /
  copper ingots / iron ingots / chests (Quark: `c:` tags, which have no Bedrock counterpart).
- **Disabling a module** removes its recipes from the next loaded world only; the items stay in the
  creative inventory and `/give`.
- **Redstone**: chutes and pipes see redstone sources directly next to them (game/Shapes
  approximation), not power arriving through a solid block.

## 8. Tests

Selftests (dx 1100..1199): `logistics.config`, `logistics.chute_drops` (1100), `logistics.chute_blocked`
(1105), `logistics.pipe_transport` (1110), `logistics.pipe_eject_absorb` (1115),
`logistics.pipe_limits` (1120), `logistics.pipe_encase` (1125), `logistics.trough` (1130),
`logistics.crate_capacity` (1140), `logistics.crate_hoppers` (1145), `logistics.crate_screen`
(1150), `logistics.crate_spill` (1155), `logistics.drops_recipes` (1160: every block drops itself, the trough its
contents, every recipe is registered), `logistics.showcase` (1170..1182, client scenario).

E2E: `tools/e2e/server/scenarios/logistics.ps1` runs `logistics.*` and checks the pass count and the
log lines. `tools/e2e/client/scenarios/logistics.ps1` builds a showcase (chute, pipes with items,
trough with a cow, crate); the scenario uses the trough and the crate with real right clicks and
takes screenshots of the blocks, pipe items, the trough screen and the crate screen with its overlay
(then scrolled with the overlay button); with `-Mode remote-vanilla` the row picker form and the
crate window without overlay.
