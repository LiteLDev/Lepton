# U27 misctweaks: elytra, note block, recipe, armor stand tweaks, the experimental nerfs and the Java-only review

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules
`tweaks/BetterElytraRocket`, `tweaks/CampfiresBoostElytra`, `tweaks/MoreNoteBlockSounds`,
`tweaks/AutomaticRecipeUnlock`, `tweaks/ArmedArmorStands`, `experimental/GameNerfs`,
`experimental/SpawnerReplacer`, `experimental/VillagerRerollingRework`, and the Java-only review of
`experimental/AdjustableChat`, `experimental/NarratorReadout`, `experimental/OverlayShader`,
`experimental/ClimateControlRemover` and `client/MicrocraftingHelper` (plus the mixins these modules
use: `ArmorStandMixin`, `BoatMixin`, `ElytraItemMixin`, `ExperienceOrbMixin`, `FallingBlockEntityMixin`,
`Huge{Red,Brown}MushroomFeatureMixin`, `PistonBaseBlockMixin`, `PistonMovingBlockEntityMixin`,
`TripWireHookBlockMixin`, `ChunkGeneratorMixin` (JigsawPlacement), `SpawnerBlockEntityMixin`,
`VillagerMixin`, `AbstractVillagerMixin`, `ClimateParameterPointMixin`).

## 1. Modules covered

### BetterElytraRocket (tweaks, on)
Using a firework rocket while wearing a working elytra and **not** gliding starts gliding right away:
the rocket is fired attached to the player (one is consumed unless in creative), the player starts
gliding and jumps. Without the module a rocket can only boost an elytra flight that already runs.

### CampfiresBoostElytra (tweaks, on)
Every tick, a gliding player whose vertical speed is below `maxSpeed` looks straight down (up to 20
blocks through air and Grates): a **lit signal campfire** there pushes the player up by
`boostStrength`, a lit signal **soul** campfire pulls the player down (force x -1.5). Past 16 blocks
the force fades linearly to 0 at 20. The new vertical speed is `min(maxSpeed, vy + force)`.

### MoreNoteBlockSounds (tweaks, on)
A note block that stands on an amethyst block (amethyst block, budding amethyst, amethyst cluster,
small/medium/large bud) and has no mob head on top plays the amethyst chime instead of its
instrument (Java gives amethyst the harp, which Quark replaces; Bedrock gives it the bass drum),
pitched like any note (`2^((note-12)/12)`), with the note particle. Quark's other instruments
(mob heads) are vanilla on both editions (Bedrock 1.20+ plays head sounds), so only the amethyst
sound is added (`enableAmethystSound`).

### AutomaticRecipeUnlock (tweaks, on)
On login every player is given every recipe of the server (except `ignoredRecipes`).
`forceLimitedCrafting` turns the limited-crafting game rule on. `disableRecipeBook` closes and hides
the recipe book in crafting screens. The client drops the "new recipes" toast when more than 100
recipes are unlocked in the first second of a session.

### ArmedArmorStands (tweaks, on)
Java armor stands get arms by default, so they can hold an item plus a shield. Bedrock armor stands
always have arms and hold a main-hand item, so the only behaviour delta is the shield: a player
using an armor stand with a **shield** puts it in the stand's **off hand** (Java's equipment slot of
a shield) instead of the main hand.

### GameNerfs (experimental, off)
Every nerf toggle Quark has:
- `nerfMending`: XP orbs never repair items (Unmending). Combining an item that has Mending with the
  same item or an enchanted book in an anvil removes Mending, fully repairs the result, resets its
  repair cost to 0 and costs 5 levels. `noNerfForMendingTwo`: items with Mending II+ still repair
  from XP, and Mending II+ is kept. Items with a repair cost > 0 show the tooltip line
  "(Was Repaired)".
- `disableIronFarms`: iron golems drop no iron ingots on death.
- `disableIceRoads`: boats use a ground friction of 0.45 (Java ice is 0.98), so ice roads are slow.
- `disableWoolDrops`: sheep drop no wool on death.
- `enableSelectiveMobGriefing` + `nonGriefingEntities`: the listed mobs behave as if `mobGriefing`
  were off (no block damage from their explosions, no block take/place/eat/break-door/raid goals).
- `enableDimensionLockedElytra` + `elytraAllowedDimensions`: elytra only glide in the listed
  dimensions.
- `disableFallingBlockDupe`: a removed falling block stops ticking (no dupe through portals).
- `disablePistonPhysicsExploits`: Java piston update-order fixes (see §7).
- `disableMushroomBlockRemoval`: huge mushroom caps only replace air and leaves.
- `disableTripwireHookDupe`: a tripwire hook update only re-sets hooks that still exist.
- `villageSpawnNerf` + `villageSpawnNerfDistance`: villages near the world spawn are rarer.

### SpawnerReplacer (experimental, off)
Every server tick, a monster spawner whose mob is a key of `replaceMobs` ("from,to" pairs) is
switched to the mapped mob.

### VillagerRerollingRework (experimental, off)
- `seedInitialVillagerTrades`: the first trades a villager generates for a profession are always
  the same for that villager, so breaking and replacing its job site does not reroll them.
- On the first restock of a day (or every restock with `rerollOnAnyRestock`), each out-of-stock
  trade (or each used trade with `rerollEvenIfNotOutOfStock`) is rerolled with chance
  `chanceToRerollWhenRestocking` into a random trade of the same tier, skipping rerolls that
  duplicate an existing trade, at most `maximumRestocksPerDay` per day (-1 unlimited, 0 off).

### Java-only review
| Module | Outcome |
|---|---|
| AdjustableChat | **Ported** (client mod): the HUD chat panel is moved by `horizontalShift` / `verticalShift` UI units. |
| NarratorReadout | **Ported** (client mod): two keybinds read the game state aloud with the Windows speech API. |
| ClimateControlRemover | **Ported**: Bedrock 1.18+ uses the same multi-noise biome lookup; the disabled parameters are neutralised when the biome parameter list is built. |
| OverlayShader | **Not ported** (no runtime post-processing shaders on Bedrock), see §7. |
| MicrocraftingHelper | **Not ported** (Bedrock's recipe book has no ghost recipes), see §7. |

## 2. Blocks and items
None. The unit adds no blocks, items, recipes or textures. Referenced ids: `minecraft:elytra`,
`minecraft:firework_rocket`, `minecraft:campfire`, `minecraft:soul_campfire`, `minecraft:hay_block`,
`lepton:grate` (U5, by id), `minecraft:noteblock`, `minecraft:amethyst_block`,
`minecraft:budding_amethyst`, `minecraft:amethyst_cluster`, `minecraft:{small,medium,large}_amethyst_bud`,
`minecraft:armor_stand`, `minecraft:shield`, `minecraft:mob_spawner`.

## 3. Mechanics

### BetterElytraRocket
- Hook `FireworksItem::use` (server and client). Conditions: module on; the player is not gliding;
  the chest slot is `minecraft:elytra` with durability left (damage < max damage - 1); the elytra
  may glide in this dimension (GameNerfs lock); not riding.
- Server: `Player::startGliding()`, then the vanilla use runs while gliding (it fires the rocket
  attached to the player and consumes one unless creative), then the jump: a vertical speed of
  0.42 is sent with `SetActorMotionPacket`.
- Client mod: the same on the local player (`startGliding` + `jumpFromGround`), so the flight starts
  without waiting for the server (Java does both sides too).
- Log: `misctweaks: elytra rocket launch <player>`.

### CampfiresBoostElytra
- `ServerLevelTickEvent`, every gliding player with `vy < maxSpeed`.
- Scan from the player's block position down while the block is air or `lepton:grate`, inside the
  world height, at most 20 moves.
- The block found must be `minecraft:campfire` or `minecraft:soul_campfire` with `extinguished` =
  false and a **signal fire**. Bedrock has no `signal_fire` state: a campfire is a signal fire when a
  hay bale is directly below it (the same rule that makes its smoke rise high).
- `force = boostStrength`; if `moves > 16`: `force -= force * (1 - (moves - 16) / 4)` (Quark's
  formula as written: it drops to 1/4 at 17 blocks and climbs back to full at 20); soul campfire:
  `force *= -1.5`. New velocity `(vx, min(maxSpeed, vy + force), vz)` sent with
  `SetActorMotionPacket` (Bedrock players move client-side; this is the knockback path).

### MoreNoteBlockSounds
- Hook `NoteBlock::triggerEvent(region, pos, instrument, note)`. Server side, with
  `enableAmethystSound`, an instrument below 16 (16+ are the mob heads on top) and an amethyst block
  below: the instrument is not played;
  instead `PlaySoundPacket` `chime.amethyst_block` (volume 1, pitch `2^((note-12)/12)`) goes to the
  players near the block, and the note particle (`LevelEvent` legacy particle `Note`, data = note)
  is broadcast at `pos + (0.5, 1.2, 0.5)`. Client side (mod client) the harp is skipped too.

### AutomaticRecipeUnlock
- `PlayerJoinEvent`: runs `recipe give "<player>" *`, then `recipe take "<player>" <id>` for every
  `ignoredRecipes` entry, then `gamerule dolimitedcrafting true` when `forceLimitedCrafting`.
  Log: `misctweaks: unlocked all recipes for <player>`.
- `disableRecipeBook`: on join the server sends `SetPlayerInventoryOptionsPacket` with both
  inventory layouts set to "inventory only" (no recipe book); when the client changes them back
  (the recipe book button) the server sends them again. Works for vanilla clients too.
- Client mod: `UnlockedRecipesPacket` of type "newly unlocked" with more than 100 recipes is applied
  as "initially unlocked" (no toast).
- `skipEmptyRecipes`, `filterRecipeAdvancements`: see §7.

### ArmedArmorStands
- Hook `ArmorStand::_trySwapItem(player, slot)`: when the slot is the main hand, the player's item is
  a shield and the stand's off hand is empty, the swap goes to the off hand. A stand holding a shield
  in the off hand gives it back when the player uses it with an empty hand on that side, as vanilla.

### GameNerfs
- Mending: hook `EnchantUtils::getRandomDamagedItemWithMending` (XP orb repair): returns nothing;
  with `noNerfForMendingTwo` it picks a random damaged equipped item with Mending level > 1.
- Anvil: hook `AnvilContainerScreenValidator::getCraftResult` (server-authoritative anvil result).
  If the anvil input or material has Mending (level 1, or any level without `noNerfForMendingTwo`)
  and the material is the same item or an enchanted book: the result loses Mending, gets damage 0
  and repair cost 0. Tooltip (client mod): "(Was Repaired)" in yellow for items with a repair cost.
- Drops: hook `Spawner::spawnItem` / `BedrockSpawner::spawnItem`: a dead iron golem spawning iron
  ingots (`disableIronFarms`), or a dead sheep spawning wool (`disableWoolDrops`), spawns nothing.
- Griefing: hooks `Explosion::explode` (source in the list: no block breaking, no fire) and
  `canUse()` of `TakeBlockGoal`, `PlaceBlockGoal`, `EatBlockGoal`, `BreakDoorGoal`,
  `RaidGardenGoal` (false for listed mobs).
- Elytra lock: hook `Player::startGliding` (server, client) and stop gliding every tick when the
  dimension is not allowed. Dimension names accept Java and Bedrock spellings
  (`minecraft:the_nether` = `minecraft:nether`).
- Ice roads: hook `Boat::normalTick`: when the boat is on the ground (not in water) the horizontal
  velocity is scaled by `0.45 / friction` of the block below, i.e. ground friction 0.45 as in Quark
  (Quark returns 0.45 for every ground block, so boats on any land are a bit slower too).
- Falling block: hook `FallingBlockActor::normalTick`: skip when the actor is removed.
- Mushrooms: hook `HugeMushroomFeature::placeFixed`: cap blocks that replaced anything but air or
  leaves are restored. (Tested: Bedrock's own growth already refuses to grow a huge mushroom whose
  cap space holds glass or stone, so the hook is a guard for any other path.)
- Tripwire hooks: hook `TripWireHookBlock::calculateState`: a tripwire hook set by the update on a
  position that was not a tripwire hook before is reverted.
- Villages: hook `VillageFeature::isFeatureChunk`: with `p = 1 - d^2 / villageSpawnNerfDistance^2`
  (`d` = distance from the world spawn to the chunk), a deterministic random (seeded by the chunk
  position) below `p` re-picks the town centre 3 times looking for a zombie (abandoned) village
  (2% each, Java's plains pool weight); if none is found there is no village.

### SpawnerReplacer
- `replaceMobs` entries `"from,to"` (both entity ids, `minecraft:` optional) parsed on load/reload.
- Hook `BaseMobSpawner::tick` (server; the spawner block actor ticks through it): the mob (read
  with `save()`, `EntityIdentifier`) in the map and a `minecraft:mob_spawner` block at the spawner's
  `getPos()` -> `MobSpawnerBlockActor::setMob(region, to)` (updates clients too).
  Log: `misctweaks: spawner at <pos> <from> -> <to>`.

### VillagerRerollingRework
- Per-villager data in the world DB (`lp:misctweaks:villager:<uniqueId>`: `trades`, `day`, `rerolls`),
  removed when the villager dies.
- First trades: hook `EconomyTradeableComponent::_generateTrades` (Bedrock generates the trades of
  every tier at once, into an empty offer list). With the module and `seedInitialVillagerTrades` on
  and no offers yet: the first list generated for a trade table (profession) is kept
  (`MerchantRecipeList::createTag`, SNBT under `trades.<table path>`); every later first generation
  for that table loads the kept list instead. Java seeds the villager's random for that call; the
  Bedrock generation does not draw from a random we can seed, so the kept list gives the same result
  (same villager + same profession = same first trades).
- Reroll: hook `EconomyTradeableComponent::resupplyTrades`. New day (`time / 24000`) -> rerolls = 0
  and, unless `rerollOnAnyRestock`, a reroll attempt before the restock; with `rerollOnAnyRestock`
  the attempt runs after every restock. Attempt: as in Quark (chance per trade from the villager's
  random, tier of the old trade, duplicates skipped, reward-XP flag = new && old, counter).
  The candidate trade is generated by Bedrock's own trade table code (`_generateTrades` into a
  fresh list while the villager's own list is set aside) and one recipe of the same tier is picked
  at random.

### AdjustableChat (client mod)
- After each HUD frame the chat panel control of `hud_screen` (`chat_panel`) is moved by
  (`horizontalShift`, `verticalShift`) relative to its laid-out position (re-applied when the game
  lays it out again). Hidden HUD (F1): nothing to move.

### NarratorReadout (client mod)
- Keybinds `narrator_readout` and `narrator_full_readout` (unbound by default, as in Quark; set them
  in Controls or `config/keybinds.json`), also active in screens; 10 ticks of cooldown.
- Speech: Windows SAPI (`ISpVoice::Speak`, async, purging the previous text = Java's interrupt).
- Readout without a screen: "Looking at <block>" (+ ", " in full mode), sign text
  (". Sign Says <lines>. " for front and back) for signs; full mode adds "Holding <n>x <item>" /
  "Holding <n> <item> and <m> <item>", "Health <n>", "Food <n>". Not in game: "Not ingame".
  In a container screen: the hovered slot's item tooltip (first line, all lines in full mode) or
  "No item". Other screens: the screen's name.

### ClimateControlRemover
- Hook the `ParameterList` constructor (the multi-noise biome table of a dimension; installed at mod
  load because BDS builds its world before mods are enabled): for each biome
  target the disabled parameters (temperature, humidity, continentalness, erosion, depth, weirdness)
  become the dummy range `[0, 0]` in the parameter space and the climate parameters, and the offset
  becomes 0 (`disableOffset`). With every biome sharing the same dummy value, the parameter adds the
  same distance to every candidate, so it no longer influences the choice (Quark's mixin returns 0
  distance for it). Applies to worlds loaded after the change (biome tables are built on load).

## 4. Screens
- Recipe book: hidden through the inventory options packet (vanilla and mod clients).
- AdjustableChat moves the vanilla chat panel (mod client only; vanilla clients keep the default).
- No other screens. Vanilla clients have no keybinds (NarratorReadout needs the client mod).

## 5. Config
Section `misctweaks` (`config/misctweaks.json`), keys and defaults as in SPEC §5.1 (U27):

| Key | Type | Default |
|---|---|---|
| `version` | int | 1 |
| `enableBetterElytraRocket` | bool | true |
| `enableCampfiresBoostElytra` | bool | true |
| `boostStrength` | double | 0.5 |
| `maxSpeed` | double | 1 |
| `enableMoreNoteBlockSounds` | bool | true |
| `enableAmethystSound` | bool | true |
| `enableAutomaticRecipeUnlock` | bool | true |
| `ignoredRecipes` | string[] | [] |
| `forceLimitedCrafting` | bool | false |
| `disableRecipeBook` | bool | false |
| `skipEmptyRecipes` | bool | true |
| `filterRecipeAdvancements` | bool | true |
| `enableArmedArmorStands` | bool | true |
| `enableGameNerfs` | bool | false |
| `nerfMending` | bool | true |
| `noNerfForMendingTwo` | bool | false |
| `disableIronFarms` | bool | true |
| `disableIceRoads` | bool | true |
| `disableWoolDrops` | bool | true |
| `enableSelectiveMobGriefing` | bool | true |
| `enableDimensionLockedElytra` | bool | true |
| `disableFallingBlockDupe` | bool | true |
| `disablePistonPhysicsExploits` | bool | true |
| `disableMushroomBlockRemoval` | bool | true |
| `disableTripwireHookDupe` | bool | true |
| `villageSpawnNerf` | bool | false |
| `villageSpawnNerfDistance` | int | 7000 |
| `nonGriefingEntities` | string[] | ["minecraft:creeper", "minecraft:enderman"] |
| `elytraAllowedDimensions` | string[] | ["minecraft:the_end"] |
| `enableSpawnerReplacer` | bool | false |
| `replaceMobs` | string[] | [] (entries `"mob1,mob2"`) |
| `enableVillagerRerollingRework` | bool | false |
| `seedInitialVillagerTrades` | bool | true |
| `chanceToRerollWhenRestocking` | double | 0.25 (0..1) |
| `maximumRestocksPerDay` | int | 3 |
| `rerollOnAnyRestock` | bool | false |
| `rerollEvenIfNotOutOfStock` | bool | false |
| `enableMicrocraftingHelper` | bool | true (kept for parity; nothing to do, §7) |
| `enableAdjustableChat` | bool | false |
| `horizontalShift` | int | 0 |
| `verticalShift` | int | 0 |
| `enableClimateControlRemover` | bool | false |
| `disableTemperature` .. `disableOffset` | bool | false |
| `enableNarratorReadout` | bool | false |
| `enableOverlayShader` | bool | false (kept for parity, §7) |
| `shader` | string | "none" |

Client-side options (AdjustableChat, NarratorReadout, the toast filter, the tooltip) are read from
the client's own `config/misctweaks.json`, as Quark reads its client config.

## 6. Strings
| Key | en_US |
|---|---|
| `quark.misc.repaired` | (Was Repaired) |
| `quark.readout.not_ingame` | Not ingame |
| `quark.readout.looking` | Looking at {} |
| `quark.readout.holding` | Holding {}x {} |
| `quark.readout.holding_with_off` | Holding {} {} and {} {} |
| `quark.readout.health` | Health {} |
| `quark.readout.food` | Food {} |
| `quark.readout.sign_says` | . Sign Says |
| `quark.readout.no_item` | No item |

Keybind names (RP `texts.d`): `key.lepton.narrator_readout` = "Narrator Readout",
`key.lepton.narrator_full_readout` = "Narrator Full Readout".

## 7. Bedrock differences / not ported
- **CampfiresBoostElytra**: Bedrock campfires have no `signal_fire` state; a hay bale below the
  campfire is the signal rule on Bedrock (as for the smoke). Speeds are sent as motion packets (the
  server cannot set a player's speed otherwise), so the push follows the server's view of the
  player's speed.
- **MoreNoteBlockSounds**: only the amethyst sound is missing on Bedrock (mob heads are vanilla).
  A vanilla client that also plays note blocks locally may hear the harp under the chime.
- **AutomaticRecipeUnlock**: `skipEmptyRecipes` has nothing to skip (Bedrock recipes always have an
  output); `filterRecipeAdvancements` has no meaning (Bedrock has no recipe advancements); both keys
  are kept and read. `ignoredRecipes` uses Bedrock recipe ids (`minecraft:...` as the recipe JSON
  identifier); an ignored recipe is taken away after the unlock, so it is locked even if the player
  had unlocked it the vanilla way. The recipe-book button cannot be removed from the vanilla screen;
  the book is forced closed instead.
- **ArmedArmorStands**: Bedrock armor stands always have arms (Java's missing-arms default does not
  exist); only the shield-to-off-hand rule is added.
- **GameNerfs**: the anvil's level cost keeps Bedrock's computed cost (the cost is committed by an
  internal, non-exported part of the anvil validation; Quark sets 5). Quark also builds a result
  when vanilla has none (an incompatible combination); Bedrock's validator only accepts results the
  client predicted, so Lepton only nerfs results the game produced. The mod client's anvil preview
  shows the vanilla result; the result taken is the nerfed one. `disableIceRoads` changes boats driven
  by the integrated server or the client mod; a vanilla client on BDS drives its boat with its own
  physics. `disablePistonPhysicsExploits` **has no effect**: Quark's fix patches three update-order
  spots of Java's `PistonBaseBlock.moveBlocks` / `PistonMovingBlockEntity.tick` (stale state map,
  missing notify flag); Bedrock pistons are a different implementation (`PistonBlockActor` attached
  block lists, moving blocks spawned in `_spawnMovingBlocks`) without those code paths, so there is
  nothing equivalent to patch. Villages: Bedrock picks the abandoned (zombie) variant for the whole
  village, so the re-pick is a 3 x 2% chance to keep the village instead of Java's per-piece pool.
- **VillagerRerollingRework**: the seed is not carried through a zombie conversion (Bedrock
  transforms villagers into a new entity); Quark's special price difference has no Bedrock
  counterpart (Bedrock prices come from demand and cured discounts, which are kept).
- **NarratorReadout**: speech uses the Windows speech API directly (Java uses its narrator library);
  for non-container screens Bedrock has no narration message, so the screen name is read. Quark
  calls `acceptInput(down || full, down)`, so its "Narrator Readout" key gives the full readout and
  "Narrator Full Readout" the short one; Lepton keeps that. The hovered item of a container screen
  is the last item whose hover text the game built in the last 0.5 s.
- **OverlayShader** (not ported): Java loads a post-processing shader chain (`PostChain`,
  `shaders/post/*.json`) at runtime through the Camera module. Bedrock's RenderDragon renderer
  only uses precompiled materials shipped in resource packs; there is no API for a full-screen
  post effect, and the client headers expose no post-processing hook (the Vibrant Visuals color
  grading is a fixed pack setting and cannot simulate colour blindness). The keys are kept.
- **MicrocraftingHelper** (not ported): it extends Java's recipe book "ghost recipe" (the
  translucent ingredients shown in the grid when a recipe is missing items): right-clicking a ghost
  ingredient fills the grid with that ingredient's recipe. Bedrock's recipe book has no ghost
  recipe (the client headers contain no such state; uncraftable recipes are only shown in red and
  cannot be placed), so there is no ingredient to right-click. Bedrock's recipe book already
  crafts directly from the book. The key is kept.

## 8. Tests
Selftests (dx 2700..2799, each in this unit's 5-block row):
- `misctweaks.config` - defaults match Quark's; dimension and id spellings.
- `misctweaks.elytra_rocket` (dx 2700) - a simulated player in the air uses a rocket: without an
  elytra nothing happens; wearing one it glides and a rocket is fired at it.
- `misctweaks.campfire_force` (dx 2705) - force for a lit signal campfire, the fade past 16
  blocks, nothing past 20, soul campfire x -1.5, unlit and non-signal campfires.
- `misctweaks.note_amethyst` (dx 2710) - a punched note block on amethyst plays the chime, on stone
  it doesn't.
- `misctweaks.recipe_unlock` - the recipe command succeeds for a player.
- `misctweaks.armor_stand_shield` (dx 2715) - a shield goes to the stand's off hand, a sword to the
  main hand.
- `misctweaks.mending_nerf` (dx 2718) - anvil result transformation (same item, book, material,
  Mending II); real experience orbs: vanilla repairs, nerfed doesn't, Mending II does with
  `noNerfForMendingTwo`.
- `misctweaks.no_iron_drops` (dx 2720), `misctweaks.no_wool_drops` (dx 2725) - killed iron golems /
  sheep drop iron / wool without the nerf and none with it.
- `misctweaks.griefing` (dx 2730) - a creeper's explosion breaks no blocks with the nerf; a pig's
  does; without the nerf the creeper's does.
- `misctweaks.mushroom` (dx 2735) - with the nerf a huge mushroom still grows (through the hook);
  obstructed growth replaces nothing.
- `misctweaks.village_nerf` - the probability formula.
- `misctweaks.spawner` (dx 2740) - a pig spawner becomes a cow spawner.
- `misctweaks.villager_seed` (dx 2745) - regenerating the first trades gives the same trades;
  rerolling respects `maximumRestocksPerDay`.
- `misctweaks.climate` - parameter neutralisation.
- `misctweaks.dimension_elytra` (dx 2750) - gliding is refused outside the allowed dimensions and
  stops when the dimension is no longer allowed.
- `misctweaks.ice_roads` (dx 2755) - a boat pushed over packed ice slides much less with the nerf.

E2E: `tools/e2e/server/scenarios/misctweaks.ps1` runs `misctweaks.*` and checks the pass count and
the log lines. `misctweaks-worldgen` (DEFAULT world, ClimateControlRemover and the village nerf on)
checks that the biome tables were neutralised and that a village search goes through the nerf.
`tools/e2e/client/scenarios/misctweaks.ps1` (local, remote-mod, remote-vanilla):
recipe unlock without a toast, the shifted chat, the inventory without recipe book, the narrator
readout (block, hovered slot), the shield on an armor stand, the anvil Mending nerf and the
"(Was Repaired)" tooltip, and a rocket launch with an elytra (screenshots).