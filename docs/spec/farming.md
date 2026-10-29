# U25 farming: crop harvesting, hoes, animal husbandry tweaks and small plant tweaks

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules under
`content/tweaks/module/` plus the mixins they rely on (`HarvestFarmlandMixin`, `PigMixin`,
`AnimalMixin`, `BaseCoralPlantTypeBlockMixin`, `ChickenRendererMixin`) and the data
`data/quark/tags/{block/hoe_harvestable, block/simple_harvest_blacklisted, item/big_harvesting_hoes}.json`.
Code lives in `src/feature/farming/` (namespace `lp::feature::farming`), selftests dx 2500..2599.

## 1. Modules covered

### SimpleHarvest
Right-clicking a fully grown crop harvests it and replants it in place: the crop's drops are
spawned minus one "seed" (the crop block's own item), the block is set to its replanted state, with
the block-break particles. Works with any item in hand while `emptyHandHarvest` is true, otherwise
only with a hoe. A hoe harvests a square (see HoeHarvesting's radius); with a hoe, neighbouring
"right-click crops" (sweet berry bushes, cave vines) are clicked as if the player used them.
Clicking the block *below* a crop (farmland, soul sand, jungle log) harvests the crop above it.
Blocks a hoe can till (grass, dirt, ...) are never harvest targets (the hoe tills instead).

### HoeHarvesting
Breaking a hoe-harvestable block (tag `quark:hoe_harvestable`) with a hoe also breaks every
hoe-harvestable block in the surrounding square on the same layer: radius 2 → 3×3, high-tier hoes
(diamond, netherite; tag `quark:big_harvesting_hoes`) radius 3 → 5×5. Crops count only when fully
grown, cocoa only at age 2, player-placed (persistent) leaves never. If any of the extra blocks has
a non-zero hardness the hoe loses 1 durability. The radius also drives SimpleHarvest's area.

### PigLitters
Breeding pigs produces a litter of `minPigLitterSize..maxPigLitterSize` piglets (2..3) instead of
one. Pigs also eat golden carrots (breeding item, tempt item with speed 1.2); a parent that was fed a
golden carrot adds `minGoldenCarrotBoost..maxGoldenCarrotBoost` (0..2) piglets. The golden-carrot
mark is cleared when the animal leaves love mode.

### PoisonPotatoUsage
Using a poisonous potato on a baby mob (any ageable baby, villagers included, or a tadpole) has a
`chance` (0.1; always in creative) to poison it: eating sound (volume 0.5, pitch 0.25), a white
spell particle, Poison for 80 ticks (if `poisonEffect`) and the mob never grows up (animals stay
babies, tadpoles never become frogs). On failure: eating sound with pitch 0.5..1.0 and a smoke
particle. The potato is consumed outside creative. An already poisoned mob ignores potatoes.

### CoralOnCactus
A live coral fan (floor fan, not wall fans, not coral plants) standing on a cactus counts as being
in water: it can be placed there, survives, and does not die.

### RenewableSporeBlossoms
Bone meal on a spore blossom is consumed and has a `boneMealChance` (0.2) chance to pop a spore
blossom item. Works from players and dispensers.

### PetalsOnWater
Pink petals can be placed on a water source (like a lily pad): the block `lepton:water_pink_petals`
(1..4 petals). Using pink petals on it adds a petal (up to 4). It needs a water source below,
otherwise it breaks; it drops pink petals (one per petal).

### GrabChickens
Right-clicking an adult chicken with an empty main hand (not sneaking) picks it up: the chicken
rides the player. Right-clicking any entity with an empty hand while carrying chickens puts them
down. While a chicken is carried the player gets Slow Falling (and Slowness `slownessLevel`, 1 =
Slowness I) every tick (5-tick ambient effects without particles); a helmet (when `needsNoHelmet`)
or being under water makes the player drop the chicken. Quark's render tweaks (chicken drawn on
the head, legs hidden, `chickenOffset`) are not ported (§7).

### HorsesSwim
A ridden horse (any AbstractHorse: horse, donkey, mule, skeleton/zombie horse, llama, trader llama,
camel) in water with water below it is pushed up 0.1 blocks every tick, so it swims at the surface
instead of sinking and throwing its rider.

### VillagersFollowEmeralds
Villagers follow (tempt, speed 0.6) players holding an emerald block, like animals follow food.

## 2. Blocks and items

| Id | Name | States | Recipe | Drops | Tags |
|---|---|---|---|---|---|
| `lepton:water_pink_petals` | Pink Petals | `lepton:amount` 1..4 (default 1), `minecraft:cardinal_direction` (trait, the player's facing reversed like vanilla petals) | none (placed from `minecraft:pink_petals`) | `minecraft:pink_petals` × amount (explosion decay) | hidden from the creative inventory; pick block gives pink petals |

Geometry: four flat petal clusters 0.1 px above the water surface (`geometry.lepton.water_pink_petals`,
bone visibility by amount), rotated by the direction, no collision, instant break, selection box
16×3×16 px. Texture `textures/blocks/water_pink_petals` (original art, `tools/textures/farming.ps1`).
Survival rule: `minecraft:placement_filter` (water source below) plus a native check.

Bedrock tag equivalents (other units may add custom blocks/items):

| Quark tag | Lepton | Default members (Bedrock ids) |
|---|---|---|
| `quark:hoe_harvestable` | block tag `lepton:hoe_harvestable` + built-in list | crops (`wheat`, `carrots`, `potatoes`, `beetroot`, `nether_wart`, `torchflower_crop`, `pitcher_crop`, `melon_stem`, `pumpkin_stem`), flowers (all small and tall flowers, `pink_petals`, `wildflowers`, `cactus_flower`, `torchflower`, `wither_rose`, `flowering_azalea`, `azalea`... per vanilla `#minecraft:flowers`), `#minecraft:replaceable_by_trees` (leaves of every kind, `short_grass`, `fern`, `dead_bush`, `vine`, `glow_lichen`, `tall_grass`, `large_fern`, `hanging_roots`, `seagrass`, `crimson_roots`, `warped_roots`, `nether_sprouts`, `leaf_litter`, `bush`, `short_dry_grass`, `tall_dry_grass`), `cactus`, `reeds` (sugar cane), `cocoa`, `waterlily` |
| `quark:big_harvesting_hoes` | item tag `lepton:big_harvesting_hoes` + built-in | `diamond_hoe`, `netherite_hoe` |
| `quark:simple_harvest_blacklisted` | block tag `lepton:simple_harvest_blacklisted` | empty |
| hoe test (`HoeItem`, `#minecraft:hoes`) | item tag `minecraft:is_hoe` | vanilla hoes |

## 3. Mechanics

**SimpleHarvest** (`PlayerInteractBlockEvent`, server): ignored for spectators and simulated
re-entry. If the clicked block is tillable (`grass_block`, `dirt`, `coarse_dirt`, `dirt_with_roots`,
`grass_path`) nothing happens. `isHoe` = item tag `minecraft:is_hoe`. Without `emptyHandHarvest`
only hoes harvest. With a hoe the clicked block or the one above must be a target. Range
r = 1 (not a hoe or HoeHarvesting off), `regularHoeRadius` (2) or `highTierHoeRadius` (3) for
`big_harvesting_hoes`; the square is `x,z ∈ [1-r, r-1]`. For every column: try the block at the
clicked height, else the block above. Action per block: blacklisted → none; `r > 1` and a
right-click crop → *click* (the game's own block use, as the player, main hand); in
`harvestableBlocks` → *harvest*. If anything happened the interaction is cancelled (the item is not
used), the edits run on the next tick, and with `harvestingCostsDurability` a hoe takes 1 damage.

*Harvest*: drops are computed with the player's held item (Fortune applies) as for mining; one
item equal to the crop's own item (`wheat_seeds`, `carrot`, `potato`, `beetroot_seeds`,
`nether_wart`, `cocoa_beans`) is removed; the rest is spawned at the block; destroy particles and
sound; the block becomes the "after" state.

`harvestableBlocks` entries are `"<block>[state=value,...][,<after>]"`; an entry matches when every
listed state matches; the after state is the block's default state with its listed states (default:
the block's default state). Defaults (Bedrock ids/states for Quark's list):
`minecraft:wheat[growth=7]`, `minecraft:carrots[growth=7]`, `minecraft:potatoes[growth=7]`,
`minecraft:beetroot[growth=7]`, `minecraft:nether_wart[age=3]`, and for each `direction` 0..3
`minecraft:cocoa[age=2,direction=d],minecraft:cocoa[age=0,direction=d]`.
`rightClickableBlocks` defaults: `minecraft:sweet_berry_bush`, `minecraft:cave_vines_body_with_berries`,
`minecraft:cave_vines_head_with_berries` (Bedrock splits Java's `cave_vines` by berry state).
`doHarvestingSearch`: non-`minecraft:` crops (blocks of the game's crop class, e.g. other native
mods) harvest at their maximum growth and replant at growth 0; non-`minecraft:` bush-type blocks
that accept bone meal are right-click crops.

`villagersUseSimpleHarvest` — Quark 4.1-488's mixin applies it only when SimpleHarvest itself is
**disabled** (`!staticEnabled && villagersUseSimpleHarvest`). Lepton reproduces that exactly: with
SimpleHarvest disabled, farmer villagers (Bedrock `HarvestFarmBlockGoal`) harvest-and-replant mature
crops instead of breaking them; with the default config villagers behave as in vanilla.

**HoeHarvesting** (`PlayerDestroyBlockEvent` with a hoe in the main hand): if the broken block is
hoe-harvestable, every other hoe-harvestable block in the square (same rules, same y) is destroyed
as by the player (`Block::playerDestroy`, tool drops) with particles; durability −1 if any had a
hardness ≠ 0. Water is never harvestable; crops only at max growth; cocoa at age 2; leaves with
`persistent_bit` never.

**PigLitters**: hook on `BreedableComponent::mate`; after the vanilla baby is born, for a pig,
`litter = rand(min,max)` + per golden-carrot parent `rand(minBoost,maxBoost)`; `litter-1` extra
babies (`minecraft:pig<minecraft:entity_born>`) spawn at the first parent. Golden carrots
(`pigsEatGoldenCarrots`): using one on an adult pig that can breed puts it in love (like vanilla
feeding: love particles, item consumed outside creative) and marks it (`quark:AteGoldenCarrot`
entity tag); the tag is removed whenever the animal is not in love. Pigs within 10 blocks walk
towards players holding a golden carrot (speed 1.2, stop at 2.5 blocks, like Java's TemptGoal).

**PoisonPotatoUsage** (`GameMode::interact` hook): see §1. Poisoned mobs get the entity tag
`quark:poison_potato_applied` (saved with the entity) and Bedrock's own age-growth pause
(`AgeableComponent::mGrowthPaused`), re-applied every second for animals and tadpoles. Particles:
`minecraft:mobspell_emitter` / `minecraft:basic_smoke_particle`; sound `random.eat`.

**CoralOnCactus**: hooks on `CoralFan::$checkAlive` and `CoralFan::$neighborChanged` (floor fans
only; `CoralFanHang` is the wall fan class and is excluded): a live fan on a cactus never turns dead
and does not pop off. Placement is allowed through the `PlayerInteractBlockEvent` path: using a live
coral fan on the top face of a cactus places the fan (vanilla refuses).

**RenewableSporeBlossoms**: hooks on `FertilizerItem::$_useOn` (players) and
`FertilizerItem::$dispense` (dispensers): a spore blossom target consumes one bone meal (not in
creative), plays the crop-growth particles and with `boneMealChance` drops a spore blossom.

**PetalsOnWater**: `PlayerInteractBlockEvent` (clicked block) and `PlayerUseItemEvent` (use in the
air; a ray from the eyes, 5 blocks, stopping at water sources) with `minecraft:pink_petals`: if the
target is water (or the top face of a block below water/petals) and the water is a source with air
above, place `water_pink_petals` (amount 1, facing the player); if it is already water petals with
amount < 4, add one. Sound `pink_petals` place; the item is used up outside creative.

**GrabChickens** (`GameMode::interact` hook, main hand empty, not sneaking, not a simulated
player): carrying chickens → they dismount; else if the player may host (no helmet when
`needsNoHelmet`, not under water) and the target is an adult chicken → it rides the player. Server
tick: a player carrying a chicken that may no longer host drops it; otherwise Slow Falling 0 and
Slowness `slownessLevel-1` for 5 ticks, ambient, hidden.

**HorsesSwim**: server tick over loaded horse-family entities: ridden, in water, water at the block
below → `move(0, 0.1, 0)` on the server and the same motion sent to the riders' clients.

**VillagersFollowEmeralds**: every 5 ticks, each villager (`minecraft:villager_v2`) without a
target within 10 blocks of a player holding an emerald block (either hand) paths to that player at
speed 0.6 and stops within 2.5 blocks; it does not start again for 100 ticks after the player stops
holding the block (Java TemptGoal's calm-down).

## 4. Screens

None. All modules are server side and work for vanilla clients (the resource pack carries the
water petals model and texture).

## 5. Config

Section `farming` (`config/farming.json`), version 1.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enableSimpleHarvest` | bool | true | SimpleHarvest module |
| `emptyHandHarvest` | bool | true | Can players harvest crops with empty hand clicks? |
| `harvestingCostsDurability` | bool | false | Does harvesting crops with a hoe cost durability? |
| `doHarvestingSearch` | bool | true | Should Quark look for(nonvanilla) crops, and handle them? |
| `villagersUseSimpleHarvest` | bool | true | Should villagers use simple harvest instead of breaking crops? (see §3) |
| `harvestableBlocks` | string[] | see §3 | Which crops can be harvested? Format `"harvestState[,afterHarvest]"` |
| `rightClickableBlocks` | string[] | see §3 | Which blocks should right click harvesting simulate a click on instead of breaking? |
| `enableHoeHarvesting` | bool | true | HoeHarvesting module |
| `regularHoeRadius` | int | 2 | 1..5 |
| `highTierHoeRadius` | int | 3 | 1..5 |
| `enablePigLitters` | bool | true | PigLitters module |
| `minPigLitterSize` | int | 2 | ≥ 1 |
| `maxPigLitterSize` | int | 3 | ≥ 1 |
| `pigsEatGoldenCarrots` | bool | true | |
| `minGoldenCarrotBoost` | int | 0 | ≥ 0 |
| `maxGoldenCarrotBoost` | int | 2 | ≥ 0 |
| `enablePoisonPotatoUsage` | bool | true | PoisonPotatoUsage module |
| `chance` | double | 0.1 | poison chance |
| `poisonEffect` | bool | true | apply Poison (80 ticks) |
| `enableCoralOnCactus` | bool | true | CoralOnCactus module |
| `enableRenewableSporeBlossoms` | bool | true | RenewableSporeBlossoms module |
| `boneMealChance` | double | 0.2 | ≤ 0 disables the bone meal use |
| `enablePetalsOnWater` | bool | true | PetalsOnWater module |
| `enableGrabChickens` | bool | true | GrabChickens module |
| `needsNoHelmet` | bool | true | |
| `slownessLevel` | int | 1 | Set to 0 to disable |
| `chickenOffset` | double | 0.0 | -1..1; Negative numbers moves the chicken down, positive up (no effect, §7) |
| `enableHorsesSwim` | bool | true | HorsesSwim module |
| `enableVillagersFollowEmeralds` | bool | true | VillagersFollowEmeralds module |

Ranges are clamped as Quark's `@Config.Min/Max`. A disabled module does nothing; existing water
petals keep working as blocks (they only drop vanilla pink petals) but no new ones can be placed.

## 6. Strings

No mod text: Quark shows none for these modules outside JEI hints (not ported, SPEC §8).
Block name (RP `texts.d/*/farming.lang`): `tile.lepton:water_pink_petals.name` = "Pink Petals".

## 7. Bedrock differences / not ported

- Ids and states in `harvestableBlocks`/`rightClickableBlocks` are Bedrock ones (`growth` instead of
  `age`, `beetroot`, `direction` for cocoa, cave vines split into two berry blocks).
- `SimpleHarvestEvent` (Quark's API event for other mods) and the Flan claim integration: Java mod
  APIs, not ported (SPEC §8).
- GrabChickens: the vanilla Bedrock player only accepts tame parrots as passengers (its rideable
  seats are the shoulder seats), so the server lets a chicken through `Player::canAddPassenger`
  only while GrabChickens seats it. Passenger positions are computed by each client from the
  vanilla player definition, so the chicken sits on the shoulder seat. Quark's two client render
  tweaks are not ported: drawing the carried chicken on the head pushed down by
  `0.25 - chickenOffset` (`LivingEntityRendererMixin`) and hiding its legs (`ChickenModel`).
  Investigated in the client build: in 1.26.51 world actors are drawn in a batch by
  `DataDrivenRendererV2::generateFrame` from ECS-extracted render data; the per-actor paths
  (`ActorRenderDispatcher::render`, `DataDrivenRenderer::$render`,
  `DataDrivenGeometry::copyBoneMatricesToActor` / `copyTransformMatricesToModelParts`) are never
  called for in-world actors (render probes in the client e2e never fired), and the extracted data
  has no exposed per-actor layout to edit. A resource-pack route needs the vanilla chicken client
  entity file, which U18 (VariantAnimalTextures) owns. `chickenOffset` is kept in the config for
  parity and has no effect.
- PoisonPotatoUsage: the advancement trigger (`poison_baby`) is not ported (no custom advancements).
- PigLitters: the "animals bred" statistic/advancement for extra piglets is not ported.
- HorsesSwim: a horse ridden by a real client is moved by that client (Bedrock vehicle movement is
  client-predicted); the push is sent to the rider as entity motion, which the client applies.
- PetalsOnWater: the in-air use reads the target with a server-side ray (5 blocks) since Bedrock
  sends no hit result for item use in the air.

## 8. Tests

Selftests (dx 2500..2599): `farming.config`, `farming.harvest_parse`, `farming.simple_harvest`
(wheat, cocoa, clicking below, hoe area, any item, empty-hand rule, tillable), `farming.hoe_harvest`
(3×3 and 5×5 break, immature crops kept), `farming.right_click_crops` (berry bush clicked by hoe
area), `farming.coral_on_cactus`, `farming.spore_blossom`, `farming.water_petals`,
`farming.poison_potato`, `farming.pig_litters`, `farming.grab_chickens`, `farming.horses_swim`,
`farming.villagers_follow`.

E2E: `tools/e2e/server/scenarios/farming.ps1` runs `farming.*` and checks the log lines;
`tools/e2e/client/scenarios/farming.ps1` builds a small farm (mature wheat, petals on water in all 4
amounts, a coral fan on cactus, a carried chicken) and saves screenshots of each.
