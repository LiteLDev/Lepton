# Lepton

[![Build](https://github.com/LiteLDev/Lepton/actions/workflows/build.yml/badge.svg)](https://github.com/LiteLDev/Lepton/actions/workflows/build.yml)
[![License](https://img.shields.io/github/license/LiteLDev/Lepton)](COPYING.LESSER)

Lots of small things that make vanilla better, each one a module you can turn off. Lepton brings Vazkii's Java mod [Quark](https://modrinth.com/mod/quark) to Bedrock, for dedicated servers and single-player worlds.

## Features

Lepton ports the modules of Quark 4.1-488. [docs/SPEC.md](docs/SPEC.md) has the details and the [Bedrock differences](docs/SPEC.md#8-not-ported).

- **Automation**: chains that pull blocks along, iron rods, piston rules, gravisand, metal buttons, the obsidian pressure plate, the redstone randomizer, the ender watcher, block-placing dispensers, chutes and feeding troughs.
- **Building**: vertical slabs, stone and brick variants, palette blocks (shingles, framed glass, gold bars, industrial and Japanese palettes, thatch, grates), wood variants (vertical planks, bookshelves, ladders, posts, hedges, leaf carpets, hollow logs), compressed blocks, stools, rope, variant chests and furnaces, crystal lamps and glass item frames.
- **Management**: inventory sorting, easy transferring, chest searching, the hotbar changer, tool restock, quick armour swapping, expanded item interactions and item sharing.
- **Tools**: pickarangs, torch arrows, parrot eggs, skull pikes, bottled clouds, slime in a bucket, ancient tomes, colour runes, beacon redirection, the abacus, pathfinder maps, ambience discs, the trowel and the seed pouch.
- **Oddities**: matrix enchanting, the backpack, the Totem of Holding, copper pipes, crates, magnets and the tiny potato.
- **Mobs**: crabs, shibas, oretoises, stonelings, the Forgotten, foxhounds and wraiths.
- **Tweaks**: simple harvesting, hoe harvesting, reacharound placing, rotation lock, enhanced ladders, emotes, patting the dogs, double doors, better sponges, utility recipes, map washing, dyeable item frames and many smaller ones.
- **World**: the azalea, trumpet and ashen woods, new stones and permafrost, big stone clusters, corundum, fairy rings, fallen logs, monster boxes, the Glimmering Weald, Nether obsidian spikes, chorus vegetation and spiral spires.
- **Client** (needs the client mod): usage ticker, elytra indicator, improved tooltips, auto-walk, back button, long-range pick block, greener grass, variant animal skins, bucket icons and camera mode.

Players on a Lepton server don't need the client mod: the server sends its resource pack, and features with a screen fall back to forms, vanilla container screens or the `/quark` commands below. HUD, tooltip and keybind features need the client mod.

## Install

You need Minecraft Bedrock Edition **1.26.51** for Windows (x64) with LeviLamina **26.51.x** (on a client, installed through [LeviLauncher](https://github.com/LiteLDev/LeviLauncher)).

With [lip](https://github.com/LiteLDev/lip):

```sh
lip install github.com/LiteLDev/Lepton           # dedicated server
lip install github.com/LiteLDev/Lepton#client    # client
```

Or download the zip from the [latest release](https://github.com/LiteLDev/Lepton/releases/latest) and extract it next to `LeviLamina/`:

| Where | Zip | Folder |
| --- | --- | --- |
| Dedicated server | `lepton-server-windows-x64.zip` | `plugins/Lepton/` |
| Client (LeviLauncher) | `lepton-client-windows-x64.zip` | `mods/Lepton/` |

## Commands

| Command | Who | What it does |
| --- | --- | --- |
| `/quark reload` | Operators, console | Reloads the configuration and translations. |
| `/quark sort [container]` | Everyone | Sorts your inventory, or the container you look at. |
| `/quark transfer insert\|extract [filtered]` | Everyone | Moves items into or out of the container you look at. |
| `/quark hotbar [1-3]` | Everyone | Swaps the hotbar with an inventory row (a form without a row). |
| `/quark shulker` | Everyone | Opens the shulker box in your main hand. |
| `/quark share` | Everyone | Shares the item in your main hand in chat. |
| `/quark emote [name]` | Everyone | Plays an emote (`wave`, `custom:<file>`, ...); a form without a name. |
| `/quark lockrotation [clear]` | Everyone | Rotation lock for players without the client mod. |
| `/quark variant [name\|original]` | Everyone | Variant selector form, or picks a variant directly (VariantSelector enabled). |
| `/quark crystals info` | Everyone | Hints about corundum, crystal lamps, beacon redirection and magnets. |
| `/quark crystals magnetic` | Everyone | Tells whether the item in your hand is magnetic. |
| `/quark newstones info` | Everyone | Hints about the new stones and permafrost. |
| `/quark newstones sources [radius]` | Operators | Lists the stone cluster sources near you. |
| `/quark locate glimmering_weald\|monster_box` | Operators | Finds the nearest Glimmering Weald or known monster box. |
| `/quark worldgen stats` | Operators | Counts the features generated since the start. |
| `/quark enchanting tome <enchantment>` | Operators | Gives an ancient tome. |
| `/quark enchanting rune <colour>` | Operators | Applies a rune colour to the enchanted item in your hand. |
| `/quark enchanting demo [anvil\|smithing\|fill]` | Operators | Test helpers for matrix enchanting, tomes and runes. |
| `/quark gadgets quill <biome>` | Operators | Gives a Pathfinder's Quill for a biome. |
| `/quark gadgets potato angry\|name <name>` | Operators | Tiny potato test helpers. |
| `/quark seedpouch <item>` | Operators | Gives a full seed pouch. |
| `/quark cosmetics gallery\|view <row>\|buckets\|pumpkin <cursed>\|carry` | Operators | Cosmetics previews. |
| `/quark debug backpack\|totem\|hud\|form\|container` | Operators | Test helpers (fill the backpack, spawn a totem, tooltip test kit, test form, test container). |
| `/quark net [ping]` | Operators, console | Lists who runs the client mod; pings the client mods. |
| `/quark selftest ["glob"]` | Operators, console | Runs the in-game selftests. |

`/lepton` is an alias of `/quark`.

## Configuration

Settings live in `config/*.json` inside the mod folder (`plugins/Lepton/` or `mods/Lepton/`). Each file is created with defaults on first start, and `/quark reload` applies changes (content and world generation changes apply when a world loads). A unit's file has one `enable<Module>` switch per Quark module plus that module's Quark options, with Quark's names and defaults: [docs/SPEC.md §5](docs/SPEC.md#5-configuration) lists every key.

| File | Contents |
| --- | --- |
| `mod.json` | Lepton itself: `language` (`en_US`; server messages when a player's own language has no translation), `usePlayerLocale` (`true`), `requireResourcePack` (`true`; dedicated server: players must accept the resource pack). |
| `keybinds.json` | Client: the keys of Lepton's keybinds (also in the game's Controls settings). |
| `vslabs.json` | VerticalSlabs, SlabsToBlocks |
| `stones.json` | MoreStoneVariants, RawMetalBricks, SturdyStone, CelebratoryLamps |
| `newstones.json` | NewStoneTypes, Permafrost, BigStoneClusters, VanillaStoneClusters |
| `bricks.json` | MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori, DuskboundBlocks |
| `palette.json` | Shingles, FramedGlass, GoldBars, IndustrialPalette, JapanesePalette, NetherBrickFenceGate, Thatch, Grate |
| `woodvariants.json` | VerticalPlanks, VariantBookshelves, VariantLadders, WoodenPosts, Hedges, LeafCarpet, HollowLogs |
| `newwoods.json` | AzaleaWood, AncientWood, BlossomTrees |
| `storage.json` | CompressedBlocks, Stools, Rope, ShearVines, MorePottedPlants, GlassShard, UtilityRecipes |
| `variantcontainers.json` | VariantChests, VariantFurnaces |
| `redstone.json` | ChainsConnectBlocks, IronRod, PistonsMoveTileEntities (with `pistonPushLimit`), Gravisand, MetalButtons, ObsidianPlate, RedstoneRandomizer, EnderWatcher, DispensersPlaceBlocks |
| `logistics.json` | Chute, Pipes, FeedingTrough, Crate |
| `crystals.json` | Corundum, RainbowLamps, BeaconRedirection, Magnets |
| `enchanting.json` | MatrixEnchanting, AncientTomes, ColorRunes |
| `backpack.json` | Backpack, TotemOfHolding, SeedPouch |
| `inventory.json` | InventorySorting, EasyTransferring, ChestSearching, HotbarChanger, AutomaticToolRestock, QuickArmorSwapping, ExpandedItemInteractions, ItemSharing |
| `hud.json` | UsageTicker, ElytraIndicator, ImprovedTooltips, AutoWalkKeybind, BackButtonKeybind |
| `placement.json` | ReacharoundPlacing, LockRotation, VariantSelector, Trowel, ReplaceScaffolding, LongRangePickBlock, EnhancedLadders |
| `cosmetics.json` | GreenerGrass, VariantAnimalTextures, BucketsShowInhabitants, SoulCandles, WoolShutsUpMinecarts, UsesForCurses, MoreVillagers, Camera |
| `emotes.json` | Emotes, PatTheDogs (custom emotes go in `config/quark_emotes/`) |
| `mobsoverworld.json` | Crabs, Shiba, Toretoise |
| `mobsdeep.json` | Stonelings, Forgotten, Foxhound, Wraith |
| `throwables.json` | Pickarang, TorchArrow, ParrotEggs, SkullPikes, BottledCloud, SlimeInABucket |
| `gadgets.json` | Abacus, PathfinderMaps, AmbientDiscs, EndermoshMusicDisc, CompassesWorkEverywhere, TinyPotato |
| `frames.json` | GlassItemFrame, DyeableItemFrames, MoreBannerLayers, MapWashing |
| `farming.json` | SimpleHarvest, HoeHarvesting, PigLitters, PoisonPotatoUsage, CoralOnCactus, RenewableSporeBlossoms, PetalsOnWater, GrabChickens, HorsesSwim, VillagersFollowEmeralds |
| `mobtweaks.json` | SaferCreatures, SlimesToMagmaCubes, SnowGolemPlayerHeads, VexesDieWithTheirMasters, ZombieVillagersOnNormal, DragonScales, GoldToolsHaveFortune, DiamondRepair, NoDurabilityOnCosmetics, ShulkerPacking, MagmaKeepsConcretePowder, ImprovedSponges, DoubleDoorOpening |
| `misctweaks.json` | BetterElytraRocket, CampfiresBoostElytra, MoreNoteBlockSounds, AutomaticRecipeUnlock, ArmedArmorStands, GameNerfs, SpawnerReplacer, VillagerRerollingRework, AdjustableChat, NarratorReadout, ClimateControlRemover |
| `worldsurface.json` | FairyRings, FallenLogs, MonsterBox, GlimmeringWeald, CherryGroveWaterPetals |
| `worldbeyond.json` | NetherObsidianSpikes, NoMoreLavaPockets, ChorusVegetation, SpiralSpires |

### Keybinds (client mod)

| Default key | Action |
| --- | --- |
| H | Hotbar changer (Quark uses Z, which is Bedrock's Mob Effects key) |
| K | Rotation lock |
| R (hold) | Variant selector (experimental, VariantSelector enabled) |
| F12 | Camera mode |
| Shift + T over a slot | Share the item (the chat key, in inventory screens) |
| Mouse button 5 | Back (`backMouseButton` in `hud.json`) |
| Not bound | Sorting, insert, extract, shift lock, auto-walk, narrator readout, each emote |

Change them in the game's Controls settings or in `config/keybinds.json`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [AGENTS.md](AGENTS.md) is the developer guide.

## Credits

Lepton reproduces the behaviour of [Quark](https://github.com/VazkiiMods/Quark) by Vazkii. It copies no code or assets from it.

## License

[LGPL-3.0-or-later](COPYING.LESSER)
