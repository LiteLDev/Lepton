# U26 mobtweaks: mob, item and block tweaks from Quark's `tweaks` category

Reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules under
`content/tweaks/module/` plus the mixins they rely on (`SlimeMixin`, `SpongeBlockMixin`,
`ConcretePowderBlockMixin`, `AnvilMenuMixin`, `ItemMixin`) and `DoubleDoorMessage`. Behaviour only;
no Quark code or assets are used.

## 1. Modules covered

### SaferCreatures
When a rabbit lands, `heightReduction` (5.0) blocks are subtracted from its fall distance before
fall damage is computed (rabbits jump high and hurt themselves in vanilla; 5 is the frog value).
When a slime lands and `enableSlimeFallDamageRemoval` is on, its fall distance becomes 0 (no fall
damage) and, if it fell more than 2 blocks, its vertical motion is set to -2 (it keeps sinking into
the ground contact instead of floating). Nothing else: Quark 4.1-488 has no creeper/knockback part.

### SlimesToMagmaCubes
A slime that dies from the magma block's "hot floor" damage splits into **magma cubes** instead
of slimes (same number, sizes and positions as the vanilla split). Tiny magma cubes drop no cream.

### SnowGolemPlayerHeads
A snow golem with a custom name that still wears its pumpkin and is killed by a witch (directly or
by the witch's thrown potion) drops a player head of the player named like the golem, in addition
to its normal drops.

### VexesDieWithTheirMasters
Every vex whose owner (the evoker that summoned it) is dead or dying is killed at once (generic
kill damage: normal death, drops and XP).

### ZombieVillagersOnNormal
On Normal difficulty a villager killed by a zombie always becomes a zombie villager (vanilla: 50%
on Normal, 100% on Hard, never on Easy). The conversion keeps profession, level, trades, gossip,
experience and plays the "zombie infects" sound. Easy and Hard are unchanged.

### DragonScales
Adds the item **Dragon Scale** (`lepton:dragon_scale`). An ender dragon that dies after the first
dragon of the fight was already killed (`hasPreviouslyKilledDragon`, i.e. respawned dragons) drops
one Dragon Scale at its position when its death animation reaches tick 100. Crafting an **elytra**
with a dragon scale (shapeless, exactly these two items) yields a new elytra and the original
elytra is kept ("elytra duplication").

### GoldToolsHaveFortune
Golden tools have a built-in ("baked") enchantment: Fortune `fortuneLevel` (2) on the golden
pickaxe, axe, shovel and hoe, Looting `fortuneLevel` on the golden sword (the Java tag
`counts_as_weapon_for_gold_tools_have_fortune`). Baked enchantments do not stack with real ones:
the higher level applies. `fortuneLevel = 0` disables the baked Fortune/Looting. Golden tools also
get the harvest level `harvestLevel` (2 = iron): they drop what an iron tool of the same kind drops
(golden pickaxe mines diamond/gold/redstone/emerald ore with drops). `itemEnchantments` bakes extra
enchantments into any item (`"item+enchant@level"`, e.g. `"minecraft:stick+minecraft:sharpness@10"`).
Tooltips list baked enchantments (`displayBakedEnchantmentsInTooltip`), italic when
`italicTooltip`, only when the baked level beats the item's own level.

### DiamondRepair
Changes anvil repair materials: `repairChangesList` entries `"<item>=<repair>[,<repair>...]"`
replace the item's vanilla repair materials (default: every netherite tool and armour piece is
repaired with diamonds instead of netherite ingots); `unrepairableItemsList` items cannot be repaired
with any material. `enableJeiHints` shows the change on the item (Quark: JEI info page).

### NoDurabilityOnCosmetics
Using an anvil only to rename (empty right slot) or with a cosmetic item in the right slot (item tag
`quark:cosmetic_anvil_items`, empty in Quark; Lepton: `lepton:cosmetic_anvil_items`, when
`allowCosmeticItems`) never damages the anvil (vanilla: 12% chance per use).

### ShulkerPacking
Sneak-using a single chest or trapped chest (not an ender chest, not a double chest) while holding
a shulker shell in **each** hand destroys the chest without drops and gives the player an undyed
shulker box holding the chest's 27 slots (and its custom name), into the selected hotbar slot when
possible. One shell is used from each hand (none in creative), the shulker box close sound plays
and the "used shulker shell" statistic is increased by 2.

### MagmaKeepsConcretePowder
Concrete powder whose block below is a magma block never turns into concrete, neither by touching
water nor by falling into water.

### ImprovedSponges
A sponge drains up to `maximumWaterDrain` (256, vanilla 64) water blocks, following water up to
`maximumCrawlDistance` (10, vanilla 6) blocks away. With `enablePlacingOnWater`, using a sponge
while looking at a water source block (no block in between, within reach) places the sponge into
the water (like a lily pad); it then soaks up the water as usual.

### DoubleDoorOpening
When a player (not sneaking) opens or closes a door by hand, its partner door — the door of the same
type next to it on the hinge's opposite side, same facing, opposite hinge, same open state — opens /
closes too (`enableDoors`). Fence gates stacked directly above or below a used fence gate (same type,
same facing, same open state) follow it (`enableFenceGates`; the one below is tried first, the
one above only if no gate below followed). Blocks tagged `non_double_door` are excluded. Works for
vanilla doors/gates and for Lepton's generated doors/gates (`lepton:door`, `lepton:fence_gate`).

## 2. Blocks and items

| Id | English name | Notes |
|---|---|---|
| `lepton:dragon_scale` | Dragon Scale | Item, stack 64, creative "Items" category (Quark: Ingredients tab after the eye of ender). Texture drawn by `tools/textures/mobtweaks.ps1`. |

Recipes:

| Id | Type | Pattern | Result |
|---|---|---|---|
| `lepton:elytra_duplication` | shapeless, crafting table and 2x2 | `minecraft:elytra` + `lepton:dragon_scale` | `minecraft:elytra`; the consumed elytra (with its damage, enchantments and name) is given back |

Tags (other units may add blocks/items):

| Tag | Kind | Meaning |
|---|---|---|
| `lepton:non_double_door` | block | doors / gates excluded from DoubleDoorOpening (Quark `quark:non_double_door`, empty) |
| `lepton:cosmetic_anvil_items` | item | cosmetic anvil items for NoDurabilityOnCosmetics (Quark `quark:cosmetic_anvil_items`, empty) |
| `lepton:chest` | block | Lepton chests stored in a virtual container (27 slots) that ShulkerPacking packs like vanilla chests (Quark: `c:chests`) |

## 3. Mechanics

All mechanics run on the server thread and check their `enable<Module>` key on every use, so
`/quark reload` applies immediately.

- **SaferCreatures** — hook `Mob::causeFallDamageToActor(distance, ...)`: rabbit ⇒ `distance =
  max(0, distance - heightReduction)`; slime with `enableSlimeFallDamageRemoval` ⇒ if `distance > 2`
  set the vertical velocity to -2, then `distance = 0`.
- **SlimesToMagmaCubes** — `MobDieEvent`: a `minecraft:slime` whose damage cause is `magma` is
  remembered. Hook `Slime::remove()`: after the vanilla split of a remembered slime, every slime it
  just spawned is replaced by a `minecraft:magma_cube` of the same size at the same position (name tag
  and persistence copied). Natural slime splitting is otherwise untouched.
- **SnowGolemPlayerHeads** — `MobDieEvent`: `minecraft:snow_golem` with a non-empty name tag, not
  sheared (still wears the pumpkin), damage source entity (or its owner, for potions) a
  `minecraft:witch` ⇒ spawn a `minecraft:player_head` item at the golem's position named
  "&lt;name&gt;'s Head". Logged as `mobtweaks: snow golem head <name>`.
- **VexesDieWithTheirMasters** — `MobDieEvent` of any mob: every loaded `minecraft:vex` whose owner
  id is the dying mob is killed. A sweep every 20 ticks also kills vexes whose owner is dead/gone
  dying while the vex was unloaded is not covered by Java either (the owner reference is null).
- **ZombieVillagersOnNormal** — `ActorHurtEvent` on a villager (`minecraft:villager_v2` /
  `minecraft:villager`) on Normal difficulty whose damage would kill it and whose attacker is a
  zombie-family mob (`zombie`, `husk`, `drowned`, `zombie_villager(_v2)`, Java's `Zombie` subclasses
  incl. `zombie_pigman`): the hit is cancelled and the villager runs its vanilla
  `minecraft:become_zombie` transformation (the same transformation Bedrock uses on Hard, keeping
  profession, trades and level), plus the infection sound. Logged as `mobtweaks: villager zombified`.
- **DragonScales** — hook `EnderDragon::die()`: when the dragon was a respawned one
  (`mDragonKilledPreviously`, or the End fight's `mPreviouslyKilled` for a dragon in the End), 100
  ticks later a `lepton:dragon_scale` item spawns at the dragon's (last known) position. Logged as
  `mobtweaks: dragon scale dropped`. The recipe is a behaviour-pack shapeless recipe; hook
  `CraftHandlerCrafting::handleConsumedItem`: for `lepton:elytra_duplication` the consumed elytra is
  given back to the player after the craft (inventory, else dropped); with the module disabled the
  craft request is rejected.
- **GoldToolsHaveFortune** — baked map item → {enchantment → level} rebuilt on every config load:
  `itemEnchantments` entries first, then (when `fortuneLevel > 0`) Looting for `golden_sword` and
  Fortune for `golden_pickaxe/axe/shovel/hoe` unless already set. Hooks
  `EnchantUtils::getEnchantLevel` (returns `max(real, baked)`) and `EnchantUtils::hasEnchant`, which
  every Bedrock enchantment effect (fortune drops, looting, silk touch, sharpness, ...) reads.
  Harvest level: hook the tools' `canDestroySpecial(block)`; a golden tool answers like the tool of
  tier `harvestLevel` (0 wooden, 1 stone, 2 iron, 3 diamond, 4 netherite) of the same kind.
- **DiamondRepair** — hook every `isValidRepairItem` override (tools, weapons, armour, shields,
  maces, wolf armour, data-driven items): unrepairable ⇒ false; listed ⇒ repair item in the list.
  Unknown ids in the lists are ignored (Quark: same).
- **NoDurabilityOnCosmetics** — hook `AnvilContainerScreenValidator::getCraftResult` to note,
  per player, whether the anvil's material slot is empty or holds a cosmetic item; hook
  `AnvilBlock::tryApplyCraftingDamage` to skip the damage for such a craft.
- **ShulkerPacking** — `PlayerInteractBlockEvent` (sneaking): main hand and off-hand shulker
  shells. The off-hand of Bedrock does not accept shulker shells, so Lepton allows them there
  (`Item` off-hand flag, server and client mod) and, for vanilla clients that refuse the off-hand,
  also accepts **two or more shells in the main hand** (two are used). The event is cancelled and the
  packing happens on the next tick: chest contents (27 slots; a paired double chest is refused like
  Java's container-size check) go into an `minecraft:undyed_shulker_box` item (`Items` list in its
  user data), the chest's custom name becomes the item's name, the chest is removed without drops.
  Logged as `mobtweaks: packed chest`.
- **MagmaKeepsConcretePowder** — hooks `ConcretePowderBlock::_isTouchingWater` and
  `_tryTouchWater`: false when the block below is `minecraft:magma`.
- **ImprovedSponges** — hook `SpongeBlock::_performAbsorbWater(region, pos)`, reimplementing Java's
  breadth-first search: start at the sponge (counts as 1), visit the 6 neighbours up to depth
  `maximumCrawlDistance`, stop after `maximumWaterDrain + 1` matches; a match is water (removed),
  a waterlogged block (water layer removed) or kelp / seagrass / tall seagrass (destroyed with
  drops). Returns true (sponge turns wet) if anything was absorbed. Placing on water:
  `PlayerUseItemEvent` with a sponge: ray from the eyes along the view, 4.5 blocks (Java's block
  interaction range); the first water source hit before any other block becomes a sponge (sound,
  one sponge used unless creative), then absorbs.
- **DoubleDoorOpening** — `PlayerInteractBlockEvent` on a door/gate by a non-sneaking player: its
  open state is remembered; 2 ticks later, if it changed, the partner (rules in §1) is set to the
  same state through the block's own code (vanilla `DoorBlock::setToggled` /
  `FenceGateBlock::_setOpen`, Lepton `shapes::setOpen`), which plays the sound. Iron doors and
  `lepton:redstone_only` doors never toggle by hand, so they are never paired (Java:
  `canOpenByHand`).

## 4. Screens

None. Mod clients show tooltip lines (GoldToolsHaveFortune baked enchantments, DiamondRepair
hints); vanilla clients see vanilla tooltips (client-side only in Quark too).

## 5. Config

Section `mobtweaks` (`config/mobtweaks.json`), `version` 1:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enableSaferCreatures` | bool | true | module |
| `heightReduction` | double | 5.0 | blocks subtracted from rabbit fall height |
| `enableSlimeFallDamageRemoval` | bool | true | slimes take no fall damage |
| `enableSlimesToMagmaCubes` | bool | true | module |
| `enableSnowGolemPlayerHeads` | bool | true | module |
| `enableVexesDieWithTheirMasters` | bool | true | module |
| `enableZombieVillagersOnNormal` | bool | true | module |
| `enableDragonScales` | bool | true | module (drops and recipe) |
| `enableGoldToolsHaveFortune` | bool | true | module |
| `fortuneLevel` | int | 2 | baked Fortune / Looting level (min 0) |
| `harvestLevel` | int | 2 | golden tool harvest level 0..4 |
| `displayBakedEnchantmentsInTooltip` | bool | true | tooltip lines (mod clients) |
| `italicTooltip` | bool | true | baked lines in italic |
| `itemEnchantments` | string[] | [] | `"item+enchant@level"` extra baked enchantments |
| `enableDiamondRepair` | bool | true | module |
| `repairChangesList` | string[] | the 9 netherite tools/armour `=minecraft:diamond` | repair item changes |
| `unrepairableItemsList` | string[] | [] | items that cannot be repaired with materials |
| `enableJeiHints` | bool | true | DiamondRepair tooltip hints (mod clients) |
| `enableNoDurabilityOnCosmetics` | bool | true | module |
| `allowCosmeticItems` | bool | true | cosmetic items also spare the anvil |
| `enableShulkerPacking` | bool | true | module |
| `enableMagmaKeepsConcretePowder` | bool | true | module |
| `enableImprovedSponges` | bool | true | module |
| `maximumWaterDrain` | int | 256 | max water blocks (min 64) |
| `maximumCrawlDistance` | int | 10 | max BFS depth (min 6) |
| `enablePlacingOnWater` | bool | true | sponges can be placed on water |
| `enableDoubleDoorOpening` | bool | true | module |
| `enableDoors` | bool | true | doors open together |
| `enableFenceGates` | bool | true | stacked gates open together |

Ids in the lists use Java/Bedrock ids (`minecraft:netherite_sword`); enchantment ids may carry the
`minecraft:` prefix (`minecraft:sharpness`, `minecraft:silk_touch`, ...).

## 6. Strings

| Key | en_US |
|---|---|
| `quark.jei.hint.repair_item_changed` | This item's repair material in an Anvil has been changed to {}. |
| `quark.jei.hint.repair_item_changed_multiple` | This item's repair material in an Anvil has been changed to one of {}. |
| `quark.jei.hint.repair_item_removed` | This item has been changed to not be repairable in an Anvil using repair materials. |
| `lp.mobtweaks.player_head` | {}'s Head (Minecraft's `block.minecraft.player_head.named`) |

Item name: `item.lepton:dragon_scale` = "Dragon Scale" (RP `texts.d`).

## 7. Bedrock differences / not ported

- **Player heads** carry no skin on Bedrock (no player profiles on skulls): the dropped head is a
  plain player head named "&lt;name&gt;'s Head". The "Getting Ahead" advancement does not exist
  (Bedrock has no custom advancements, SPEC §8).
- **Elytra duplication**: Bedrock crafting has no remaining items for this recipe, so the original
  elytra goes back to the inventory (dropped when full) instead of staying in the grid.
- **ShulkerPacking**: additionally accepts two shells in the main hand (vanilla clients cannot put a
  shulker shell into the off-hand). Lepton chests take part through the `lepton:chest` tag.
- **DiamondRepair / harvest level on remote servers**: the anvil preview and mining prediction are
  computed by the client. Vanilla clients on a BDS see the vanilla repair rules in the anvil (the
  server accepts both but the vanilla client never offers a diamond repair); mod clients and local
  worlds apply the config (the client mod hooks the same functions with its own config).
- **Tooltips**: Lepton appends the baked enchantment line after the vanilla tooltip; a lower real
  enchantment line of the same kind stays visible above it (Java replaces it in place).
- **JEI hints** of the other modules are not ported (SPEC §8); DiamondRepair's `enableJeiHints`
  drives the tooltip hint instead.
- **ZombieVillagersOnNormal** uses Bedrock's own conversion (event `minecraft:become_zombie`), which
  keeps what Bedrock keeps on Hard.

## 8. Tests

Selftests (`/quark selftest "mobtweaks.*"`), dx range 2600..2699:

| Test | dx | Checks |
|---|---|---|
| `mobtweaks.config` | - | defaults, list parsing (`repairChangesList`, `itemEnchantments`) |
| `mobtweaks.safer_creatures` | 2600 | rabbit / slime fall damage from a 10 block drop |
| `mobtweaks.slimes_to_magma` | 2605 | a big slime killed by magma damage splits into magma cubes; other deaths split into slimes |
| `mobtweaks.snow_golem_head` | 2610 | named golem killed by a witch drops the named head; unnamed / sheared golems don't |
| `mobtweaks.vexes` | 2615 | an evoker's vexes die with it |
| `mobtweaks.zombie_villagers` | 2620 | on Normal a villager killed by a zombie becomes a zombie villager |
| `mobtweaks.dragon_scale` | 2625 | a respawned dragon drops a scale at death tick 100; the elytra recipe exists |
| `mobtweaks.gold_tools` | 2630 | baked fortune/looting levels, golden pickaxe drops diamonds from diamond ore with fortune |
| `mobtweaks.diamond_repair` | - | netherite gear repairs with diamonds, not netherite ingots; unrepairable list |
| `mobtweaks.cosmetic_anvil` | 2635 | rename-only anvil craft never damages the anvil |
| `mobtweaks.shulker_packing` | 2640 | chest with items + two shells becomes a shulker box item with the items and name |
| `mobtweaks.magma_concrete` | 2645 | powder over magma next to water stays powder; elsewhere it hardens |
| `mobtweaks.sponges` | 2650 | a sponge drains more than 65 blocks and up to 10 blocks away; placing on water |
| `mobtweaks.double_doors` | 2660 | vanilla and Lepton double doors and stacked gates open/close together |

Server e2e: `tools/e2e/server/scenarios/mobtweaks.ps1` runs `mobtweaks.*` and waits for the log
lines. Client e2e: `tools/e2e/client/scenarios/mobtweaks.ps1` gives the dragon scale and a golden
pickaxe and screenshots the inventory (item icon, baked "Fortune II" tooltip).
