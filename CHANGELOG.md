# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-10-30

First release: the modules of Quark 4.1-488 on Minecraft Bedrock 1.26.51 (LeviLamina 26.51.x), for
dedicated servers and local worlds. Every module can be turned off in `config/<unit>.json`. Players
without the client mod get the blocks, items and mobs through the resource pack, and forms or vanilla
container screens in place of the client mod's screens. Bedrock limits are listed in
[docs/SPEC.md §8](docs/SPEC.md#8-not-ported).

### Added

#### Automation

- Chains connect the blocks they link, so pistons move them together (ChainsConnectBlocks).
- Iron rods break the block in front of them when a piston pushes them (IronRod).
- Pistons follow Quark's block entity rules and push limit (PistonsMoveTileEntities, `pistonPushLimit`).
- Gravisand, iron and gold buttons, the obsidian pressure plate (players only), the redstone randomizer
  and the ender watcher (Gravisand, MetalButtons, ObsidianPlate, RedstoneRandomizer, EnderWatcher).
- Dispensers place blocks and plant seeds (DispensersPlaceBlocks).
- The chute and the feeding trough, which tempts and breeds animals nearby (Chute, FeedingTrough).

#### Building

- Vertical slabs for every vanilla slab, including the 1.26 slabs Quark predates (VerticalSlabs).
- Stone variants: polished calcite and dripstone, and bricks, chiseled bricks and pillars of granite,
  diorite, andesite, calcite and dripstone, with stairs, slabs, walls and vertical slabs
  (MoreStoneVariants); raw iron, gold and copper bricks (RawMetalBricks); sturdy stone that pistons
  can't move (SturdyStone); the Stone Lamp and Stone Brick Lamp (CelebratoryLamps).
- More bricks (blue nether, sandstone, red sandstone, cobblestone, mossy cobblestone, blackstone, dirt and
  netherrack bricks), soul sandstone, carved mud bricks, mud pillars and lattice, midori and duskbound
  blocks (MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori, DuskboundBlocks).
- The palette blocks: terracotta shingles, framed glass and panes, gold bars (they replace the Nether
  Brick Fences of Nether Fortresses), iron plates, the iron pillar and iron ladder, paper lanterns and
  walls, bamboo mats, the nether brick fence gate, thatch and the iron grate (Shingles, FramedGlass,
  GoldBars, IndustrialPalette, JapanesePalette, NetherBrickFenceGate, Thatch, Grate).
- Vertical planks, variant bookshelves and ladders, wooden posts, hedges, leaf carpets and hollow logs
  you can crawl and climb into, for every wood (VerticalPlanks, VariantBookshelves, VariantLadders,
  WoodenPosts, Hedges, LeafCarpet, HollowLogs).
- Compressed storage blocks (crates, sacks, bundles, the Block of Charcoal, the Blaze Lantern and more),
  stools you can sit on, rope, 25 more plants in flower pots (CompressedBlocks, Stools, Rope,
  MorePottedPlants).
- Shears cut vines so they stop growing (ShearVines).
- Variant chests and trapped chests for every wood, nether brick, purpur and prismarine, also on
  donkeys and in structures; deepslate and blackstone furnaces (VariantChests, VariantFurnaces).
- Crystal lamps, one per corundum colour (RainbowLamps).
- The glass item frame and glow glass item frame (GlassItemFrame).

#### Management

- Inventory sorting, easy transferring (insert, extract, shift lock), chest searching, the hotbar
  changer, automatic tool restock, quick armour swapping, expanded item interactions (equip, shulker
  box insert / dump / open, lava trash, bundle scrolling) and item sharing (InventorySorting,
  EasyTransferring, ChestSearching, HotbarChanger, AutomaticToolRestock, QuickArmorSwapping,
  ExpandedItemInteractions, ItemSharing). Players without the client mod use `/quark sort`,
  `/quark transfer`, `/quark hotbar`, `/quark shulker` and `/quark share`.
- The hotbar changer's key defaults to H (Quark uses Z, which is Bedrock's Mob Effects key).

#### Tools

- Pickarang and Flamerang, torch arrows, parrot eggs, skull pikes, bottled clouds and slime in a
  bucket (Pickarang, TorchArrow, ParrotEggs, SkullPikes, BottledCloud, SlimeInABucket).
- Ancient tomes push enchantments past their maximum level in the anvil; rune smithing templates
  colour enchanted items and are duplicated with corundum (or amethyst without the Corundum
  module) and cobblestone (AncientTomes, ColorRunes).
- Beacon beams bend through corundum clusters (BeaconRedirection).
- The abacus, the Pathfinder's Quill and pathfinder maps, eight ambience discs, the Endermosh music
  disc (Abacus, PathfinderMaps, AmbientDiscs, EndermoshMusicDisc).
- The trowel, which places a random block from your hotbar (Trowel).
- The seed pouch carries up to 640 seeds and plants them 3x3 (SeedPouch).

#### Oddities

- Matrix enchanting: the enchanting table becomes Quark's matrix enchanter, with bookshelf and candle
  influences (a native screen with the client mod, forms without) (MatrixEnchanting).
- The backpack (27 more slots while worn, dyeable), ravager hide and the Totem of Holding, which keeps
  your drops where you died (Backpack, TotemOfHolding).
- Copper pipes and encased pipes, the storage crate and magnets (Pipes, Crate, Magnets).
- The tiny potato (TinyPotato).

#### Mobs

- Crabs (with the Potion of Stability), shibas and oretoises (Crabs, Shiba, Toretoise).
- Stonelings and the Heart of Diamond, the Forgotten and its hat, foxhounds, wraiths and soul beads
  (Stonelings, Forgotten, Foxhound, Wraith).

#### Tweaks

- Right-click to harvest and replant crops; hoes harvest an area (SimpleHarvest, HoeHarvesting).
- Pig litters, poison potatoes stop babies from growing, coral on cactus, renewable spore blossoms,
  pink petals on water, carrying chickens on your head, horses that swim, villagers that follow
  emerald blocks (PigLitters, PoisonPotatoUsage, CoralOnCactus, RenewableSporeBlossoms, PetalsOnWater,
  GrabChickens, HorsesSwim, VillagersFollowEmeralds).
- Safer creatures, slimes into magma cubes, snow golem player heads, vexes die with their masters,
  zombie villagers on Normal, dragon scales and elytra duplication (SaferCreatures,
  SlimesToMagmaCubes, SnowGolemPlayerHeads, VexesDieWithTheirMasters, ZombieVillagersOnNormal,
  DragonScales).
- Gold tools have Fortune, netherite repairs with diamonds, no durability loss on cosmetic anvil
  uses, shulker packing, magma keeps concrete powder, better sponges, double doors open together
  (GoldToolsHaveFortune, DiamondRepair, NoDurabilityOnCosmetics, ShulkerPacking,
  MagmaKeepsConcretePowder, ImprovedSponges, DoubleDoorOpening).
- Better elytra rockets, campfire elytra boosts, amethyst note blocks, automatic recipe unlock,
  shields on armor stands (BetterElytraRocket, CampfiresBoostElytra, MoreNoteBlockSounds,
  AutomaticRecipeUnlock, ArmedArmorStands).
- Reacharound placing, rotation lock (K), scaffolding replacing, enhanced ladders (ReacharoundPlacing,
  LockRotation, ReplaceScaffolding, EnhancedLadders).
- Two slabs craft back into their block; glass drops shards, plus dirty glass; Quark's utility
  recipes (SlabsToBlocks, GlassShard, UtilityRecipes).
- Dyeable item frames, map washing and the banner layer limit (up to Bedrock's 6 drawn layers)
  (DyeableItemFrames, MapWashing, MoreBannerLayers).
- Compasses and clocks work everywhere (CompassesWorkEverywhere).
- Beach and ocean villagers (MoreVillagers).
- Emotes (18, with a chat-screen grid, keybinds and custom emotes; `/quark emote` without the client
  mod) and patting the dogs (Emotes, PatTheDogs).

#### World

- Azalea, trumpet (blossom) and ashen (ancient) woods: full wood sets with vertical planks,
  bookshelves, ladders, posts, hollow logs, hedges, leaf carpets and chests; trumpet trees in five
  colours (snowy, swamp, savanna, plains and badlands biomes) and the ashen tree, with leaf decay,
  saplings, potted saplings and falling leaves; azalea trees grow azalea logs; the Enchanted Fruit;
  sniffers dig up ashen saplings (AzaleaWood, BlossomTrees, AncientWood).
- Limestone, jasper, shale and myalite, permafrost, big stone clusters (NewStoneTypes, Permafrost,
  BigStoneClusters).
- Corundum in nine colours, growing in caves (Corundum).
- Fairy rings, fallen logs, monster boxes, cherry grove water petals and the Glimmering Weald with glow
  shrooms (FairyRings, FallenLogs, MonsterBox, CherryGroveWaterPetals, GlimmeringWeald).
- Nether obsidian spikes, no more hidden lava pockets, chorus weeds and twist, spiral spires of myalite
  with teleporting viaducts (NetherObsidianSpikes, NoMoreLavaPockets, ChorusVegetation,
  SpiralSpires).

#### Client (needs the client mod)

- Usage ticker, elytra indicator, improved tooltips (attributes, food, shulker box and map previews,
  enchanted books, fuel), auto-walk and back-button keybinds (UsageTicker, ElytraIndicator,
  ImprovedTooltips, AutoWalkKeybind, BackButtonKeybind).
- Long-range pick block (LongRangePickBlock).
- Greener grass, variant and shiny animal skins, bucket icons that show their creature, soul candles,
  wool silences minecarts, uses for curses, camera mode with filters (F12) (GreenerGrass,
  VariantAnimalTextures, BucketsShowInhabitants, SoulCandles, WoolShutsUpMinecarts, UsesForCurses,
  Camera).

#### Experimental (off by default)

- Variant selector (R) and the hammer, game nerfs, spawner replacer, villager trade rerolling,
  vanilla stone clusters, adjustable chat, narrator readout, climate control remover
  (VariantSelector, GameNerfs, SpawnerReplacer, VillagerRerollingRework, VanillaStoneClusters,
  AdjustableChat, NarratorReadout, ClimateControlRemover).

#### Lepton

- A configuration file per unit (`config/<unit>.json`), reloaded with `/quark reload`; `/lepton` is an
  alias of `/quark`.
- English and Simplified Chinese translations.
- The client mod and the server talk over a companion protocol; vanilla clients get forms and vanilla
  container screens instead of native screens.
- On a dedicated server, chat messages the server builds (such as the `/give` feedback) show Lepton's
  block and item names instead of their translation keys.

[Unreleased]: https://github.com/LiteLDev/Lepton/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/LiteLDev/Lepton/releases/tag/v0.1.0
