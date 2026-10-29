# Lepton behaviour spec

What Lepton reproduces from the Java mod [Quark](https://modrinth.com/mod/quark) **4.1-488** (Vazkii,
tag `release-4.1-488+1.21.1` of [VazkiiMods/Quark](https://github.com/VazkiiMods/Quark)). This is the
behaviour reference for every feature unit (see [AGENTS.md](../AGENTS.md)). It describes behaviour
only: never copy code or assets from Quark (CC BY-NC-SA 3.0).

This file holds the cross-cutting parts (overview, module inventory, conventions, configuration,
commands, work plan). The details of each feature unit live in `docs/spec/<unit>.md`, written by
the unit itself (template: [docs/spec/README.md](spec/README.md)).

## 1. Overview

Lepton is a LeviLamina native mod for Minecraft Bedrock **1.26.51** (LeviLamina **26.51.7**) that
reproduces the behaviour of Quark 4.1-488 as closely as Bedrock allows: every module, its blocks,
items, mobs, mechanics, screens, keybinds, config options and English strings.

- **One code base, two builds.** The server build runs on the dedicated server (BDS, `plugins/`).
  The client build runs in the game client (LeviLauncher, `mods/`) and covers local worlds through
  the integrated server plus the client-only features (HUD, tooltips, keybinds, native screens).
- **Packs.** A behavior pack (blocks, items, entities, recipes, loot tables) and a resource pack
  (textures, models, names, sounds) ship with the mod. On BDS the resource pack is put on the world
  and sent to every joining client, so players without the client mod see the right blocks.
- **Players with the client mod** get native screens and client features. The server and the
  client mod talk over the companion protocol (`src/net`): custom LeviLamina runtime packets with a
  handshake. The server only sends mod packets to players whose client completed the handshake
  (`net::hasClientMod(player)`), so vanilla clients never receive them.
- **Vanilla clients** (no client mod, e.g. console or phone players on a Lepton server) get
  fallbacks: forms (`game/Forms`) or the vanilla container screen (`game/VirtualContainer`) instead of
  native screens, and the resource pack for visuals. Features that are purely client-side in Quark
  (HUD, tooltips, keybinds) need the client mod.
- **Ids** are `lepton:<path>`, where `<path>` is Quark's registry path (`quark:azalea_planks` ->
  `lepton:azalea_planks`). Lepton-only ids use a descriptive path in the same style.
- **Policy.** No code or assets are copied from Quark. Textures are original pixel art drawn by
  `tools/textures/*.ps1`. English UI strings match Quark's wording (they are behaviour) and are written
  into Lepton's own lang files, with a Chinese (zh_CN) translation.

## 2. Modules, blocks and items

### 2.1 Module inventory

Every Quark 4.1-488 module (`*Module.java` under `content/*/module/` and `addons/oddities/module/`),
the unit that ports it and its enable key. "Default" is Quark's `enabledByDefault` (all modules of
the `experimental` category are off). 163 modules.

| Quark category | Module | Unit | Enable key | Default |
|---|---|---|---|---|
| automation | ChainsConnectBlocks | U10 redstone | `enableChainsConnectBlocks` | true |
| automation | Chute | U11 logistics | `enableChute` | true |
| automation | DispensersPlaceBlocks | U10 redstone | `enableDispensersPlaceBlocks` | true |
| automation | EnderWatcher | U10 redstone | `enableEnderWatcher` | true |
| automation | FeedingTrough | U11 logistics | `enableFeedingTrough` | true |
| automation | Gravisand | U10 redstone | `enableGravisand` | true |
| automation | IronRod | U10 redstone | `enableIronRod` | true |
| automation | MetalButtons | U10 redstone | `enableMetalButtons` | true |
| automation | ObsidianPlate | U10 redstone | `enableObsidianPlate` | true |
| automation | PistonsMoveTileEntities | U10 redstone | `enablePistonsMoveTileEntities` | true |
| automation | RedstoneRandomizer | U10 redstone | `enableRedstoneRandomizer` | true |
| building | CelebratoryLamps | U2 stones | `enableCelebratoryLamps` | true |
| building | CompressedBlocks | U8 storage | `enableCompressedBlocks` | true |
| building | DuskboundBlocks | U4 bricks | `enableDuskboundBlocks` | true |
| building | FramedGlass | U5 palette | `enableFramedGlass` | true |
| building | GlassItemFrame | U24 frames | `enableGlassItemFrame` | true |
| building | GoldBars | U5 palette | `enableGoldBars` | true |
| building | Grate | U5 palette | `enableGrate` | true |
| building | Hedges | U6 woodvariants | `enableHedges` | true |
| building | HollowLogs | U6 woodvariants | `enableHollowLogs` | true |
| building | IndustrialPalette | U5 palette | `enableIndustrialPalette` | true |
| building | JapanesePalette | U5 palette | `enableJapanesePalette` | true |
| building | LeafCarpet | U6 woodvariants | `enableLeafCarpet` | true |
| building | Midori | U4 bricks | `enableMidori` | true |
| building | MoreBrickTypes | U4 bricks | `enableMoreBrickTypes` | true |
| building | MoreMudBlocks | U4 bricks | `enableMoreMudBlocks` | true |
| building | MorePottedPlants | U8 storage | `enableMorePottedPlants` | true |
| building | MoreStoneVariants | U2 stones | `enableMoreStoneVariants` | true |
| building | NetherBrickFenceGate | U5 palette | `enableNetherBrickFenceGate` | true |
| building | RainbowLamps | U12 crystals | `enableRainbowLamps` | true |
| building | RawMetalBricks | U2 stones | `enableRawMetalBricks` | true |
| building | Rope | U8 storage | `enableRope` | true |
| building | ShearVines | U8 storage | `enableShearVines` | true |
| building | Shingles | U5 palette | `enableShingles` | true |
| building | SoulSandstone | U4 bricks | `enableSoulSandstone` | true |
| building | Stools | U8 storage | `enableStools` | true |
| building | SturdyStone | U2 stones | `enableSturdyStone` | true |
| building | Thatch | U5 palette | `enableThatch` | true |
| building | VariantBookshelves | U6 woodvariants | `enableVariantBookshelves` | true |
| building | VariantChests | U9 variantcontainers | `enableVariantChests` | true |
| building | VariantFurnaces | U9 variantcontainers | `enableVariantFurnaces` | true |
| building | VariantLadders | U6 woodvariants | `enableVariantLadders` | true |
| building | VerticalPlanks | U6 woodvariants | `enableVerticalPlanks` | true |
| building | VerticalSlabs | U1 vslabs | `enableVerticalSlabs` | true |
| building | WoodenPosts | U6 woodvariants | `enableWoodenPosts` | true |
| client | AutoWalkKeybind | U16 hud | `enableAutoWalkKeybind` | true |
| client | BackButtonKeybind | U16 hud | `enableBackButtonKeybind` | true |
| client | BucketsShowInhabitants | U18 cosmetics | `enableBucketsShowInhabitants` | true |
| client | Camera | U18 cosmetics | `enableCamera` | true |
| client | ChestSearching | U15 inventory | `enableChestSearching` | true |
| client | ElytraIndicator | U16 hud | `enableElytraIndicator` | true |
| client | GreenerGrass | U18 cosmetics | `enableGreenerGrass` | true |
| client | ImprovedTooltips | U16 hud | `enableImprovedTooltips` | true |
| client | LongRangePickBlock | U17 placement | `enableLongRangePickBlock` | true |
| client | MicrocraftingHelper | U27 misctweaks | `enableMicrocraftingHelper` | true |
| client | SoulCandles | U18 cosmetics | `enableSoulCandles` | true |
| client | UsageTicker | U16 hud | `enableUsageTicker` | true |
| client | UsesForCurses | U18 cosmetics | `enableUsesForCurses` | true |
| client | VariantAnimalTextures | U18 cosmetics | `enableVariantAnimalTextures` | true |
| client | WoolShutsUpMinecarts | U18 cosmetics | `enableWoolShutsUpMinecarts` | true |
| experimental | AdjustableChat | U27 misctweaks | `enableAdjustableChat` | **false** |
| experimental | ClimateControlRemover | U27 misctweaks | `enableClimateControlRemover` | **false** |
| experimental | GameNerfs | U27 misctweaks | `enableGameNerfs` | **false** |
| experimental | NarratorReadout | U27 misctweaks | `enableNarratorReadout` | **false** |
| experimental | OverlayShader | U27 misctweaks | `enableOverlayShader` | **false** |
| experimental | SpawnerReplacer | U27 misctweaks | `enableSpawnerReplacer` | **false** |
| experimental | VanillaStoneClusters | U3 newstones | `enableVanillaStoneClusters` | **false** |
| experimental | VariantSelector | U17 placement | `enableVariantSelector` | **false** |
| experimental | VillagerRerollingRework | U27 misctweaks | `enableVillagerRerollingRework` | **false** |
| management | AutomaticToolRestock | U15 inventory | `enableAutomaticToolRestock` | true |
| management | EasyTransferring | U15 inventory | `enableEasyTransferring` | true |
| management | ExpandedItemInteractions | U15 inventory | `enableExpandedItemInteractions` | true |
| management | HotbarChanger | U15 inventory | `enableHotbarChanger` | true |
| management | InventorySorting | U15 inventory | `enableInventorySorting` | true |
| management | ItemSharing | U15 inventory | `enableItemSharing` | true |
| management | QuickArmorSwapping | U15 inventory | `enableQuickArmorSwapping` | true |
| mobs | Crabs | U20 mobsoverworld | `enableCrabs` | true |
| mobs | Forgotten | U21 mobsdeep | `enableForgotten` | true |
| mobs | Foxhound | U21 mobsdeep | `enableFoxhound` | true |
| mobs | Shiba | U20 mobsoverworld | `enableShiba` | true |
| mobs | Stonelings | U21 mobsdeep | `enableStonelings` | true |
| mobs | Toretoise | U20 mobsoverworld | `enableToretoise` | true |
| mobs | Wraith | U21 mobsdeep | `enableWraith` | true |
| oddities | Backpack | U14 backpack | `enableBackpack` | true |
| oddities | Crate | U11 logistics | `enableCrate` | true |
| oddities | Magnets | U12 crystals | `enableMagnets` | true |
| oddities | MatrixEnchanting | U13 enchanting | `enableMatrixEnchanting` | true |
| oddities | Pipes | U11 logistics | `enablePipes` | true |
| oddities | TinyPotato | U23 gadgets | `enableTinyPotato` | true |
| oddities | TotemOfHolding | U14 backpack | `enableTotemOfHolding` | true |
| tools | Abacus | U23 gadgets | `enableAbacus` | true |
| tools | AmbientDiscs | U23 gadgets | `enableAmbientDiscs` | true |
| tools | AncientTomes | U13 enchanting | `enableAncientTomes` | true |
| tools | BeaconRedirection | U12 crystals | `enableBeaconRedirection` | true |
| tools | BottledCloud | U22 throwables | `enableBottledCloud` | true |
| tools | ColorRunes | U13 enchanting | `enableColorRunes` | true |
| tools | EndermoshMusicDisc | U23 gadgets | `enableEndermoshMusicDisc` | true |
| tools | ParrotEggs | U22 throwables | `enableParrotEggs` | true |
| tools | PathfinderMaps | U23 gadgets | `enablePathfinderMaps` | true |
| tools | Pickarang | U22 throwables | `enablePickarang` | true |
| tools | SeedPouch | U14 backpack | `enableSeedPouch` | true |
| tools | SkullPikes | U22 throwables | `enableSkullPikes` | true |
| tools | SlimeInABucket | U22 throwables | `enableSlimeInABucket` | true |
| tools | TorchArrow | U22 throwables | `enableTorchArrow` | true |
| tools | Trowel | U17 placement | `enableTrowel` | true |
| tweaks | ArmedArmorStands | U27 misctweaks | `enableArmedArmorStands` | true |
| tweaks | AutomaticRecipeUnlock | U27 misctweaks | `enableAutomaticRecipeUnlock` | true |
| tweaks | BetterElytraRocket | U27 misctweaks | `enableBetterElytraRocket` | true |
| tweaks | CampfiresBoostElytra | U27 misctweaks | `enableCampfiresBoostElytra` | true |
| tweaks | CompassesWorkEverywhere | U23 gadgets | `enableCompassesWorkEverywhere` | true |
| tweaks | CoralOnCactus | U25 farming | `enableCoralOnCactus` | true |
| tweaks | DiamondRepair | U26 mobtweaks | `enableDiamondRepair` | true |
| tweaks | DoubleDoorOpening | U26 mobtweaks | `enableDoubleDoorOpening` | true |
| tweaks | DragonScales | U26 mobtweaks | `enableDragonScales` | true |
| tweaks | DyeableItemFrames | U24 frames | `enableDyeableItemFrames` | true |
| tweaks | Emotes | U19 emotes | `enableEmotes` | true |
| tweaks | EnhancedLadders | U17 placement | `enableEnhancedLadders` | true |
| tweaks | GlassShard | U8 storage | `enableGlassShard` | true |
| tweaks | GoldToolsHaveFortune | U26 mobtweaks | `enableGoldToolsHaveFortune` | true |
| tweaks | GrabChickens | U25 farming | `enableGrabChickens` | true |
| tweaks | HoeHarvesting | U25 farming | `enableHoeHarvesting` | true |
| tweaks | HorsesSwim | U25 farming | `enableHorsesSwim` | true |
| tweaks | ImprovedSponges | U26 mobtweaks | `enableImprovedSponges` | true |
| tweaks | LockRotation | U17 placement | `enableLockRotation` | true |
| tweaks | MagmaKeepsConcretePowder | U26 mobtweaks | `enableMagmaKeepsConcretePowder` | true |
| tweaks | MapWashing | U24 frames | `enableMapWashing` | true |
| tweaks | MoreBannerLayers | U24 frames | `enableMoreBannerLayers` | true |
| tweaks | MoreNoteBlockSounds | U27 misctweaks | `enableMoreNoteBlockSounds` | true |
| tweaks | MoreVillagers | U18 cosmetics | `enableMoreVillagers` | true |
| tweaks | NoDurabilityOnCosmetics | U26 mobtweaks | `enableNoDurabilityOnCosmetics` | true |
| tweaks | PatTheDogs | U19 emotes | `enablePatTheDogs` | true |
| tweaks | PetalsOnWater | U25 farming | `enablePetalsOnWater` | true |
| tweaks | PigLitters | U25 farming | `enablePigLitters` | true |
| tweaks | PoisonPotatoUsage | U25 farming | `enablePoisonPotatoUsage` | true |
| tweaks | ReacharoundPlacing | U17 placement | `enableReacharoundPlacing` | true |
| tweaks | RenewableSporeBlossoms | U25 farming | `enableRenewableSporeBlossoms` | true |
| tweaks | ReplaceScaffolding | U17 placement | `enableReplaceScaffolding` | true |
| tweaks | SaferCreatures | U26 mobtweaks | `enableSaferCreatures` | true |
| tweaks | ShulkerPacking | U26 mobtweaks | `enableShulkerPacking` | true |
| tweaks | SimpleHarvest | U25 farming | `enableSimpleHarvest` | true |
| tweaks | SlabsToBlocks | U1 vslabs | `enableSlabsToBlocks` | true |
| tweaks | SlimesToMagmaCubes | U26 mobtweaks | `enableSlimesToMagmaCubes` | true |
| tweaks | SnowGolemPlayerHeads | U26 mobtweaks | `enableSnowGolemPlayerHeads` | true |
| tweaks | UtilityRecipes | U8 storage | `enableUtilityRecipes` | true |
| tweaks | VexesDieWithTheirMasters | U26 mobtweaks | `enableVexesDieWithTheirMasters` | true |
| tweaks | VillagersFollowEmeralds | U25 farming | `enableVillagersFollowEmeralds` | true |
| tweaks | ZombieVillagersOnNormal | U26 mobtweaks | `enableZombieVillagersOnNormal` | true |
| world | AncientWood | U7 newwoods | `enableAncientWood` | true |
| world | AzaleaWood | U7 newwoods | `enableAzaleaWood` | true |
| world | BigStoneClusters | U3 newstones | `enableBigStoneClusters` | true |
| world | BlossomTrees | U7 newwoods | `enableBlossomTrees` | true |
| world | CherryGroveWaterPetals | U28 worldsurface | `enableCherryGroveWaterPetals` | true |
| world | ChorusVegetation | U29 worldbeyond | `enableChorusVegetation` | true |
| world | Corundum | U12 crystals | `enableCorundum` | true |
| world | FairyRings | U28 worldsurface | `enableFairyRings` | true |
| world | FallenLogs | U28 worldsurface | `enableFallenLogs` | true |
| world | GlimmeringWeald | U28 worldsurface | `enableGlimmeringWeald` | true |
| world | MonsterBox | U28 worldsurface | `enableMonsterBox` | true |
| world | NetherObsidianSpikes | U29 worldbeyond | `enableNetherObsidianSpikes` | true |
| world | NewStoneTypes | U3 newstones | `enableNewStoneTypes` | true |
| world | NoMoreLavaPockets | U29 worldbeyond | `enableNoMoreLavaPockets` | true |
| world | Permafrost | U3 newstones | `enablePermafrost` | true |
| world | SpiralSpires | U29 worldbeyond | `enableSpiralSpires` | true |
### 2.2 Assignments added by U0

These modules are not named in the work plan (§9); U0 placed them by theme:

| Module | Unit | Why |
|---|---|---|
| MicrocraftingHelper | U27 misctweaks | client-only crafting helper; part of the Java-only review |
| AdjustableChat | U27 misctweaks | experimental, client-only; Java-only review |
| ClimateControlRemover | U27 misctweaks | experimental, world-gen tweak; Java-only review |
| NarratorReadout | U27 misctweaks | experimental, client-only; Java-only review |
| OverlayShader | U27 misctweaks | experimental, client-only (post shaders); Java-only review |

Quark 4.1-488 has **no SignEditing module** (vanilla 1.20 made signs editable and Quark dropped it).
"Sign editing" in U24 means: check that Bedrock's own sign editing matches Java's (it does: interact
with a sign to edit it) and document any difference in `docs/spec/frames.md`. The only sign-related
Quark code is the hanging-sign-on-post rule, which belongs to WoodenPosts (U6). Result
(`docs/spec/frames.md` §7.5): no difference, nothing to port.

### 2.3 Blocks and items: conventions

Per-unit detail (ids, states, recipes, drops, tags, creative placement): **see `docs/spec/<unit>.md`**.
Rules shared by every unit:

- **Ids** `lepton:<quark path>`. Block and item names come from the resource pack `texts.d`
  fragments (`tile.lepton:<id>.name`, `item.lepton:<id>.name`), English text as in Quark.
- **Ownership of variants.** A shape or variant block of a material belongs to the unit that owns
  the material: e.g. `lepton:limestone_vertical_slab` is U3's, `lepton:azalea_bookshelf` is U7's.
  U1 (VerticalSlabs) owns only the vertical slabs of **vanilla** materials; U6 owns the variants of
  vanilla woods; U9 owns every chest and furnace, including `lepton:azalea_chest` and the other chests
  of U7's woods (azalea, blossom, ancient), whose recipes use U7's planks and logs.
  A unit whose behaviour applies to many materials (vertical slabs, bookshelves, ladders, posts,
  hollow logs, hedges, leaf carpets, chests, ...) implements the behaviour once, keyed by a block
  tag (`lepton:<feature>`, documented in its spec), and material owners add tagged blocks.
- **Shared block tags.** Any unit may put these on its blocks and gets the behaviour from the owner's
  native code: `lepton:requires_tool`, `lepton:soul_fire_base`, `lepton:soul_speed`,
  `lepton:infiniburn`, `lepton:dragon_immune` (U4); `lepton:players_only` for pressure plates and
  `lepton:press_ticks_<n>` with 4 and 100 for buttons (U10, in `game/Shapes`); `lepton:chest` (blocks
  packed by ShulkerPacking) and `lepton:non_double_door` (U26); the item tag
  `lepton:cosmetic_anvil_items` (U26, empty as in Quark). Wooden posts use the states
  `lepton:side_0..3` (the four directions off the axis: y -> N, S, W, E; x -> down, up, N, S; z ->
  down, up, W, E) instead of six `connect_<dir>` states (U6; `New-WvPost` sets them up).
- **Generated shape and wood sets.** Shapes (block, stairs, slab, vertical slab, wall, fence, fence
  gate, pane) come from `New-ShapeSet` and wood sets (log, wood, stripped, planks, ..., door, trapdoor,
  button, pressure plate; signs and boats are not feasible, see `tools/pack/README.md`)
  from `New-WoodSet` in `tools/pack/lib.ps1`. A unit calls them from `tools/pack/<unit>.ps1` and
  commits the generated files. Native placement rules for these shapes (stairs corners, double
  slabs, vertical slab stacking, wall/fence/pane connections, axis pillars) are in `game/Shapes`.
- **Recipes** use Bedrock recipe JSON in the behavior pack (crafting table and stonecutter as in
  Quark). A disabled module must not leave working recipes or obtainable items behind: the unit
  removes or blocks them natively when its `enable<Module>` key is false.
- **Custom blocks have no block entities.** Per-block data lives in block states plus the world DB
  (`game/WorldDb`); containers use `game/VirtualContainer`; custom mobs are behavior-pack entities
  driven through `game/Entities`.

## 3. Mechanics

**See `docs/spec/<unit>.md`.** Shared rules:

- Numbers (chances, ranges, durations, cooldowns) are Quark's defaults and come from the config
  (§5), never hard-coded when Quark makes them configurable.
- Java ticks equal Bedrock ticks (20 per second). Java block/entity semantics map to the closest
  Bedrock equivalent; every difference is listed in the unit spec's "Bedrock differences" section.
- Server-side logic must work for vanilla clients too. Logic that Quark runs on the client (HUD,
  tooltips, input) needs the client mod; when it changes the world (sorting, transferring, emotes,
  ...) the client sends a request over `src/net` and the server validates and applies it.
- World edits from cancelled interaction events are deferred to the next tick (AGENTS.md).

## 4. Screens

**See `docs/spec/<unit>.md`** for every screen and its layout. Shared rules:

- **Mod clients** get the screen drawn natively by the client mod (`src/client/ui`: HUD hook,
  container-screen overlays with buttons / text field / greyed-out slots, tooltip lines, custom
  screens for radial or grid layouts such as the emote wheel and the matrix enchanting table).
- **Vanilla clients** get a fallback that keeps the feature usable: a form (`game/Forms`: modal,
  simple and custom forms with callbacks) or the vanilla container screen (`game/VirtualContainer`).
  Each screen in a unit spec states its fallback, or why there is none (client-only features).
- Screens open from the server: the server decides with `net::hasClientMod(player)` whether to send
  the native screen request or show the fallback.

## 5. Configuration

- One JSON file per unit: `config/<unit>.json` under the mod folder, registered with
  `LP_REGISTER_CONFIG("<unit>", T)`. `T` is a reflectable struct with `int version` (bump it when
  keys change; files are merged with new defaults).
- One `bool enable<ModuleName>` per Quark module, default as in Quark (§2.1; experimental modules
  `false`). When it is false the module does nothing and its content is not obtainable.
- Every Quark option of the module, with Quark's default and Quark's field name (camelCase). When
  two modules of one unit share an option name (or an option would clash with an enable key), the
  key is prefixed with the module name: `stoolsSitHeight`, `crabsSpawnConfig`.
- Structured Quark options (biome/dimension filters, spawn configs, world-gen styles, colours)
  become JSON objects; the unit documents their shape and keeps Quark's values. Java-only parts
  (Forge biome tags without a Bedrock counterpart) map to the closest Bedrock biome tags.
- `config/mod.json` is the foundation section: `language` (default `en_US`), `usePlayerLocale`
  (`true`), `requireResourcePack` (`true`, BDS).
- `config/keybinds.json` (client build): every keybind registered with `LP_REGISTER_KEYBIND`, with
  its key codes; keybinds are also rebindable in the game's controls screen. Quark's keybinds
  (name key, default): `quark.keybind.autorun` (unbound, AutoWalkKeybind), `quark.keybind.back`
  (mouse button 4, BackButtonKeybind), `quark.keybind.camera_mode` (F12, Camera),
  `quark.keybind.change_hotbar` (Z, HotbarChanger), `quark.keybind.sort_player` and the container
  sort button (unbound, InventorySorting), `quark.keybind.transfer_insert` / `transfer_extract` /
  `shift_lock` (unbound, EasyTransferring), `quark.keybind.lock_rotation` (K, LockRotation),
  `quark.keybind.variant_selector` (R, VariantSelector), `quark.keybind.narrator_readout` /
  `narrator_full_readout` (unbound, NarratorReadout), `quark.emote.<name>` (unbound, one per emote,
  Emotes). ItemSharing reuses the chat key.
  Lepton registers them as `key.lepton.<name>` with the same defaults, except where the key is taken
  by vanilla Bedrock (Z = Mob Effects, B = Emote, C / X = copy coordinates, N = toasts, Q, E, T, F1,
  F2, F4, F5, F11): `change_hotbar` defaults to **H**. `back` has no keyboard default; the side mouse
  button is `backMouseButton` in `hud.json` (Bedrock key bindings cannot hold side mouse buttons).
  `share_item` is Shift + T over a slot (the client mod can't read the game's chat key mapping).
  Defaults are unique among Lepton's keybinds.
- Config is global (not per world) and reloaded by `/quark reload`; units re-read values on use or
  through `lp::config::onReload`.

### 5.1 Keys per unit

Generated from Quark 4.1-488's `@Config` fields. "Default" is Quark's value; `structured` rows show
Quark's Java expression for the unit to translate into JSON. Option labels are Quark's config names or
descriptions.

#### U1 `config/vslabs.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableVerticalSlabs` | bool | true | VerticalSlabs module |
| `allowSideConnections` | bool | `true` | VerticalSlabs: Should Walls and Panes attempt to connect to the side of Vertical Slabs? |
| `enableSlabsToBlocks` | bool | true | SlabsToBlocks module |
| `bannedSlabs` | string[] | `[]` | SlabsToBlocks: Slabs that should NOT be convertible back into full-blocks |

#### U2 `config/stones.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableMoreStoneVariants` | bool | true | MoreStoneVariants module |
| `enableBricks` | bool | `true` | MoreStoneVariants |
| `enableChiseledBricks` | bool | `true` | MoreStoneVariants |
| `enablePillar` | bool | `true` | MoreStoneVariants |
| `enableRawMetalBricks` | bool | true | RawMetalBricks module |
| `enableSturdyStone` | bool | true | SturdyStone module |
| `enableCelebratoryLamps` | bool | true | CelebratoryLamps module |
| `lightLevel` | int | `15` | CelebratoryLamps |

#### U3 `config/newstones.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableNewStoneTypes` | bool | true | NewStoneTypes module |
| `enableLimestone` | bool | `true` | NewStoneTypes |
| `enableJasper` | bool | `true` | NewStoneTypes |
| `enableShale` | bool | `true` | NewStoneTypes |
| `enableMyalite` | bool | `true` | NewStoneTypes |
| `newStoneTypesLimestone` | object | structured: `new StoneTypeConfig()` | NewStoneTypes |
| `newStoneTypesJasper` | object | structured: `new StoneTypeConfig()` | NewStoneTypes |
| `newStoneTypesShale` | object | structured: `new StoneTypeConfig()` | NewStoneTypes |
| `newStoneTypesMyalite` | object | structured: `new StoneTypeConfig(DimensionConfig.end(false))` | NewStoneTypes |
| `addNewStonesToMasonTrades` | bool | `true` | NewStoneTypes |
| `enablePermafrost` | bool | true | Permafrost module |
| `generationSettings` | object | structured: `UndergroundStyleConfig.styleBuilder() .style(new PermafrostStyle()) .biomes(CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:frozen...` | Permafrost |
| `enableBigStoneClusters` | bool | true | BigStoneClusters module |
| `calcite` | object | structured: `bob(BiomeTags.IS_MOUNTAIN).build()` | BigStoneClusters |
| `bigStoneClustersLimestone` | object | structured: `bob(Tags.Biomes.IS_SWAMP, BiomeTags.IS_OCEAN).build()` | BigStoneClusters |
| `bigStoneClustersJasper` | object | structured: `bob(BiomeTags.IS_BADLANDS, Tags.Biomes.IS_SANDY).build()` | BigStoneClusters |
| `bigStoneClustersShale` | object | structured: `bob(Tags.Biomes.IS_SNOWY, Tags.Biomes.IS_COLD).build()` | BigStoneClusters |
| `bigStoneClustersMyalite` | object | structured: `AirStoneClusterConfig.airStoneBuilder() .generateInAir(true) .dimensions(DimensionConfig.end(false)) .biomes(CompoundBiomeConfig.fromBiom...` | BigStoneClusters |
| `blocksToReplace` | string[] | `["minecraft:stone", "minecraft:andesite", "minecraft:diorite", "minecraft:granite", "minecraft:netherrack\|minecraft:the_nether", "minecraft:end_stone\|minecraft:the_end", "quark:limestone", "quark:jasper", "quark:slate"]` | BigStoneClusters: Blocks that stone clusters can replace. If you want to make it so it only replaces in one dimension,  |
| `enableVanillaStoneClusters` | bool | false | VanillaStoneClusters module |
| `granite` | object | structured: `bob(BiomeTags.IS_MOUNTAIN).build()` | VanillaStoneClusters |
| `diorite` | object | structured: `bob(Tags.Biomes.IS_SAVANNA, Tags.Biomes.IS_JUNGLE, Tags.Biomes.IS_MUSHROOM).build()` | VanillaStoneClusters |
| `andesite` | object | structured: `bob(BiomeTags.IS_FOREST).build()` | VanillaStoneClusters |

#### U4 `config/bricks.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableMoreBrickTypes` | bool | true | MoreBrickTypes module |
| `enableBlueNetherBricks` | bool | `true` | MoreBrickTypes: This also comes with a utility recipe for Red Nether Bricks |
| `enableSandstoneBricks` | bool | `true` | MoreBrickTypes: This also includes Red Sandstone Bricks and Soul Sandstone Bricks |
| `enableCobblestoneBricks` | bool | `true` | MoreBrickTypes: This also includes Mossy Cobblestone Bricks |
| `enableBlackstoneBricks` | bool | `true` | MoreBrickTypes: Requires Cobblestone Bricks to be enabled |
| `enableDirtBricks` | bool | `true` | MoreBrickTypes: Requires Cobblestone Bricks to be enabled |
| `enableNetherrackBricks` | bool | `true` | MoreBrickTypes: Requires Cobblestone Bricks to be enabled |
| `enableSoulSandstone` | bool | true | SoulSandstone module |
| `enableMoreMudBlocks` | bool | true | MoreMudBlocks module |
| `enableMidori` | bool | true | Midori module |
| `enableDuskboundBlocks` | bool | true | DuskboundBlocks module |

#### U5 `config/palette.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableShingles` | bool | true | Shingles module |
| `enableFramedGlass` | bool | true | FramedGlass module |
| `enableGoldBars` | bool | true | GoldBars module |
| `generateInNetherFortress` | bool | `true` | GoldBars |
| `enableIndustrialPalette` | bool | true | IndustrialPalette module |
| `enableIronPlates` | bool | `true` | IndustrialPalette |
| `enableIronLadder` | bool | `true` | IndustrialPalette |
| `enableJapanesePalette` | bool | true | JapanesePalette module |
| `enablePaperBlocks` | bool | `true` | JapanesePalette |
| `enableBambooMats` | bool | `true` | JapanesePalette |
| `enableNetherBrickFenceGate` | bool | true | NetherBrickFenceGate module |
| `enableThatch` | bool | true | Thatch module |
| `fallDamageMultiplier` | double | `0.5` | Thatch |
| `enableGrate` | bool | true | Grate module |

#### U6 `config/woodvariants.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableVerticalPlanks` | bool | true | VerticalPlanks module |
| `enableVariantBookshelves` | bool | true | VariantBookshelves module |
| `variantBookshelvesChangeNames` | bool | `true` | VariantBookshelves |
| `enableVariantLadders` | bool | true | VariantLadders module |
| `variantLaddersChangeNames` | bool | `true` | VariantLadders |
| `enableWoodenPosts` | bool | true | WoodenPosts module |
| `enableHedges` | bool | true | Hedges module |
| `enableLeafCarpet` | bool | true | LeafCarpet module |
| `enableHollowLogs` | bool | true | HollowLogs module |
| `enableAutoCrawl` | bool | `true` | HollowLogs |

#### U7 `config/newwoods.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableAzaleaWood` | bool | true | AzaleaWood module |
| `replaceAzaleaTreeLog` | bool | `true` | AzaleaWood: If true, the oak log in the vanilla azalea tree ConfiguredFeature will be replaced with Quark's azalea log.... |
| `enableAncientWood` | bool | true | AncientWood module |
| `ancientFruitGivesExp` | bool | `true` | AncientWood |
| `ancientFruitExpValue` | int | `10` | AncientWood |
| `ancientCityLootWeight` | int | `0` | AncientWood: Set to a value other than 0 to enable Ancient City loot chest generation (8 recommended if you do) |
| `ancientCityLootQuality` | int | `1` | AncientWood |
| `sniffingLootWeight` | int | `1` | AncientWood: Set to 0 to disable sniffer sniffing. The vanilla loot table has every entry at weight 1, so without editin... |
| `sniffingLootQuality` | int | `0` | AncientWood |
| `enableBlossomTrees` | bool | true | BlossomTrees module |
| `blue` | object | structured: `new BlossomTreeConfig(200, Tags.Biomes.IS_SNOWY)` | BlossomTrees |
| `lavender` | object | structured: `new BlossomTreeConfig(100, Tags.Biomes.IS_SWAMP)` | BlossomTrees |
| `orange` | object | structured: `new BlossomTreeConfig(100, BiomeTags.IS_SAVANNA)` | BlossomTrees |
| `yellow` | object | structured: `new BlossomTreeConfig(200, Tags.Biomes.IS_PLAINS)` | BlossomTrees |
| `red` | object | structured: `new BlossomTreeConfig(30, BiomeTags.IS_BADLANDS)` | BlossomTrees |
| `dropLeafParticles` | bool | `true` | BlossomTrees |

The blossom tree objects (`blue`, `lavender`, `orange`, `yellow`, `red`) have the shape
`{ "dimensions": { "isBlacklist", "dimensions" }, "biomes": { "tags": { "isBlacklist", "tags" }, "biomes": { "isBlacklist", "biomes" } }, "rarity" }`
(`docs/spec/newwoods.md` §5).

#### U8 `config/storage.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableCompressedBlocks` | bool | true | CompressedBlocks module |
| `burnsForever` | bool | `true` | CompressedBlocks: Charcoal Block and Blaze Lantern Stay On Fire Forever |
| `charcoalBlockFuelTime` | int | `16000` | CompressedBlocks: Charcoal Block Fuel Time |
| `blazeLanternFuelTime` | int | `24000` | CompressedBlocks: Blaze Lantern Fuel Time |
| `stickBlockFuelTime` | int | `900` | CompressedBlocks: Stick Block Fuel Time |
| `enableCharcoalBlock` | bool | `true` | CompressedBlocks |
| `enableSugarCaneBlock` | bool | `true` | CompressedBlocks |
| `enableCactusBlock` | bool | `true` | CompressedBlocks |
| `enableChorusFruitBlock` | bool | `true` | CompressedBlocks |
| `enableStickBlock` | bool | `true` | CompressedBlocks |
| `enableAppleCrate` | bool | `true` | CompressedBlocks |
| `enableGoldenAppleCrate` | bool | `true` | CompressedBlocks |
| `enablePotatoCrate` | bool | `true` | CompressedBlocks |
| `enableCarrotCrate` | bool | `true` | CompressedBlocks |
| `enableGoldenCarrotCrate` | bool | `true` | CompressedBlocks |
| `enableBeetrootCrate` | bool | `true` | CompressedBlocks |
| `enableCocoaBeanSack` | bool | `true` | CompressedBlocks |
| `enableNetherWartSack` | bool | `true` | CompressedBlocks |
| `enableGunpowderSack` | bool | `true` | CompressedBlocks |
| `enableBerrySack` | bool | `true` | CompressedBlocks |
| `enableGlowBerrySack` | bool | `true` | CompressedBlocks |
| `enableBlazeLantern` | bool | `true` | CompressedBlocks |
| `enableBondedLeather` | bool | `true` | CompressedBlocks |
| `enableBondedRabbitHide` | bool | `true` | CompressedBlocks |
| `enableStools` | bool | true | Stools module |
| `enableRope` | bool | true | Rope module |
| `forceEnableMoveTileEntities` | bool | `false` | Rope: Set to true to allow ropes to move Tile Entities even if Pistons Push TEs is disabled. Note that ropes will... |
| `enableDispenserBehavior` | bool | `true` | Rope |
| `enableShearVines` | bool | true | ShearVines module |
| `enableMorePottedPlants` | bool | true | MorePottedPlants module |
| `enableGlassShard` | bool | true | GlassShard module |
| `enableUtilityRecipes` | bool | true | UtilityRecipes module |
| `betterStoneToolCrafting` | bool | `true` | UtilityRecipes: Can other stone-like materials be used for crafting stone tools? |
| `enableDispenser` | bool | `true` | UtilityRecipes: Can a dispenser be crafted by adding a bow to a dropper? |
| `enableRepeater` | bool | `true` | UtilityRecipes: Can a repeater be crafted with the pattern for a redstone torch? |
| `enableMinecarts` | bool | `true` | UtilityRecipes: Can you craft a minecart around blocks which can be placed inside? |
| `enableChestBoats` | bool | `true` | UtilityRecipes: Can you craft a boat around a chest to directly make a chest boat? |
| `logsToChests` | bool | `true` | UtilityRecipes: Can you craft four chests at once using logs? |
| `coralToDye` | bool | `true` | UtilityRecipes: Can Coral be crafted into dye? |
| `bentRecipes` | bool | `true` | UtilityRecipes: Can cookies, paper, and bread be crafted in a 2x2 crafting table? |
| `compostableToxins` | bool | `true` | UtilityRecipes: Can Rotten Flesh and Poisonous Potatoes be composted? |
| `effectiveDragonBreath` | bool | `true` | UtilityRecipes: Does Dragon Breath return a bottle when used as a reagent or material? |
| `torchesBurn` | bool | `true` | UtilityRecipes: Can torches be used as fuel in furnaces? |
| `boneMealUtility` | bool | `true` | UtilityRecipes: Can bones be smelted down to bone meal? |
| `charcoalToBlackDye` | bool | `true` | UtilityRecipes: Can Charcoal be crafted into Black Dye? |
| `easyHopper` | bool | `true` | UtilityRecipes: Can two Logs be used instead of a Chest to make a Hopper? |
| `easySticks` | bool | `true` | UtilityRecipes: Can two Logs be used to craft 16 sticks? |
| `smeltRawOreBlocks` | bool | `true` | UtilityRecipes: Can raw ore blocks be smelted, taking 9x the time a normal item? |

#### U9 `config/variantcontainers.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableVariantChests` | bool | true | VariantChests module |
| `enableRevertingWoodenChests` | bool | `true` | VariantChests |
| `replaceWorldgenChests` | bool | `true` | VariantChests |
| `structureChests` | string[] | `[]` | VariantChests: Chests to put in structures. It's preferred to use worldgen tags for this. The format per entry is \"struct... |
| `enableVariantFurnaces` | bool | true | VariantFurnaces module |

#### U10 `config/redstone.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableChainsConnectBlocks` | bool | true | ChainsConnectBlocks module |
| `enableIronRod` | bool | true | IronRod module |
| `usePreEndRecipe` | bool | `false` | IronRod |
| `enablePistonsMoveTileEntities` | bool | true | PistonsMoveTileEntities module |
| `enableChestsMovingTogether` | bool | `true` | PistonsMoveTileEntities |
| `renderBlacklist` | string[] | `["psi:programmer", "botania:starfield"]` | PistonsMoveTileEntities |
| `movementBlacklist` | string[] | `["minecraft:spawner", "minecraft:trial_spawner", "minecraft:vault", "integrateddynamics:cable", "randomthings:blockbreaker", "minecraft:ender_chest", "minecraft:enchanting_table", "#minecraft:ceiling_hanging_signs", "#c:chests/trapped", "endergetic:bolloom_bud"]` | PistonsMoveTileEntities |
| `namespaceMovementBlacklist` | string[] | `[]` | PistonsMoveTileEntities |
| `delayedUpdateList` | string[] | `["minecraft:dispenser", "minecraft:dropper"]` | PistonsMoveTileEntities |
| `pistonPushLimit` | int | `12` | Zeta's general option "Changes the piston push limit"; it takes effect through the piston structure resolver of PistonsMoveTileEntities |
| `enableGravisand` | bool | true | Gravisand module |
| `enableMetalButtons` | bool | true | MetalButtons module |
| `enableIron` | bool | `true` | MetalButtons |
| `enableGold` | bool | `true` | MetalButtons |
| `enableObsidianPlate` | bool | true | ObsidianPlate module |
| `enableRedstoneRandomizer` | bool | true | RedstoneRandomizer module |
| `enableEnderWatcher` | bool | true | EnderWatcher module |
| `enableDispensersPlaceBlocks` | bool | true | DispensersPlaceBlocks module |
| `blacklist` | string[] | `["minecraft:water", "minecraft:lava", "minecraft:fire"]` | DispensersPlaceBlocks |
| `wrapExistingBehaviors` | bool | `true` | DispensersPlaceBlocks: Set to false to refrain from registering any behaviors for blocks that have optional dispense behaviors alr... |

#### U11 `config/logistics.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableChute` | bool | true | Chute module |
| `enablePipes` | bool | true | Pipes module |
| `pipeSpeed` | int | `5` | Pipes: How long it takes for an item to cross a pipe. Bigger = slower. |
| `maxPipeItems` | int | `16` | Pipes: Set to 0 if you don't want pipes to have a max amount of items |
| `doPipesWhoosh` | bool | `true` | Pipes: When items eject or are absorbed by pipes, should they make sounds? |
| `enableEncasedPipes` | bool | `true` | Pipes |
| `renderPipeItems` | bool | `true` | Pipes |
| `emitVibrations` | bool | `true` | Pipes |
| `enableFeedingTrough` | bool | true | FeedingTrough module |
| `cooldown` | int | `30` | FeedingTrough: How long, in game ticks, between animals being able to eat from the trough |
| `maxAnimals` | int | `32` | FeedingTrough: The maximum amount of animals allowed around the trough's range for an animal to enter love mode |
| `loveChance` | double | `0.333333333` | FeedingTrough: The chance (between 0 and 1) for an animal to enter love mode when eating from the trough |
| `range` | double | `10` | FeedingTrough |
| `lookChance` | double | `0.015` | FeedingTrough: Chance that an animal decides to look for a trough. Closer it is to 1 the more performance it will take. De... |
| `enableCrate` | bool | true | Crate module |
| `maxItems` | int | `640` | Crate |

#### U12 `config/crystals.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableCorundum` | bool | true | Corundum module |
| `generationSettings` | object | structured: `UndergroundStyleConfig.styleBuilder() .style(new CorundumStyle()) .biomeDeny(BiomeTags.IS_OCEAN) .rarity(400) .horizontalSize(36) .vertic...` | Corundum |
| `crystalChance` | double | `0.16` | Corundum |
| `crystalClusterChance` | double | `0.2` | Corundum |
| `crystalClusterOnSidesChance` | double | `0.6` | Corundum |
| `doubleCrystalChance` | double | `0.2` | Corundum |
| `caveCrystalGrowthChance` | int | `5` | Corundum: The chance that a crystal can grow, this is on average 1 in X world ticks, set to a higher value to make th... |
| `crystalsCraftRunes` | bool | `true` | Corundum (gates the corundum variant of the rune duplication recipe, `#S#` / `#C#` / `###` with `#` = corundum, `S` = `minecraft:stone_tool_materials`, `C` = `lepton:smithing_template_rune` -> 2 templates; U13 ships the amethyst variant. Not in 0.1.0) |
| `enableCollateralMovement` | bool | `true` | Corundum |
| `enableRainbowLamps` | bool | true | RainbowLamps module |
| `lightLevel` | int | `15` | RainbowLamps |
| `useCorundum` | bool | `true` | RainbowLamps: Whether Rainbow Lamps should be made from and themed on Corundum if that module is enabled. |
| `enableBeaconRedirection` | bool | true | BeaconRedirection module |
| `horizontalMoveLimit` | int | `64` | BeaconRedirection |
| `allowTintedGlassTransparency` | bool | `true` | BeaconRedirection |
| `enableMagnets` | bool | true | Magnets module |
| `magneticDerivationList` | string[] | `["minecraft:iron_ingot", "minecraft:copper_ingot", "minecraft:exposed_copper", "minecraft:weathered_copper", "minecraft:oxidized_copper", "minecraft:raw_iron", "minecraft:raw_copper", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "..."` (full list in Quark) | Magnets: Any items you place in this list will be derived so that any block made of it will become magnetizable |
| `magneticWhitelist` | string[] | `["minecraft:chipped_anvil", "minecraft:damaged_anvil", "minecraft:iron_horse_armor", "minecraft:chainmail_helmet", "minecraft:chainmail_boots", "minecraft:chainmail_leggings", "minecraft:chainmail_chestplate", "#minecraft:cauldrons"]` | Magnets: Block/Item IDs to force-allow magnetism on, regardless of their crafting recipe |
| `magneticBlacklist` | string[] | `["minecraft:tripwire_hook", "minecraft:map", "#minecraft:all_hanging_signs", "#minecraft:hanging_signs"]` | Magnets: Block/Item IDs to force-disable magnetism on, regardless of their crafting recipe |
| `usePreEndRecipe` | bool | `false` | Magnets |
| `usePistonLogic` | bool | `true` | Magnets: When true magnets will never push something that pistons cant push. Disable to have further control. This a... |
| `affectEntities` | bool | `true` | Magnets: Allows magnets to push and pull entities in the 'affected_by_magnets' tag (edit it with datapack). Turning ... |
| `affectsArmor` | bool | `true` | Magnets: Allows magnets to push and pull entities having magnetic armor. Requires 'magnetic_entities' config ON |
| `entitiesPullForce` | double | `0.18` | Magnets: Determines how fast entities are pulled by magnets. Still follows the inverse square law |
| `stoneCutterSilkTouch` | bool | `true` | Magnets: Stonecutters pulled by magnets will silk touch the blocks they cut. |
| `stoneCutterMaxHardness` | double | `3` | Magnets: The maximum hardness of a block that a stonecutter pushed by magnets can cut through. |

#### U13 `config/enchanting.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableMatrixEnchanting` | bool | true | MatrixEnchanting module |
| `maxBookshelves` | int | `15` | MatrixEnchanting: The maximum enchanting power the matrix enchanter can accept |
| `piecePriceScale` | int | `9` | MatrixEnchanting: Should this be X, the price of a piece increase by 1 every X pieces you generate |
| `bookEnchantability` | int | `12` | MatrixEnchanting: The higher this is, the better enchantments you'll get on books |
| `baseMaxPieceCount` | int | `3` | MatrixEnchanting: How many pieces you can generate without any bookshelves |
| `baseMaxPieceCountBook` | int | `1` | MatrixEnchanting: How many pieces you can generate without any bookshelves (for Books) |
| `minLevelCutoff` | int | `8` | MatrixEnchanting: At which piece count the calculation for the min level should default to increasing one per piece rather th... |
| `chargePerLapis` | int | `4` | MatrixEnchanting: How many pieces a single Lapis can generate |
| `minLevelScaleFactor` | double | `1.2` | MatrixEnchanting: How much the min level requirement for adding a new piece should increase for each piece added (up until th... |
| `minLevelScaleFactorBook` | double | `2.0` | MatrixEnchanting: How much the min level requirement for adding a new piece to a book should increase per each bookshelf bein... |
| `dupeMultiplier` | double | `1.4` | MatrixEnchanting: How much to multiply the frequency of pieces where at least one of the same type has been generated |
| `incompatibleMultiplier` | double | `0.0` | MatrixEnchanting: How much to multiply the frequency of pieces where incompatible pieces have been generated |
| `allowBooks` | bool | `true` | MatrixEnchanting: Set to false to disable the ability to create Enchanted Books |
| `allowTreasures` | bool | `false` | MatrixEnchanting: Set this to true to allow treasure enchantments to be rolled as pieces |
| `allowUndiscoverableEnchantments` | bool | `false` | MatrixEnchanting: Set this to true to allow undiscoverable enchantments (that is, enchantments that are not in the #minecraft... |
| `treasureWhitelist` | string[] | `[]` | MatrixEnchanting: Any treasure enchantment IDs here will be able to appear in books in matrix enchanting |
| `undiscoverableWhitelist` | string[] | `[]` | MatrixEnchanting: Any undiscoverable enchantment IDs here will be able to appear in matrix enchanting |
| `showTooltip` | bool | `true` | MatrixEnchanting: Set to false to disable the tooltip for items with pending enchantments |
| `normalizeRarity` | bool | `true` | MatrixEnchanting: By default, enchantment rarities are fuzzed a bit to feel better with the new system. Set this to false to ... |
| `allowUnderwaterEnchanting` | bool | `true` | MatrixEnchanting: Matrix Enchanting can be done with water instead of air around the enchanting table. Set this to false to d... |
| `allowShortBlockEnchanting` | bool | `true` | MatrixEnchanting: Matrix Enchanting can be done with short (<= 3px blocks) instead of air around the enchanting table. Set th... |
| `allowZeroEnchantability` | bool | `false` | MatrixEnchanting: If true, items that have 0 enchantability, but are in the #c:enchantables tag, such as Shears and Shields, ... |
| `soulCandlesInvert` | bool | `true` | MatrixEnchanting: Candles with soul sand below them or below the bookshelves dampen enchantments instead of influence them. |
| `disallowedEnchantments` | string[] | `[]` | MatrixEnchanting: A list of enchantment IDs you don't want the enchantment table to be able to create |
| `statesToInfluences` | string[] | `[]` | MatrixEnchanting: An array of influences that other blocks should apply.  |
| `allowInfluencing` | bool | `true` | MatrixEnchanting: Set to false to disable the ability to influence enchantment outcomes with candles |
| `influenceMax` | int | `4` | MatrixEnchanting: The max amount of candles that can influence a single enchantment |
| `influencePower` | double | `0.125` | MatrixEnchanting: How much each candle influences an enchantment. This works as a multiplier to its weight |
| `automaticallyConvert` | bool | `true` | MatrixEnchanting: If you set this to false, the vanilla Enchanting Table will no longer automatically convert to the Matrix E... |
| `enableAncientTomes` | bool | true | AncientTomes module |
| `lootTables` | string[] | structured: `Lists.newArrayList( loot(BuiltInLootTables.STRONGHOLD_LIBRARY.location(), 20), loot(BuiltInLootTables.SIMPLE_DUNGEON.location(), 20), loo...` | AncientTomes: Format is lootTable,weight. i.e. \"minecraft:chests/stronghold_library,30\" |
| `ancientTomesItemQuality` | int | `2` | AncientTomes |
| `normalUpgradeCost` | int | `10` | AncientTomes |
| `limitBreakUpgradeExtraCost` | int | `20` | AncientTomes |
| `maxLimitBreakLevels` | int | `1` | AncientTomes |
| `enchantNames` | string[] | structured: `generateDefaultEnchantmentList()` | AncientTomes: Valid Enchantments |
| `overleveledBooksGlowRainbow` | bool | `true` | AncientTomes |
| `deepslateTweak` | bool | `true` | AncientTomes: When enabled, Efficiency VI Diamond and Netherite pickaxes can instamine Deepslate when under Haste 2 |
| `deepslateTweakNeedsHaste2` | bool | `true` | AncientTomes |
| `librariansExchangeAncientTomes` | bool | `true` | AncientTomes: Master Librarians will offer to exchange Ancient Tomes, provided you give them a max-level Enchanted Book o... |
| `curseGear` | bool | `false` | AncientTomes: Applying a tome will also randomly curse your item |
| `combineWithBooks` | bool | `true` | AncientTomes: Allows combining tomes with normal books |
| `sanityCheck` | bool | `true` | AncientTomes: Whether a sanity check is performed on the valid enchantments. If this is turned off, enchantments such as ... |
| `enableColorRunes` | bool | true | ColorRunes module |
| `dungeonWeight` | int | `10` | ColorRunes |
| `netherFortressWeight` | int | `8` | ColorRunes |
| `jungleTempleWeight` | int | `8` | ColorRunes |
| `desertTempleWeight` | int | `8` | ColorRunes |
| `colorRunesItemQuality` | int | `0` | ColorRunes |

#### U14 `config/backpack.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableBackpack` | bool | true | Backpack module |
| `superOpMode` | bool | `false` | Backpack: Set this to true to allow the backpacks to be unequipped even with items in them |
| `enableRavagerHide` | bool | `true` | Backpack |
| `itemsInBackpackTick` | bool | `true` | Backpack |
| `baseRavagerHideDrop` | int | `1` | Backpack |
| `extraChancePerLooting` | double | `0.5` | Backpack |
| `allowArmorStandUnloading` | bool | `true` | Backpack |
| `enableTotemOfHolding` | bool | true | TotemOfHolding module |
| `darkSoulsMode` | bool | `true` | TotemOfHolding: Set this to false to remove the behaviour where totems destroy themselves if the player dies again. |
| `enableOnPK` | bool | `false` | TotemOfHolding: Spawn Totem on PVP Kill |
| `destroyLostItems` | bool | `false` | TotemOfHolding: Set this to true to make it so that if a totem is destroyed, the items it holds are destroyed alongside it ... |
| `allowAnyoneToCollect` | bool | `true` | TotemOfHolding: Set this to false to only allow the owner of a totem to collect its items rather than any player |
| `dropAllItemsOnHit` | bool | `false` | TotemOfHolding: Set this to true to make the totem drop everything when punched once |
| `enableSeedPouch` | bool | true | SeedPouch module |
| `maxItems` | int | `640` | SeedPouch |
| `showAllVariantsInCreative` | bool | `true` | SeedPouch |
| `shiftRange` | int | `3` | SeedPouch |
| `allowFertilizer` | bool | `true` | SeedPouch: Allow putting bone meal into the Seed Pouch (or anything else in the tag 'quark:seed_pouch_fertilizers') |
| `fertilizerShiftRange` | int | `3` | SeedPouch |

#### U15 `config/inventory.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableInventorySorting` | bool | true | InventorySorting module |
| `enablePlayerInventory` | bool | `true` | InventorySorting |
| `enablePlayerInventoryInChests` | bool | `true` | InventorySorting |
| `enableChests` | bool | `true` | InventorySorting |
| `satisfyingClick` | bool | `true` | InventorySorting: Play a click when sorting inventories using keybindings |
| `enableEasyTransferring` | bool | true | EasyTransferring module |
| `enableShiftLock` | bool | `true` | EasyTransferring |
| `enableChestSearching` | bool | true | ChestSearching module |
| `overlayColor` | object | structured: `RGBAColorConfig.forColor(0, 0, 0, 0.67)` | ChestSearching |
| `enableHotbarChanger` | bool | true | HotbarChanger module |
| `animationTime` | double | `7` | HotbarChanger |
| `enableAutomaticToolRestock` | bool | true | AutomaticToolRestock module |
| `enchantNames` | string[] | structured: `generateDefaultEnchantmentList()` | AutomaticToolRestock: Important Enchantments |
| `enableLooseMatching` | bool | `true` | AutomaticToolRestock: Enable replacing your tools with tools of the same type but not the same item |
| `enableEnchantMatching` | bool | `true` | AutomaticToolRestock: Enable comparing enchantments to find a replacement |
| `checkHotbar` | bool | `false` | AutomaticToolRestock: Allow pulling items from one hotbar slot to another |
| `unstackablesOnly` | bool | `false` | AutomaticToolRestock |
| `ignoredItems` | string[] | `["quark:trowel", "botania:exchange_rod", "botania:dirt_rod", "botania:skydirt_rod", "botania:cobble_rod"]` | AutomaticToolRestock: Any items you place in this list will be ignored by the restock feature |
| `enableQuickArmorSwapping` | bool | true | QuickArmorSwapping module |
| `swapOffHand` | bool | `true` | QuickArmorSwapping |
| `enableExpandedItemInteractions` | bool | true | ExpandedItemInteractions module |
| `enableArmorInteraction` | bool | `true` | ExpandedItemInteractions |
| `enableShulkerBoxInteraction` | bool | `true` | ExpandedItemInteractions |
| `enableLavaInteraction` | bool | `true` | ExpandedItemInteractions |
| `allowOpeningShulkerBoxes` | bool | `true` | ExpandedItemInteractions |
| `allowRotatingBundles` | bool | `true` | ExpandedItemInteractions |
| `invertClicks` | bool | `false` | ExpandedItemInteractions: Set this to true to use left clicks instead of right clicks for all Expanded Item Interactions. Some JEI hi... |
| `enableItemSharing` | bool | true | ItemSharing module |
| `cooldown` | int | `100` | ItemSharing: In ticks. |
| `renderItemsInChat` | bool | `true` | ItemSharing |

#### U16 `config/hud.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableUsageTicker` | bool | true | UsageTicker module |
| `invert` | bool | `false` | UsageTicker: Switch the armor display to the off hand side and the hand display to the main hand side |
| `shiftLeft` | int | `0` | UsageTicker |
| `shiftRight` | int | `0` | UsageTicker |
| `enableMainHand` | bool | `true` | UsageTicker |
| `enableOffHand` | bool | `true` | UsageTicker |
| `enableArmor` | bool | `true` | UsageTicker |
| `enableElytraIndicator` | bool | true | ElytraIndicator module |
| `enableImprovedTooltips` | bool | true | ImprovedTooltips module |
| `attributeTooltips` | bool | `true` | ImprovedTooltips |
| `foodTooltips` | bool | `true` | ImprovedTooltips |
| `shulkerTooltips` | bool | `true` | ImprovedTooltips |
| `mapTooltips` | bool | `true` | ImprovedTooltips |
| `enchantingTooltips` | bool | `true` | ImprovedTooltips |
| `fuelTimeTooltips` | bool | `true` | ImprovedTooltips |
| `shulkerBoxUseColors` | bool | `true` | ImprovedTooltips |
| `shulkerBoxRequireShift` | bool | `false` | ImprovedTooltips |
| `mapRequireShift` | bool | `false` | ImprovedTooltips |
| `foodDivisor` | int | `2` | ImprovedTooltips: The value of each shank of food.  |
| `showSaturation` | bool | `true` | ImprovedTooltips |
| `foodCompressionThreshold` | int | `4` | ImprovedTooltips |
| `fuelTimeDivisor` | int | `200` | ImprovedTooltips |
| `showUpgradeStatus` | bool | `true` | ImprovedTooltips: Should item attributes be colored relative to your current equipped item?  |
| `animateUpDownArrows` | bool | `true` | ImprovedTooltips |
| `enchantingStacks` | string[] | Quark's list (diamond tools and armour, mace, shears, bow, fishing rod, crossbow, trident, elytra, shield, `lepton:pickarang`, ...) | ImprovedTooltips: items shown under each enchantment |
| `enchantingAdditionalStacks` | string[] | `[]` | ImprovedTooltips: A list of additional stacks to display on each enchantment  |
| `enableAutoWalkKeybind` | bool | true | AutoWalkKeybind module |
| `drawHud` | bool | `true` | AutoWalkKeybind |
| `hudHeight` | int | `10` | AutoWalkKeybind |
| `enableBackButtonKeybind` | bool | true | BackButtonKeybind module |
| `backMouseButton` | int | `5` | (Lepton) BackButtonKeybind: mouse button bound to Back (4 = X1, 5 = X2, 0 = none) |

#### U17 `config/placement.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableReacharoundPlacing` | bool | true | ReacharoundPlacing module |
| `leniency` | double | `0.5` | ReacharoundPlacing |
| `whitelist` | string[] | `[]` | ReacharoundPlacing |
| `blacklist` | string[] | `[]` | ReacharoundPlacing |
| `enableLockRotation` | bool | true | LockRotation module |
| `lockRotationRenderLikeCrossHair` | bool | `true` | LockRotation: When true, lock rotation indicator in the same style as crosshair |
| `showKeybind` | bool | `true` | LockRotation: When true, the keybind for rotation lock will be displayed next to the indicator |
| `enableVariantSelector` | bool | false | VariantSelector module |
| `convertVariantItems` | bool | `false` | VariantSelector: Set this to true to automatically convert any dropped variant items into their originals. Do this ONLY if y... |
| `enableHammer` | bool | `false` | VariantSelector: Enable the hammer, allowing variants to be swapped between eachother, including the original block. Do this... |
| `showTooltip` | bool | `true` | VariantSelector |
| `alignHudToHotbar` | bool | `false` | VariantSelector |
| `showSimpleHud` | bool | `false` | VariantSelector |
| `showHud` | bool | `true` | VariantSelector |
| `enableGreenTint` | bool | `true` | VariantSelector |
| `overrideHeldItemRender` | bool | `true` | VariantSelector |
| `hudOffsetX` | int | `0` | VariantSelector |
| `hudOffsetY` | int | `0` | VariantSelector |
| `variantSelectorRenderLikeCrossHair` | bool | `true` | VariantSelector: When true, selector arrow will render in same style as crosshair |
| `smallerArrow` | bool | `false` | VariantSelector: Uses smaller arrow icon for variant selector overlay |
| `variants` | object | structured: `new VariantsConfig()` (`testedMods` `["lepton"]`, `manualVariants` with the Bedrock oak fence gate; `docs/spec/placement.md`) | VariantSelector |
| `enableTrowel` | bool | true | Trowel module |
| `maxDamage` | int | `0` | Trowel: Trowel Max Durability |
| `trowelBlacklist` | string[] | `["minecraft:air"]` | Trowel: Quark's `trowel_blacklist` item tag (Bedrock items have no data-pack tags) |
| `trowelWhitelist` | string[] | `[]` | Trowel: Quark's `trowel_whitelist` item tag |
| `enableReplaceScaffolding` | bool | true | ReplaceScaffolding module |
| `maxBounces` | int | `1` | ReplaceScaffolding: How many times the algorithm for finding out where a block would be placed is allowed to turn. If you set t... |
| `enableLongRangePickBlock` | bool | true | LongRangePickBlock module |
| `enableEnhancedLadders` | bool | true | EnhancedLadders module |
| `fallSpeed` | double | `-0.2` | EnhancedLadders |
| `allowFreestanding` | bool | `true` | EnhancedLadders |
| `allowDroppingDown` | bool | `true` | EnhancedLadders |
| `allowSliding` | bool | `true` | EnhancedLadders |
| `allowInventorySneak` | bool | `true` | EnhancedLadders |

#### U18 `config/cosmetics.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableGreenerGrass` | bool | true | GreenerGrass module |
| `affectLeaves` | bool | `true` | GreenerGrass |
| `affectWater` | bool | `false` | GreenerGrass |
| `blockList` | string[] | `["minecraft:large_fern", "minecraft:tall_grass", "minecraft:grass_block", "minecraft:fern", "minecraft:short_grass", "minecraft:potted_fern", "minecraft:sugar_cane", "environmental:giant_tall_grass", "grassslabs:grass_carpet", "grassslabs:grass_slab", "grassslabs:grass_stairs"]` | GreenerGrass |
| `leavesList` | string[] | `["minecraft:spruce_leaves", "minecraft:birch_leaves", "minecraft:oak_leaves", "minecraft:jungle_leaves", "minecraft:acacia_leaves", "minecraft:dark_oak_leaves", "atmospheric:rosewood_leaves", "atmospheric:morado_leaves", "atmospheric:yucca_leaves", "autumnity:maple_leaves", "environmental:willow_..."` (full list in Quark) | GreenerGrass |
| `colorMatrix` | object | structured: `new ConvulsionMatrixConfig(GRASS_PARAMS)` | GreenerGrass |
| `waterMatrix` | object | structured: `new ConvulsionMatrixConfig(WATER_PARAMS)` | GreenerGrass |
| `enableVariantAnimalTextures` | bool | true | VariantAnimalTextures module |
| `enableCow` | bool | `true` | VariantAnimalTextures |
| `enablePig` | bool | `true` | VariantAnimalTextures |
| `enableChicken` | bool | `true` | VariantAnimalTextures |
| `enableAllay` | bool | `true` | VariantAnimalTextures |
| `enableShinyRabbit` | bool | `true` | VariantAnimalTextures |
| `enableShinyLlama` | bool | `true` | VariantAnimalTextures |
| `enableShinyDolphin` | bool | `true` | VariantAnimalTextures |
| `enableShinySlime` | bool | `true` | VariantAnimalTextures |
| `enableShinyArmadillo` | bool | `true` | VariantAnimalTextures |
| `enableShinyFrog` | bool | `true` | VariantAnimalTextures |
| `enableLGBTBees` | bool | `true` | VariantAnimalTextures |
| `everyBeeIsLGBT` | bool | `false` | VariantAnimalTextures |
| `shinyAnimalChance` | int | `2048` | VariantAnimalTextures: The chance for an animal to have a special \"Shiny\" skin, like a shiny pokemon. This is 1 in X. Set to 0 t... |
| `shinySparkles` | bool | `true` | VariantAnimalTextures: If a shiny animal should emit occasional sparkles. |
| `enableBucketsShowInhabitants` | bool | true | BucketsShowInhabitants module |
| `showAxolotls` | bool | `true` | BucketsShowInhabitants |
| `showCrabs` | bool | `true` | BucketsShowInhabitants |
| `showTropicalFish` | bool | `true` | BucketsShowInhabitants |
| `showShinySlime` | bool | `true` | BucketsShowInhabitants |
| `enableSoulCandles` | bool | true | SoulCandles module |
| `enableWoolShutsUpMinecarts` | bool | true | WoolShutsUpMinecarts module |
| `enableUsesForCurses` | bool | true | UsesForCurses module |
| `vanishPumpkinOverlay` | bool | `true` | UsesForCurses |
| `bindArmorStandsWithPlayerHeads` | bool | `true` | UsesForCurses |
| `enableMoreVillagers` | bool | true | MoreVillagers module |
| `oceanVillager` | bool | `true` | MoreVillagers |
| `beachVillager` | bool | `true` | MoreVillagers |
| `enableCamera` | bool | true | Camera module |
| `dateFormat` | string | `"MM/dd/yyyy"` | Camera: Date format that will be displayed in screenshots. Must be a valid one (i.e. MM/dd/yyyy) |

#### U19 `config/emotes.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableEmotes` | bool | true | Emotes module |
| `enabledEmotes` | string[] | structured: `Lists.newArrayList(DEFAULT_EMOTE_NAMES)` | Emotes: The enabled default emotes. Remove from this list to disable them. You can also re-order them, if you feel ... |
| `customEmotes` | string[] | `[]` | Emotes: The list of Custom Emotes to be loaded. Watch the tutorial on Custom Emotes to learn how to make your own: ... |
| `customEmoteDebug` | bool | `false` | Emotes: Enable this to make custom emotes read the file every time they're triggered so you can edit on the fly. DO... |
| `buttonShiftX` | int | `0` | Emotes |
| `buttonShiftY` | int | `0` | Emotes |
| `enablePatTheDogs` | bool | true | PatTheDogs module |
| `dogsWantLove` | int | `-1` | PatTheDogs: How many ticks it takes for a dog to want affection after being pet/tamed; leave -1 to disable |
| `petAllMobs` | bool | `false` | PatTheDogs: Whether you can pet all mobs |
| `pettableDenylist` | string[] | `["minecraft:ender_dragon", "minecraft:wither", "minecraft:armor_stand"]` | PatTheDogs: If `petAllMobs` is set, these mobs still can't be pet |
| `pettableAllowlist` | string[] | `[]` | PatTheDogs: Even if `petAllMobs` is not set, these mobs can be pet |

#### U20 `config/mobsoverworld.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableCrabs` | bool | true | Crabs module |
| `crabsSpawnConfig` | object | structured: `new EntitySpawnConfig(5, 1, 3, CompoundBiomeConfig.fromBiomeTags(false, BiomeTags.IS_BEACH))` | Crabs |
| `enableBrewing` | bool | `true` | Crabs |
| `resilienceRequiredForAllEffects` | bool | `true` | Crabs: Whether Resilience should be required for 'How Did We Get Here?' and (if brewing is enabled) 'A Furious Coc... |
| `addCrabLegToFishermanTrades` | bool | `true` | Crabs |
| `enableShiba` | bool | true | Shiba module |
| `shibaSpawnConfig` | object | structured: `new EntitySpawnConfig(40, 1, 3, CompoundBiomeConfig.fromBiomeTags(false, BiomeTags.IS_MOUNTAIN))` | Shiba |
| `ignoreAreasWithSkylight` | bool | `false` | Shiba |
| `enableToretoise` | bool | true | Toretoise module |
| `maxYLevel` | int | `0` | Toretoise |
| `cooldownTicks` | int | `20 * 60` | Toretoise: The number of ticks from mining a tortoise until feeding it could cause it to regrow. |
| `foods` | string[] | `["minecraft:glow_berries"]` | Toretoise: The items that can be fed to toretoises to make them regrow ores. |
| `allowToretoiseToRegrow` | bool | `true` | Toretoise |
| `regrowChance` | int | `3` | Toretoise: Feeding a toretoise after cooldown will regrow them with a one-in-this-number chance.  |
| `dimensions` | object | structured: `DimensionConfig.overworld(false)` | Toretoise |
| `toretoiseSpawnConfig` | object | structured: `new EntitySpawnConfig(120, 2, 4, CompoundBiomeConfig.fromBiomeTags(true, Tags.Biomes.IS_VOID, BiomeTags.IS_NETHER, BiomeTags.IS_END))` | Toretoise |

#### U21 `config/mobsdeep.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableStonelings` | bool | true | Stonelings module |
| `maxYLevel` | int | `0` | Stonelings |
| `dimensions` | object | structured: `DimensionConfig.overworld(false)` | Stonelings |
| `stonelingsSpawnConfig` | object | structured: `new EntitySpawnConfig(80, 1, 1, CompoundBiomeConfig.fromBiomeTags(true, Tags.Biomes.IS_VOID, BiomeTags.IS_NETHER, BiomeTags.IS_END))` | Stonelings |
| `enableDiamondHeart` | bool | `true` | Stonelings |
| `cautiousStonelings` | bool | `false` | Stonelings: When enabled, stonelings are much more aggressive in checking for players |
| `tamableStonelings` | bool | `true` | Stonelings |
| `wealdPathfinderMaps` | bool | `true` | Stonelings: Disabled if if Pathfinder Maps are disabled. |
| `enableForgotten` | bool | true | Forgotten module |
| `forgottenSpawnRate` | double | `0.05` | Forgotten: This is the probability of a Skeleton that spawns under the height threshold being replaced with a Forgotten. |
| `maxHeightForSpawn` | int | `0` | Forgotten |
| `enableFoxhound` | bool | true | Foxhound module |
| `tameChance` | double | `0.05` | Foxhound: The chance coal will tame a foxhound |
| `foxhoundsSpeedUpFurnaces` | bool | `true` | Foxhound |
| `foxhoundSpawnConfig` | object | structured: `new EntitySpawnConfig(30, 1, 2, CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:nether_wastes", "minecraft:basalt_deltas"))` | Foxhound |
| `lesserSpawnConfig` | object | structured: `new CostSensitiveEntitySpawnConfig(2, 1, 1, 0.7, 0.15, CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:soul_sand_valley"))` | Foxhound |
| `enableWraith` | bool | true | Wraith module |
| `wraithSpawnConfig` | object | structured: `new CostSensitiveEntitySpawnConfig(5, 1, 3, 0.7, 0.15, CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:soul_sand_valley"))` | Wraith |
| `wraithSounds` | string[] | Quark's 20 entries (`docs/spec/mobsdeep.md`) | Wraith (a private `@Config` field in Quark) |

#### U22 `config/throwables.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enablePickarang` | bool | true | Pickarang module |
| `pickarangType` | object | structured: `new PickarangType<>(Items.DIAMOND, Items.DIAMOND_PICKAXE, 20, 3, 800, 20.0, 2, 10)` | Pickarang: pickarang |
| `flamerangType` | object | structured: `new PickarangType<>(Items.NETHERITE_INGOT, Items.NETHERITE_PICKAXE, 20, 4, 1040, 20.0, 3, 10)` | Pickarang: flamerang |
| `enableFlamerang` | bool | `true` | Pickarang |
| `neverUseHeartOfDiamond` | bool | `false` | Pickarang: Set this to true to use the recipe without the Heart of Diamond, even if the Heart of Diamond is enabled. |
| `enableTorchArrow` | bool | true | TorchArrow module |
| `extinguishOnMiss` | bool | `false` | TorchArrow |
| `enableParrotEggs` | bool | true | ParrotEggs module |
| `chance` | double | `0.05` | ParrotEggs: The chance feeding a parrot will produce an egg |
| `eggTime` | int | `12000` | ParrotEggs: How long it takes to create an egg |
| `enableKotobirb` | bool | `true` | ParrotEggs: Enable Special Awesome Parrot |
| `enableSkullPikes` | bool | true | SkullPikes module |
| `pikeRange` | double | `5` | SkullPikes |
| `enableBottledCloud` | bool | true | BottledCloud module |
| `cloudLevelBottom` | int | `191` | BottledCloud |
| `cloudLevelTop` | int | `196` | BottledCloud |
| `enableSlimeInABucket` | bool | true | SlimeInABucket module |

#### U23 `config/gadgets.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableAbacus` | bool | true | Abacus module |
| `highlightColor` | object | structured: `RGBAColorConfig.forColor(0, 0, 0, 0.4)` | Abacus |
| `enablePathfinderMaps` | bool | true | PathfinderMaps module |
| `customs` | string[] | `[]` | PathfinderMaps |
| `applyDefaultTrades` | bool | `true` | PathfinderMaps: Set to false to make it so the default quark Pathfinder Map Built-In don't get added, and only the custom o... |
| `pathfindersQuillSpeed` | int | `32` | PathfinderMaps: How many steps in the search should the Pathfinder's Quill do per tick? The higher this value, the faster i... |
| `multiThreaded` | bool | `true` | PathfinderMaps: Experimental. Determines if quills should be multithreaded instead. Will ignore quill speed. This could dra... |
| `allowRetrying` | bool | `true` | PathfinderMaps: Allows retrying after a pathfinder quill fails to find a biome nearby. Turn off if you think its op |
| `searchRadius` | int | `6400` | PathfinderMaps |
| `xpFromTrade` | int | `5` | PathfinderMaps |
| `addToCartographer` | bool | `true` | PathfinderMaps |
| `addToWanderingTraderForced` | bool | `true` | PathfinderMaps |
| `addToWanderingTraderGeneric` | bool | `false` | PathfinderMaps |
| `addToWanderingTraderRare` | bool | `false` | PathfinderMaps |
| `drawHud` | bool | `true` | PathfinderMaps |
| `hudOnTop` | bool | `false` | PathfinderMaps |
| `enabled` | bool | `` | PathfinderMaps |
| `level` | int | `` | PathfinderMaps |
| `minPrice` | int | `` | PathfinderMaps |
| `maxPrice` | int | `` | PathfinderMaps |
| `enableAmbientDiscs` | bool | true | AmbientDiscs module |
| `dropOnSpiderKill` | bool | `true` | AmbientDiscs |
| `volume` | double | `3` | AmbientDiscs |
| `enableEndermoshMusicDisc` | bool | true | EndermoshMusicDisc module |
| `playEndermoshDuringEnderdragonFight` | bool | `false` | EndermoshMusicDisc |
| `addToEndCityLoot` | bool | `true` | EndermoshMusicDisc |
| `lootWeight` | int | `5` | EndermoshMusicDisc |
| `lootQuality` | int | `1` | EndermoshMusicDisc |
| `enableCompassesWorkEverywhere` | bool | true | CompassesWorkEverywhere module |
| `enableCompassNerf` | bool | `true` | CompassesWorkEverywhere |
| `enableClockNerf` | bool | `true` | CompassesWorkEverywhere |
| `enableNether` | bool | `true` | CompassesWorkEverywhere |
| `enableEnd` | bool | `true` | CompassesWorkEverywhere |
| `enableTinyPotato` | bool | true | TinyPotato module |
| `neverUseHeartOfDiamond` | bool | `false` | TinyPotato: Set this to true to use the recipe without the Heart of Diamond, even if the Heart of Diamond is enabled. |

#### U24 `config/frames.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableGlassItemFrame` | bool | true | GlassItemFrame module |
| `glassItemFramesUpdateMaps` | bool | `true` | GlassItemFrame |
| `glassItemFramesUpdateMapsEveryTick` | bool | `false` | GlassItemFrame: Set to true for faster map updates. Default is every 3s |
| `itemRenderScale` | double | `1.5` | GlassItemFrame: The scale at which items render in the Glass Item Frame. To match the vanilla Item Frame size, set to 1.0 |
| `enableDyeableItemFrames` | bool | true | DyeableItemFrames module |
| `enableMoreBannerLayers` | bool | true | MoreBannerLayers module |
| `layerLimit` | int | `16` | MoreBannerLayers (effective limit `min(layerLimit, 6)` on Bedrock: the banner renderer draws 6 layers) |
| `enableMapWashing` | bool | true | MapWashing module |

#### U25 `config/farming.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableSimpleHarvest` | bool | true | SimpleHarvest module |
| `emptyHandHarvest` | bool | `true` | SimpleHarvest: Can players harvest crops with empty hand clicks? |
| `harvestingCostsDurability` | bool | `false` | SimpleHarvest: Does harvesting crops with a hoe cost durability? |
| `doHarvestingSearch` | bool | `true` | SimpleHarvest: Should Quark look for(nonvanilla) crops, and handle them? |
| `villagersUseSimpleHarvest` | bool | `true` | SimpleHarvest: Should villagers use simple harvest instead of breaking crops? |
| `harvestableBlocks` | string[] | `["minecraft:wheat[age=7]", "minecraft:carrots[age=7]", "minecraft:potatoes[age=7]", "minecraft:beetroots[age=3]", "minecraft:nether_wart[age=3]", "minecraft:cocoa[age=2,facing=north],minecraft:cocoa[age=0,facing=north]", "minecraft:cocoa[age=2,facing=south],minecraft:cocoa[age=0,facing=south]", "..."` (full list in Quark) | SimpleHarvest: Which crops can be harvested?  |
| `rightClickableBlocks` | string[] | `["minecraft:sweet_berry_bush", "minecraft:cave_vines"]` | SimpleHarvest: Which blocks should right click harvesting simulate a click on instead of breaking?  |
| `enableHoeHarvesting` | bool | true | HoeHarvesting module |
| `regularHoeRadius` | int | `2` | HoeHarvesting |
| `highTierHoeRadius` | int | `3` | HoeHarvesting |
| `enablePigLitters` | bool | true | PigLitters module |
| `minPigLitterSize` | int | `2` | PigLitters |
| `maxPigLitterSize` | int | `3` | PigLitters |
| `pigsEatGoldenCarrots` | bool | `true` | PigLitters |
| `minGoldenCarrotBoost` | int | `0` | PigLitters |
| `maxGoldenCarrotBoost` | int | `2` | PigLitters |
| `enablePoisonPotatoUsage` | bool | true | PoisonPotatoUsage module |
| `chance` | double | `0.1` | PoisonPotatoUsage |
| `poisonEffect` | bool | `true` | PoisonPotatoUsage |
| `enableCoralOnCactus` | bool | true | CoralOnCactus module |
| `enableRenewableSporeBlossoms` | bool | true | RenewableSporeBlossoms module |
| `boneMealChance` | double | `0.2` | RenewableSporeBlossoms |
| `enablePetalsOnWater` | bool | true | PetalsOnWater module |
| `enableGrabChickens` | bool | true | GrabChickens module |
| `needsNoHelmet` | bool | `true` | GrabChickens |
| `slownessLevel` | int | `1` | GrabChickens: Set to 0 to disable |
| `chickenOffset` | double | `0.0` | GrabChickens: Negative numbers moves the chicken down, positive up (the carried-chicken render is U18's resource pack, driven by the cosmetics server pass) |
| `enableHorsesSwim` | bool | true | HorsesSwim module |
| `enableVillagersFollowEmeralds` | bool | true | VillagersFollowEmeralds module |

#### U26 `config/mobtweaks.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableSaferCreatures` | bool | true | SaferCreatures module (Quark 4.1-488: rabbit fall height and slime fall damage; no creeper or knockback changes) |
| `heightReduction` | double | `5.0` | SaferCreatures: How many blocks should be subtracted from the rabbit fall height when calculating fall damage. 5 is the sam... |
| `enableSlimeFallDamageRemoval` | bool | `true` | SaferCreatures |
| `enableSlimesToMagmaCubes` | bool | true | SlimesToMagmaCubes module |
| `enableSnowGolemPlayerHeads` | bool | true | SnowGolemPlayerHeads module |
| `enableVexesDieWithTheirMasters` | bool | true | VexesDieWithTheirMasters module |
| `enableZombieVillagersOnNormal` | bool | true | ZombieVillagersOnNormal module |
| `enableDragonScales` | bool | true | DragonScales module |
| `enableGoldToolsHaveFortune` | bool | true | GoldToolsHaveFortune module |
| `fortuneLevel` | int | `2` | GoldToolsHaveFortune |
| `harvestLevel` | int | `2` | GoldToolsHaveFortune |
| `displayBakedEnchantmentsInTooltip` | bool | `true` | GoldToolsHaveFortune |
| `italicTooltip` | bool | `true` | GoldToolsHaveFortune |
| `itemEnchantments` | string[] | `[]` | GoldToolsHaveFortune: Enchantments other than Gold's Fortune/Looting to bake into items. Format is \"item+enchant@level\", such a... |
| `enableDiamondRepair` | bool | true | DiamondRepair module |
| `repairChangesList` | string[] | `["minecraft:netherite_sword=minecraft:diamond", "minecraft:netherite_pickaxe=minecraft:diamond", "minecraft:netherite_axe=minecraft:diamond", "minecraft:netherite_shovel=minecraft:diamond", "minecraft:netherite_hoe=minecraft:diamond", "minecraft:netherite_helmet=minecraft:diamond", "minecraft:net..."` (full list in Quark) | DiamondRepair: Repair Item Changes |
| `unrepairableItemsList` | string[] | `[]` | DiamondRepair: Unrepairable Items |
| `enableJeiHints` | bool | `true` | DiamondRepair |
| `enableNoDurabilityOnCosmetics` | bool | true | NoDurabilityOnCosmetics module |
| `allowCosmeticItems` | bool | `true` | NoDurabilityOnCosmetics: Allow applying cosmetic items such as color runes with no anvil durability usage? Cosmetic items are define... |
| `enableShulkerPacking` | bool | true | ShulkerPacking module |
| `enableMagmaKeepsConcretePowder` | bool | true | MagmaKeepsConcretePowder module |
| `enableImprovedSponges` | bool | true | ImprovedSponges module |
| `maximumWaterDrain` | int | `256` | ImprovedSponges: The maximum number of water tiles that a sponge can soak up. Vanilla default is 64. |
| `maximumCrawlDistance` | int | `10` | ImprovedSponges: The maximum number of water tiles that a sponge can 'crawl along' for draining. Vanilla default is 6. |
| `enablePlacingOnWater` | bool | `true` | ImprovedSponges |
| `enableDoubleDoorOpening` | bool | true | DoubleDoorOpening module |
| `enableDoors` | bool | `true` | DoubleDoorOpening |
| `enableFenceGates` | bool | `true` | DoubleDoorOpening |

#### U27 `config/misctweaks.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableBetterElytraRocket` | bool | true | BetterElytraRocket module |
| `enableCampfiresBoostElytra` | bool | true | CampfiresBoostElytra module |
| `boostStrength` | double | `0.5` | CampfiresBoostElytra |
| `maxSpeed` | double | `1` | CampfiresBoostElytra |
| `enableMoreNoteBlockSounds` | bool | true | MoreNoteBlockSounds module |
| `enableAmethystSound` | bool | `true` | MoreNoteBlockSounds |
| `enableAutomaticRecipeUnlock` | bool | true | AutomaticRecipeUnlock module |
| `ignoredRecipes` | string[] | `[]` | AutomaticRecipeUnlock: A list of recipe names that should NOT be added in by default |
| `forceLimitedCrafting` | bool | `false` | AutomaticRecipeUnlock |
| `disableRecipeBook` | bool | `false` | AutomaticRecipeUnlock |
| `skipEmptyRecipes` | bool | `true` | AutomaticRecipeUnlock: If true, recipes that have no output will be skipped from unlock |
| `filterRecipeAdvancements` | bool | `true` | AutomaticRecipeUnlock: If enabled, advancements granting recipes will be stopped from loading, potentially reducing the lagspike o... |
| `enableArmedArmorStands` | bool | true | ArmedArmorStands module |
| `enableGameNerfs` | bool | false | GameNerfs module |
| `nerfMending` | bool | `true` | GameNerfs: Makes Mending act like the Unmending mod  |
| `noNerfForMendingTwo` | bool | `false` | GameNerfs: No Nerf for Mending II |
| `disableIronFarms` | bool | `true` | GameNerfs: Makes Iron Golems not drop Iron Ingots |
| `disableIceRoads` | bool | `true` | GameNerfs: Makes Boats not glide on ice |
| `disableWoolDrops` | bool | `true` | GameNerfs: Makes Sheep not drop Wool when killed |
| `enableSelectiveMobGriefing` | bool | `true` | GameNerfs: Disables mob griefing for only specific entities |
| `enableDimensionLockedElytra` | bool | `true` | GameNerfs: Force Elytra to only work in specific dimensions |
| `disableFallingBlockDupe` | bool | `true` | GameNerfs: Makes falling blocks not able to be duped via dimension crossing |
| `disablePistonPhysicsExploits` | bool | `true` | GameNerfs: Fixes several piston physics exploits, most notably including TNT duping (no effect on Bedrock: its pistons have none of the Java update-order paths Quark patches) |
| `disableMushroomBlockRemoval` | bool | `true` | GameNerfs: Fixes mushroom growth being able to replace blocks |
| `disableTripwireHookDupe` | bool | `true` | GameNerfs: Makes tripwire hooks unable to be duplicated |
| `villageSpawnNerf` | bool | `false` | GameNerfs: Makes villages spawn less often when close to spawn |
| `villageSpawnNerfDistance` | int | `7000` | GameNerfs: Distance at which villages will spawn as normal. Effect scales linearly from world spawn |
| `nonGriefingEntities` | string[] | `["minecraft:creeper", "minecraft:enderman"]` | GameNerfs |
| `elytraAllowedDimensions` | string[] | `["minecraft:the_end"]` | GameNerfs |
| `enableSpawnerReplacer` | bool | false | SpawnerReplacer module |
| `replaceMobs` | string[] | `[]` | SpawnerReplacer: Mobs to be replaced with other mobs.  |
| `enableVillagerRerollingRework` | bool | false | VillagerRerollingRework module |
| `seedInitialVillagerTrades` | bool | `true` | VillagerRerollingRework: If enabled, the first two trades a villager generates for a profession will always be the same for a given ... |
| `chanceToRerollWhenRestocking` | double | `0.25` | VillagerRerollingRework |
| `maximumRestocksPerDay` | int | `3` | VillagerRerollingRework: Set to 0 to disable the chance to reroll trades when restocking. Set to -1 to allow unlimited rerolling.  |
| `rerollOnAnyRestock` | bool | `false` | VillagerRerollingRework: If enabled, villagers will reroll when they restock, rather than when they begin work for the day.  |
| `rerollEvenIfNotOutOfStock` | bool | `false` | VillagerRerollingRework: If enabled, villagers will be able to reroll any trade that has been used AT ALL since the last restock. |
| `enableMicrocraftingHelper` | bool | true | MicrocraftingHelper module |
| `enableAdjustableChat` | bool | false | AdjustableChat module |
| `horizontalShift` | int | `0` | AdjustableChat |
| `verticalShift` | int | `0` | AdjustableChat |
| `enableClimateControlRemover` | bool | false | ClimateControlRemover module |
| `disableTemperature` | bool | `false` | ClimateControlRemover: Disables the temperature comparison when choosing biomes to generate. |
| `disableHumidity` | bool | `false` | ClimateControlRemover: Disables the humidity comparison when choosing biomes to generate. |
| `disableContinentalness` | bool | `false` | ClimateControlRemover: Disables the 'continentalness' comparison when choosing biomes to generate.  |
| `disableErosion` | bool | `false` | ClimateControlRemover: Disables the 'erosion' comparison when choosing biomes to generate.  |
| `disableDepth` | bool | `false` | ClimateControlRemover: Disables the 'depth' comparison when choosing biomes to generate.  |
| `disableWeirdness` | bool | `false` | ClimateControlRemover: Disables the 'weirdness' comparison when choosing biomes to generate.  |
| `disableOffset` | bool | `false` | ClimateControlRemover: Disables the 'offset' parameter when choosing biomes to generate.  |
| `enableNarratorReadout` | bool | false | NarratorReadout module |
| `enableOverlayShader` | bool | false | OverlayShader module |
| `shader` | string | `"none"` | OverlayShader: Sets the name of the shader to load on a regular basis. This can load any shader the Camera module can (and... |

#### U28 `config/worldsurface.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableFairyRings` | bool | true | FairyRings module |
| `forestChance` | double | `0.00625` | FairyRings |
| `plainsChance` | double | `0.0025` | FairyRings |
| `fairyRingsDimensions` | object | structured: `new DimensionConfig(false, "minecraft:overworld")` | FairyRings |
| `oresRaw` | string[] | `["minecraft:emerald_ore", "minecraft:diamond_ore"]` | FairyRings: Ores |
| `enableFallenLogs` | bool | true | FallenLogs module |
| `fallenLogsDimensions` | object | structured: `new DimensionConfig(false, "minecraft:overworld")` | FallenLogs |
| `hollowChance` | double | `0.7` | FallenLogs: Percentage of fallen logs spawning as hollow. Requires Hollow Logs Module to be enabled |
| `rarity` | int | `5` | FallenLogs |
| `onWaterChance` | double | `0.1` | FallenLogs: Chance for logs to spawn on water |
| `sparseBiomeRarity` | int | `12` | FallenLogs |
| `biomeTags` | string[] | `["has_fallen_acacia=minecraft:acacia_log", "has_fallen_birch=minecraft:birch_log", "has_fallen_cherry=minecraft:cherry_log", "has_fallen_dark_oak=minecraft:dark_oak_log", "has_fallen_jungle=minecraft:jungle_log", "has_fallen_mangrove=minecraft:mangrove_log", "has_fallen_oak=minecraft:oak_log", "h..."` (full list in Quark) | FallenLogs: Tags that define which biomes can have which wood types. These are all biome tags under the Quark namespace |
| `enableMonsterBox` | bool | true | MonsterBox module |
| `monsterBoxChancePerChunk` | double | `0.2` | MonsterBox: The chance for the monster box generator to try and place one in a chunk. 0 is 0%, 1 is 100% This can be hi... |
| `minY` | int | `-50` | MonsterBox |
| `maxY` | int | `0` | MonsterBox |
| `minMobCount` | int | `5` | MonsterBox |
| `maxMobCount` | int | `8` | MonsterBox |
| `monsterBoxDimensions` | object | structured: `DimensionConfig.overworld(false)` | MonsterBox |
| `enableExtraLootTable` | bool | `true` | MonsterBox |
| `activationRange` | double | `2.5` | MonsterBox |
| `searchRange` | int | `15` | MonsterBox: How many blocks to search vertically from a position before trying to place a block. Higher means you'll ge... |
| `enableGlimmeringWeald` | bool | true | GlimmeringWeald module |
| `minDepthRange` | double | `1.55` | GlimmeringWeald: Min Depth Range |
| `maxWeirdnessRange` | double | `2` | GlimmeringWeald: Max Weirdness Range |
| `enableCherryGroveWaterPetals` | bool | true | CherryGroveWaterPetals module |
| `cherryGroveWaterPetalsChancePerChunk` | double | `0.5` | CherryGroveWaterPetals: The chance for a Cherry Grove chunk to attempt to spawn petals on top of water (1 is 100%, 0 is 0%) |
| `triesPerChunk` | int | `1` | CherryGroveWaterPetals: Should a chunk have water petals, how many would the generator try to place |
| `size` | int | `3` | CherryGroveWaterPetals: The 'size' of groups of petals |
| `sizeVariation` | int | `1` | CherryGroveWaterPetals: Groups of petals may be +/- this size on each axis, set to 0 to disable variation |
| `edgeVariation` | double | `0.25` | CherryGroveWaterPetals: Edges of groups will have this chance to be 'jagged', that is, gain or lose a petal. Only applies when useC... |
| `useCarpet` | bool | `false` | CherryGroveWaterPetals: If true, Cherry Leaf Carpet from Leaf Carpet Module (from Building category) will be used instead of Pink P... |
| `biomes` | object | structured: `CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:cherry_grove")` | CherryGroveWaterPetals |
| `cherryGroveWaterPetalsDimensions` | object | structured: `DimensionConfig.overworld(false)` | CherryGroveWaterPetals |

#### U29 `config/worldbeyond.json`

| Key | Type | Default | Quark option |
|---|---|---|---|
| `version` | int | 1 | (Lepton) |
| `enableNetherObsidianSpikes` | bool | true | NetherObsidianSpikes module |
| `chancePerChunk` | double | `0.1` | NetherObsidianSpikes: The chance for a chunk to contain spikes (1 is 100%, 0 is 0%) |
| `bigSpikeChance` | double | `0.03` | NetherObsidianSpikes: The chance for a spike to be big (1 is 100%, 0 is 0%) |
| `triesPerChunk` | int | `4` | NetherObsidianSpikes: Should a chunk have spikes, how many would the generator try to place |
| `bigSpikeSpawners` | bool | `true` | NetherObsidianSpikes |
| `netherObsidianSpikesDimensions` | object | structured: `DimensionConfig.nether(false)` | NetherObsidianSpikes |
| `enableNoMoreLavaPockets` | bool | true | NoMoreLavaPockets module |
| `enableChorusVegetation` | bool | true | ChorusVegetation module |
| `chorusVegetationRarity` | int | `150` | ChorusVegetation |
| `chorusVegetationRadius` | int | `7` | ChorusVegetation |
| `chunkAttempts` | int | `120` | ChorusVegetation |
| `highlandsChance` | double | `1.0` | ChorusVegetation |
| `midlandsChance` | double | `0.2` | ChorusVegetation |
| `otherEndBiomesChance` | double | `0` | ChorusVegetation |
| `passiveTeleportChance` | double | `0.2` | ChorusVegetation |
| `endermiteSpawnChance` | double | `0.01` | ChorusVegetation |
| `teleportDuplicationChance` | double | `0.01` | ChorusVegetation |
| `enableSpiralSpires` | bool | true | SpiralSpires module |
| `spiralSpiresDimensions` | object | structured: `DimensionConfig.end(false)` | SpiralSpires |
| `biomes` | object | structured: `CompoundBiomeConfig.fromBiomeReslocs(false, "minecraft:end_highlands")` | SpiralSpires |
| `spiralSpiresRarity` | int | `200` | SpiralSpires |
| `spiralSpiresRadius` | int | `15` | SpiralSpires |
| `enableMyaliteViaducts` | bool | `true` | SpiralSpires |
| `myaliteConduitDistance` | int | `24` | SpiralSpires |
| `renewableMyalite` | bool | `true` | SpiralSpires |

## 6. Commands

`/quark` (alias `/lepton`):

| Command | Permission | What it does |
|---|---|---|
| `/quark selftest ["glob"]` | operator / console | Runs the in-game selftests (`SELFTEST PASS/FAIL/DONE` lines). Quote globs. |
| `/quark reload` | operator / console | Reloads every `config/*.json` and the translations. |
| `/quark net` | operator / console | Lists the online players and whether they run the client mod (`Lepton net: ...`). |
| `/quark net ping` | operator / console | Pings every client mod (each answers; logged as `net: pong <n> from <player>`). |
| `/quark debug form` | operator (player) | Sends a test form (`form test: ...` log lines). |
| `/quark debug container` | operator (player) | Opens a 27-slot virtual container at the player's position. |

Units add sub-commands with `LP_REGISTER_SUBCOMMAND` (in `src/mod/Commands.h`); each is listed in
the unit spec. Quark 4.1-488 itself registers **no commands** (its configuration is a GUI and a
TOML file), so no Quark command has to be reproduced. Debug or admin sub-commands that a unit adds
for testing are operator-only.

## 7. Strings

**See `docs/spec/<unit>.md`** for every string of a unit. Shared rules:

- Mod text (chat messages, tooltips, screen text, form text) goes through `lp::tr`, `lp::trFor`
  (per player locale) or `lp::client::ctr` (client), with entries in
  `assets/lang/en_US/<unit>.json` and `assets/lang/zh_CN/<unit>.json`.
- Keys: Quark's own key names where the string exists in Quark (`quark.gui.button.sort`,
  `quark.keybind.change_hotbar`, `quark.emote.wave`, ...); `lp.<unit>.*` for Lepton-only text
  (fallback messages for vanilla clients, form titles, ...).
- The English text is Quark's wording (`en_us.json` of Quark 4.1-488); zh_CN follows Quark's
  `zh_cn.json` where it exists, otherwise a translation written for Lepton.
- Block, item and entity names live in the resource pack `texts.d/<locale>/<unit>.lang`
  (`tile.lepton:<id>.name`, `item.lepton:<id>`, `entity.lepton:<id>.name`), not in the mod lang files.
- A key belongs to the unit that owns the feature; other units use it through `tr(key, "English fallback")`
  without defining it (duplicate keys fail the build).

## 8. Not ported

Only things that have no meaning on Bedrock are left out. Everything else is ported, possibly with a
documented Bedrock difference in the unit spec.

| What | Why |
|---|---|
| Quark's config GUI (the "q" button, Zeta config screens, TOML files) | Replaced by `config/<unit>.json` and `/quark reload` (§5). |
| Zeta's module system, `antiOverlap` with other Java mods, Forge/NeoForge tags and capabilities, JEI/EMI/Curios/other mod integrations | Java mod-loader concepts. Lepton has its own module registry. |
| Recipe/loot condition flags (`flag = ...` in `@Config`) as data-pack conditions | Bedrock packs have no load conditions; the unit disables content natively from config instead. |
| Advancements (`data/quark/advancement/`) | Bedrock has no custom advancements. |
| Hints (`@Hint`, shown in JEI) | JEI is a Java mod; the information is in the unit specs instead. |
| OverlayShader | Java loads a post-processing shader chain (`PostChain`) at runtime. Bedrock's RenderDragon only uses precompiled materials shipped in packs, and the client exposes no full-screen post-effect hook. The config keys are kept. (AdjustableChat, NarratorReadout and ClimateControlRemover are ported; `docs/spec/misctweaks.md`.) |
| MicrocraftingHelper | It extends Java's recipe-book ghost recipe. Bedrock's recipe book has no ghost recipe (missing ingredients are only shown in red), so there is no ingredient to right-click; it already crafts from the book. The key is kept. |
| Java mod APIs (`SimpleHarvestEvent`, `IUsageTickerOverride`, `UsageTickerEvent`, `IPistonCallback`, `ITransferManager`, `ICustomSorting`, Flan, Curios, Lootr, Cyclic recipes) | No other Java mods on Bedrock. |
| Quark's own sounds, music and voice clips (stoneling, wraith, monster box, Glimmering Weald music, tiny potato voices, PatTheDogs contributor sounds) | Quark assets may not be copied: the same sound events play vanilla Bedrock sounds or original jingles. Bedrock sound definitions have no subtitles. |

Bedrock limitations. Each unit spec's §7 has the full list; these are the ones a player notices:

| What | Unit | Why |
|---|---|---|
| Signs, hanging signs, boats and chest boats of the azalea, trumpet and ashen woods (and their chest-boat recipes) | U7, U8 | Signs need a block entity, which custom blocks can't have; boats are a vanilla entity with a fixed set of woods. |
| Furnace minecart recipes | U8, U9 | Bedrock has no furnace minecart. |
| Coloured and rainbow enchantment glints (ColorRunes, overlevelled books) | U13 | The glint is drawn with a fixed texture and LeviLamina 26.51.7 exposes no glint colour; runes are applied, stored and listed in the tooltip, the glint stays purple. |
| A banner or a map picture in a glass item frame | U24 | Bedrock draws banner patterns and map pictures only in vanilla frames, banner blocks and the hand; a glass frame shows the item model. |
| More than 6 banner layers | U24 | The banner renderer draws 6 layers (`layerLimit` is capped at 6). |
| Lava-logged grates | U5 | Custom blocks can only hold water. |
| Comparator output of pipes, crates and feeding troughs | U11 | Not hooked (gravisand, variant chests, furnaces and glass frames do have one, through a per-block vtable copy). |
| Animal-only collision of the feeding trough | U11 | Block collision can't depend on the entity. |
| The Glimmering Weald as a real biome (`/locate biome`, its music) | U28 | Bedrock can't add a biome to the Overworld climate list; the area is computed from Quark's climate point (`/quark locate`), the chunk keeps its vanilla biome. No per-player music API. |
| Smooth tints (myalite and myalite crystals) and per-stack item tints (pathfinder quills and maps) | U3, U23, U29 | Custom blocks have no per-position tint: 16 hue steps. Item icons have no per-stack tint. |
| Item icons that depend on item data (abacus count, seed pouch / backpack fill, excited slime bucket animation, shiny slime buckets) | U14, U22, U23 | One icon per item: lore lines, tooltips or a separate item instead. |
| Custom mob effects in the effect list (Resilience), tipped arrows of Stability, custom potion ids | U20 | The effect and potion lists are fixed enums shared with every client: HUD / action-bar text and custom potion items instead. |
| Reach bonus of the Forgotten Hat | U21 | Bedrock has no reach attribute (the luck bonus is applied). |
| Player heads with skins (SnowGolemPlayerHeads, UsesForCurses) | U18, U26 | Bedrock skulls have no player profile. |
| Advancements, statistics and their triggers; JEI hints | all | Bedrock has neither; hints are tooltip lines or `/quark <unit> info`. |
| Creative tab positions next to specific vanilla blocks | all | Custom blocks only pick a category and group. |
| Disabled content in the creative inventory, the recipe book and the network recipe index | all | Bedrock sends them once per world; the server refuses crafting and placing disabled content. |
| Client-predicted movement for vanilla clients (climbing variant ladders, rope, hollow logs, crawling, stool bounces, ladder sliding) | U5, U6, U8, U17 | Movement is client-authoritative on Bedrock: without the client mod the server corrects the player, so it stutters. |
| HUDs, tooltips, keybinds, GreenerGrass, SoulCandles, BucketsShowInhabitants, Camera for vanilla clients; bucket icons in the hand | U16, U18 | Client features need the client mod (they are client modules in Quark too). Bedrock caches in-hand item meshes per item id. |
| Anvil previews of DiamondRepair, ancient tomes and runes for vanilla clients on a server | U13, U26 | The vanilla client computes anvil and smithing previews itself; Lepton offers those results through a helper. |
| GameNerfs `disablePistonPhysicsExploits` | U27 | No effect: Bedrock pistons have none of the Java update-order paths Quark patches. |
| Server-built chat texts in the player's own language | U0 | On a dedicated server the server builds some texts itself (e.g. the `/give` feedback) with Lepton's names in `config/mod.json` `language`, like vanilla names in the server's language. |

## 9. Work plan

**Status: complete.** Every unit below is merged and released in 0.1.0. The table stays as the map of
which unit owns which modules, files and selftest range.

U0 (this foundation: spec, net, forms, client UI, game helpers, pack and texture generators) comes
first. The units below then run in parallel, each in its own branch, owning its files only (see
AGENTS.md "Work as a feature unit"). U<n> owns selftest dx `100n..100n+99`; U0 uses `0..99`.
Every unit writes `docs/spec/<unit>.md` before its first feature.

| Unit | Name | Quark modules | Selftest dx | Spec | Depends on (beyond the foundation) |
|---|---|---|---|---|---|
| U0 | foundation | (spec, net, forms, client UI, game helpers, generators) | 0..99 | this file | |
| U1 | vslabs | VerticalSlabs, SlabsToBlocks (every vanilla slab, including the 1.26 slabs Quark 4.1-488 predates: pale oak, resin brick, poplar, cinnabar, sulfur, concrete, wool) | 100..199 | `docs/spec/vslabs.md` | New-ShapeSet; vertical slab placement in `game/Shapes` |
| U2 | stones | MoreStoneVariants, RawMetalBricks, SturdyStone, CelebratoryLamps | 200..299 | `docs/spec/stones.md` | New-ShapeSet |
| U3 | newstones | NewStoneTypes, Permafrost, BigStoneClusters, VanillaStoneClusters | 300..399 | `docs/spec/newstones.md` | New-ShapeSet; U2 variant set for the new stones |
| U4 | bricks | MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori, DuskboundBlocks | 400..499 | `docs/spec/bricks.md` | New-ShapeSet |
| U5 | palette | Shingles, FramedGlass, GoldBars, IndustrialPalette, JapanesePalette, NetherBrickFenceGate, Thatch, Grate | 500..599 | `docs/spec/palette.md` | New-ShapeSet (panes, gates) |
| U6 | woodvariants | VerticalPlanks, VariantBookshelves, VariantLadders, WoodenPosts, Hedges, LeafCarpet, HollowLogs | 600..699 | `docs/spec/woodvariants.md` | New-WoodSet hooks (variants of vanilla woods; U7 adds its woods' variants) |
| U7 | newwoods | AzaleaWood, AncientWood, BlossomTrees | 700..799 | `docs/spec/newwoods.md` | New-WoodSet; tags defined by U6/U9 for its woods' variants |
| U8 | storage | CompressedBlocks, Stools, Rope, ShearVines, MorePottedPlants, GlassShard, UtilityRecipes | 800..899 | `docs/spec/storage.md` | `game/Entities` (stool seat) |
| U9 | variantcontainers | VariantChests, VariantFurnaces | 900..999 | `docs/spec/variantcontainers.md` | `game/VirtualContainer` |
| U10 | redstone | ChainsConnectBlocks, IronRod, PistonsMoveTileEntities, Gravisand, MetalButtons, ObsidianPlate, RedstoneRandomizer, EnderWatcher, DispensersPlaceBlocks | 1000..1099 | `docs/spec/redstone.md` | `game/Shapes` |
| U11 | logistics | Chute, Pipes, FeedingTrough, Crate | 1100..1199 | `docs/spec/logistics.md` | `game/VirtualContainer`, `game/Inventory` |
| U12 | crystals | Corundum, RainbowLamps, BeaconRedirection, Magnets (+ the dirty glass beam tint for U8) | 1200..1299 | `docs/spec/crystals.md` | |
| U13 | enchanting | MatrixEnchanting, AncientTomes, ColorRunes | 1300..1399 | `docs/spec/enchanting.md` | `client/ui` custom screen, `src/net`, `game/Forms` fallback |
| U14 | backpack | Backpack, TotemOfHolding, SeedPouch | 1400..1499 | `docs/spec/backpack.md` | `game/VirtualContainer`, `game/Inventory`, `game/Entities` |
| U15 | inventory | InventorySorting, EasyTransferring, ChestSearching, HotbarChanger, AutomaticToolRestock, QuickArmorSwapping, ExpandedItemInteractions, ItemSharing | 1500..1599 | `docs/spec/inventory.md` | `client/ui` overlays + keybinds, `src/net`, `game/Inventory` |
| U16 | hud | UsageTicker, ElytraIndicator, ImprovedTooltips, AutoWalkKeybind, BackButtonKeybind | 1600..1699 | `docs/spec/hud.md` | `client/ui` HUD + tooltip hooks + keybinds |
| U17 | placement | ReacharoundPlacing, LockRotation, VariantSelector, Trowel, ReplaceScaffolding, LongRangePickBlock, EnhancedLadders | 1700..1799 | `docs/spec/placement.md` | `game/Shapes`, `client/ui`, `src/net` |
| U18 | cosmetics | GreenerGrass, VariantAnimalTextures, BucketsShowInhabitants, SoulCandles, WoolShutsUpMinecarts, UsesForCurses, MoreVillagers, Camera | 1800..1899 | `docs/spec/cosmetics.md` | resource pack; `client/ui` (Camera) |
| U19 | emotes | Emotes, PatTheDogs | 1900..1999 | `docs/spec/emotes.md` | `client/ui` custom screen + keybinds, `src/net` |
| U20 | mobsoverworld | Crabs, Shiba, Toretoise | 2000..2099 | `docs/spec/mobsoverworld.md` | `game/Entities` |
| U21 | mobsdeep | Stonelings, Forgotten, Foxhound, Wraith | 2100..2199 | `docs/spec/mobsdeep.md` | `game/Entities` |
| U22 | throwables | Pickarang, TorchArrow, ParrotEggs, SkullPikes, BottledCloud, SlimeInABucket | 2200..2299 | `docs/spec/throwables.md` | `game/Entities` |
| U23 | gadgets | Abacus, PathfinderMaps, AmbientDiscs, EndermoshMusicDisc, CompassesWorkEverywhere, TinyPotato | 2300..2399 | `docs/spec/gadgets.md` | `client/ui` HUD (Abacus) |
| U24 | frames | GlassItemFrame, DyeableItemFrames, MoreBannerLayers, MapWashing, sign editing | 2400..2499 | `docs/spec/frames.md` | `game/Entities`; "sign editing" is a check only (§2.2) |
| U25 | farming | SimpleHarvest, HoeHarvesting, PigLitters, PoisonPotatoUsage, CoralOnCactus, RenewableSporeBlossoms, PetalsOnWater, GrabChickens, HorsesSwim, VillagersFollowEmeralds | 2500..2599 | `docs/spec/farming.md` | |
| U26 | mobtweaks | SaferCreatures, SlimesToMagmaCubes, SnowGolemPlayerHeads, VexesDieWithTheirMasters, ZombieVillagersOnNormal, DragonScales, GoldToolsHaveFortune, DiamondRepair, NoDurabilityOnCosmetics, ShulkerPacking, MagmaKeepsConcretePowder, ImprovedSponges, DoubleDoorOpening | 2600..2699 | `docs/spec/mobtweaks.md` | |
| U27 | misctweaks | BetterElytraRocket, CampfiresBoostElytra, MoreNoteBlockSounds, AutomaticRecipeUnlock, ArmedArmorStands, GameNerfs, SpawnerReplacer, VillagerRerollingRework + Java-only review (AdjustableChat, NarratorReadout, OverlayShader, ClimateControlRemover, MicrocraftingHelper) | 2700..2799 | `docs/spec/misctweaks.md` | |
| U28 | worldsurface | FairyRings, FallenLogs, MonsterBox, GlimmeringWeald, CherryGroveWaterPetals + remaining Overworld world modules | 2800..2899 | `docs/spec/worldsurface.md` | U3/U7/U12 blocks used by world gen (by id; generation skips missing blocks) |
| U29 | worldbeyond | NetherObsidianSpikes, NoMoreLavaPockets, ChorusVegetation, SpiralSpires + remaining Nether/End world modules | 2900..2999 | `docs/spec/worldbeyond.md` | U3 (myalite) blocks by id |

Notes:

- World modules that the table names under another unit stay there (AzaleaWood, AncientWood,
  BlossomTrees: U7; NewStoneTypes, Permafrost, BigStoneClusters: U3; Corundum: U12). With those,
  every world module of Quark 4.1-488 is named in a row, so the "remaining" world modules of U28/U29
  are currently none; a unit that finds world generation inside another module coordinates through
  its PR.
- Units never edit `docs/SPEC.md`, `CHANGELOG.md` or `README.md`; they list the changelog / README
  lines and any change to the shared sections of this spec in their PR body.
- Cross-unit references (a block of another unit, a translation key of another unit) are by id or
  key only, and must not break when the other unit is not merged yet.
