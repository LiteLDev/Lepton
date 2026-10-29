# U18 cosmetics: greener grass, animal variants, bucket icons, soul candles, quiet minecarts, uses for curses, more villagers, camera mode

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`): `content/client/module/{GreenerGrass,
VariantAnimalTextures, BucketsShowInhabitants, SoulCandles, WoolShutsUpMinecarts, UsesForCurses,
Camera}Module`, `content/tweaks/module/MoreVillagersModule` and, handed over by U25,
GrabChickens' carried-chicken render (`GrabChickensModule.Client`, `LivingEntityRendererMixin`).
Code: `src/feature/cosmetics/` (server side, both builds) and `src/client/cosmetics/` (client build),
namespace `lp::feature::cosmetics` / `lp::client::cosmetics`. Selftests dx 1800..1899.

Most of these modules are *client* modules in Quark. On Bedrock the split is:

| Module | Where it runs | Vanilla clients |
|---|---|---|
| VariantAnimalTextures | server picks the variant (synced actor data `skin_id`), resource pack draws it | yes (resource pack) |
| MoreVillagers | server assigns the type (mark variant 7/8) and the boat trade, resource pack draws it | yes |
| UsesForCurses (binding) | server flags the armor stand, resource pack draws the body | yes |
| GrabChickens render | server flags the carried chicken, resource pack moves it to the head | yes |
| GreenerGrass, SoulCandles, WoolShutsUpMinecarts, UsesForCurses (vanishing), BucketsShowInhabitants, Camera | client mod (native hooks / `client/ui`) | no: like Quark's client modules they need the client mod (SPEC §1) |

## 1. Modules covered

### GreenerGrass
Recolours the biome tint of grass-like blocks (`blockList`), of leaves/vines (`leavesList`, only when
`affectLeaves`) and of water (only when `affectWater`) through a 3×3 colour matrix: for an input tint
(R, G, B) the output is `R' = r·(R,G,B)`, `G' = g·(R,G,B)`, `B' = b·(R,G,B)` (each row a dot product,
truncated, clamped to 0..255). Grass/foliage use `colorMatrix`, water `waterMatrix`.

### VariantAnimalTextures
Gives some animals random extra skins, chosen per entity from its id (stable for the entity's life):
- Cows: 4 extra skins + the vanilla one (5 equally likely); pigs: 3 + vanilla; chickens: 6 + vanilla;
  allays: 5 + vanilla (keys `enableCow`, `enablePig`, `enableChicken`, `enableAllay`).
- "Shiny" skins, 1 in `shinyAnimalChance` (0 = never) for cows, pigs, chickens, allays and (with
  their keys) rabbits, llamas, dolphins, slimes, armadillos and frogs. A shiny animal emits a
  happy-villager sparkle every 10 ticks when `shinySparkles` (random angle, 0.25..0.75 blocks out,
  at the top of the mob ±0.1, random velocity ±0.5).
- Pride bees (`enableLGBTBees`): a bee is "LGBT" with probability 0.056 (or always with
  `everyBeeIsLGBT`); it then gets one of 16 flag skins (not `helen`). A bee named (name tag) like a
  variant (`acebee`, `agenbee`, `arobee`, `beefluid`, `beesexual`, `beequeer`, `enbee`, `gaybee`,
  `interbee`, `lesbeean`, `panbee`, `polysexbee`, `transbee`, `helen`, `mlbeem`, `polyamorbee`,
  `salmacibee`, case-insensitive) always uses that skin; `wirebeegal`/`wiresegal` → `enbee`. Each
  skin has the four bee states (normal, angry, nectar, angry + nectar).
Variant rule (Quark, from the UUID's most significant bits `m`): shiny when `m % chance == 0`;
otherwise skin `|m % (n + 1)|` where index n (the last) is vanilla; LGBT when
`Random(m).nextDouble() < 0.056`, flag `|m % 16|`. Bedrock ids are sequential, so Lepton derives `m`
from the 64-bit unique id through SplitMix64 (a fixed mixing function) and Java's `Random` for the
bee draw.

### BucketsShowInhabitants
Bucket icons show the creature inside: axolotl bucket per axolotl colour (`showAxolotls`), Bucket of
Crab per crab colour (`showCrabs`, U20 `lepton:crab_bucket`), tropical fish bucket with the fish's
shape, pattern, base colour and pattern colour (`showTropicalFish`), and a shiny Bucket of Slime (U22
`lepton:slime_in_a_bucket`, `showShinySlime`, only when VariantAnimalTextures and
`enableShinySlime` are on and the stored slime is shiny).

### SoulCandles
A candle (or candle cake) whose block below is a soul-fire base (soul sand, soul soil) burns with
soul-fire flames instead of small flames, and 10% of its smoke particles become soul particles. If
the block below is an enchanting-power block (bookshelf) the block under that is checked instead.

### WoolShutsUpMinecarts
A minecart over a block that dampens vibrations (wool, wool carpets: `minecraft:dampens_vibrations`)
directly below its box makes no rolling/riding sound.

### UsesForCurses
- `vanishPumpkinOverlay`: a worn carved pumpkin with Curse of Vanishing does not draw the pumpkin
  blur overlay.
- `bindArmorStandsWithPlayerHeads`: an armor stand wearing a player head with Curse of Binding
  renders as a full player body (the armor stand model is hidden).

### MoreVillagers
Two villager types: ocean villagers (all ocean biomes, `oceanVillager`) and beach villagers (beach,
`beachVillager`), with their own clothes, and fishermen of those types buy a jungle boat (beach) or
an oak boat (ocean) at the Master trade level like the other types' boats.

### Camera
F12 (`quark.keybind.camera_mode`) toggles camera mode: the HUD is hidden and a camera HUD is drawn.
Keys while in camera mode (no screen open; holding sneak cycles backwards):
- `1` filter: none, Grayscale, Sepia, Desaturate, Oversaturate, Cool, Warm, Conjugate, Red/Green/Blue/
  Yellow Focus, Creeper-Vision, Enderman-Vision, Pixelize, Monochrome, Simulated Deuteranopia /
  Protanopia / Tritanopia / Achromatopsia (20 entries).
- `2` rulers: None, Rule of Thirds, Golden Ratio (1/2.61 and 1.61/2.61), Crosshair (centre); 1 px lines
  of `#33000000` inside the border area.
- `3` borders: None, Square, 4:3, 16:9, 21:9 (black bars), Polaroid (white frame
  `20 * (w*h / 518400)` UI units).
- `4` overlay: None, Date (`dateFormat`, colour `#f77700`, scale 2, bottom right), Postcard
  ("Greetings from %s!" with the world/server name, `#ef5425`, scale 3, top left +20), Watermark (the
  player name, scale 6, `#44000000`, no shadow), Held Item (the held item's name, centred, 40 below
  the top border).
- `5` + sneak: reset everything.
- Enter: shutter sound and screenshot (sneak + Enter while a shot is pending cancels it). The settings
  text, rulers and header are not drawn in the shot.
The number keys still select the hotbar slot in Java; Quark restores the previously held slot.
Settings HUD at (30, h-65): `[1] Filter: X` ... `[5] Reset Settings (Must Sneak)`, gold values, a
camera icon at (8, h-47), header "Camera Mode" (aqua) and "[F12 to exit, ENTER to shoot]" centred at
the top.

### GrabChickens render (from U25)
A chicken carried by a player (U25's GrabChickens) is drawn on top of the player's head, lowered by
`0.25 - chickenOffset` blocks (`farming` config, -1..1), with its legs hidden.

## 2. Blocks and items

No placeable content. Hidden items (no recipe, not in the creative menu, `minecraft:icon` only), used
as icon sources by BucketsShowInhabitants on mod clients:

| Id | Icon |
|---|---|
| `lepton:bucket_icon_axolotl_<lucy\|wild\|gold\|cyan\|blue>` | water bucket with that axolotl |
| `lepton:bucket_icon_crab_<red\|blue\|green>` | water bucket with that crab |
| `lepton:bucket_icon_slime_shiny` | shiny Bucket of Slime |
| `lepton:bucket_icon_tropical_bucket` | the water bucket alone (tropical fish base) |
| `lepton:bucket_icon_tropical_<a\|b>` | fish body of shape A (Kob family) / B (Flopper family), dyeable (tinted with the base colour) |
| `lepton:bucket_icon_tropical_<a\|b>_<1..6>` | pattern layer, dyeable (tinted with the pattern colour) |

Resource-pack overrides of vanilla client entities (identifiers unchanged; vanilla behaviour kept,
Lepton additions keyed on `query.skin_id` / `query.mark_variant`): `minecraft:cow`, `pig`, `chicken`,
`allay`, `rabbit`, `llama`, `dolphin`, `slime`, `armadillo`, `frog`, `bee`, `villager_v2`,
`zombie_villager_v2`, `armor_stand`. Textures (original art, `tools/textures/cosmetics.ps1`):
`textures/lepton/cosmetics/variants/<cow1..4|pig1..3|chicken1..6|allay1..5>.png`,
`<animal>_shiny.png` (all ten animals), `bees/<variant>/<normal|angry|nectar|angry_nectar>.png`,
villager clothes `villager/biome_<beach|ocean>[_baby].png`, `zombie_villager/biome_<beach|ocean>[_baby].png`,
`gui/camera.png`, bucket icons. Sound `lepton.camera.shutter` (`sounds/lepton/camera_shutter.wav`,
generated by `tools/textures/cosmetics.ps1`).

Behaviour-pack trade data: none (the boat trade is added natively, §3).

## 3. Mechanics

**Server pass** (`ServerLevelTickEvent`, every 10 ticks, plus right after `Mob::initializeComponents`
so component groups that reset synced data are corrected at once):
- Animal skins: for `minecraft:cow|pig|chicken|allay` (adults; the 26.x baby models and the warm/cold
  climate variants keep vanilla skins) the synced `SkinId` is 0 (vanilla), 1..n (variant) or 31
  (shiny); rabbits/llamas/dolphins/slimes/armadillos/frogs: 0 or 31; bees: 0 or 1..17 (index in the
  list above + 1). Disabled modules/keys write 0.
- Chicken (GrabChickens render): `SkinId |= 32` while the chicken's vehicle is a player, `+ 64` when it
  sits on the second seat, `+ 128 * code` with `code = round((chickenOffset + 1) * 100)` (0..200) read
  from `config/farming.json` (`enableGrabChickens`, `chickenOffset`; defaults if the file is absent).
- Sparkles: every 10 ticks a shiny animal (not bees) spawns `minecraft:villager_happy` at a random
  point as in §1 (`shinySparkles`).
- Armor stands: `SkinId = 1` when the head slot holds a player head with Curse of Binding
  (`bindArmorStandsWithPlayerHeads`), else 0.
- Villagers: a `minecraft:villager_v2` or `minecraft:zombie_villager_v2` initialised as *Spawned* or
  *Born* (not Loaded/Transformed) gets tag `lepton:villager_beach` (beach biome, `beachVillager`) or
  `lepton:villager_ocean` (any ocean biome, `oceanVillager`), checked one tick later at its position.
  Tagged villagers get mark variant 7 (beach) / 8 (ocean) whenever MoreVillagers is enabled (tags
  survive saving and the villager ↔ zombie villager conversions, which copy tags).
- Trades (`EconomyTradeableComponent::_generateTrades`): a fisherman with mark variant 7/8 whose
  Master tier has no boat trade gets "1 jungle boat (beach) / 1 oak boat (ocean) → 1 emerald", 12 uses,
  30 trader XP, like vanilla's boat trade.

**Client mod** (`src/client/cosmetics/`):
- GreenerGrass: hook `ClientBlockPipeline::PositionDependentTintResolver::_get` (and the
  position-agnostic resolver used for items/particles): if the block's name is in `blockList` (or
  `leavesList` with `affectLeaves`, or the tint method is Water with `affectWater`) the resolved colour
  goes through the matrix. Chunks are re-meshed after `/quark reload`.
- SoulCandles: hook `AbstractCandleBlock::animateTickBedrockLegacy`; when the soul-base check passes,
  each lit candle position gets a `BlueFlame` (soul fire flame) particle instead of the candle flame,
  and smoke is replaced by `Soul` with 10% chance (otherwise normal smoke).
- WoolShutsUpMinecarts: the minecart's looping sounds (`SoundEngine::registerLoop` for `minecart.base`
  / `minecart.inside`) get volume 0 while a `minecraft:dampens_vibrations` block is directly under the
  cart's box.
- UsesForCurses (vanishing): the pumpkin blur texture lookup is answered with a transparent texture
  while the local player's head slot is a carved pumpkin with Curse of Vanishing.
- BucketsShowInhabitants: the GUI item renderer (`ItemRenderer::renderGuiItemNew` /
  `renderGuiItemInChunk`) draws the hidden icon items of §2 instead of the vanilla bucket; tropical
  fish buckets are drawn as three layers (bucket, body tinted with the base colour, pattern tinted
  with the pattern colour). Variants are read from the bucket's NBT: axolotl `Variant`, tropical fish
  `Variant`/`MarkVariant`/`Color`/`Color2` (Bedrock stores the fish's shape, pattern and colours),
  crab `lepton_crab.variant` (U20), slime `UniqueID` in U22's stored NBT.
- Camera: §1, drawn with `client/ui` (HUD hook) and raw key events; the filter is a full-screen
  pixel-shader pass over the swap chain image before it is presented (D3D12); the shot is the
  game's screenshot (`ClientInputCallbacks::handleScreenshotButtonPress`) taken on the next frame.

## 4. Screens

Only the Camera HUD (client mod, §1). No vanilla-client fallback: Quark's Camera is client-only and
Bedrock's vanilla client has no hook for it. Keybind `key.lepton.camera_mode` (default F12,
`config/keybinds.json`).

## 5. Config

Section `cosmetics` (`config/cosmetics.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableGreenerGrass` | bool | true | GreenerGrass module (client) |
| `affectLeaves` | bool | true | |
| `affectWater` | bool | false | |
| `blockList` | string[] | Quark's list (`minecraft:large_fern`, `tall_grass`, `grass_block`, `fern`, `short_grass`, `potted_fern`, `sugar_cane`, + mod ids) | |
| `leavesList` | string[] | Quark's list (`spruce/birch/oak/jungle/acacia/dark_oak_leaves`, `vine`, + mod ids) | |
| `colorMatrix` | `{r:[3], g:[3], b:[3]}` | `r [0.89,0,0]`, `g [0,1.11,0]`, `b [0,0,0.89]` | grass/foliage matrix rows |
| `waterMatrix` | `{r:[3], g:[3], b:[3]}` | `r [0.86,0,0]`, `g [0,1,0.22]`, `b [0,0,1.22]` | water matrix rows |
| `enableVariantAnimalTextures` | bool | true | |
| `enableCow`, `enablePig`, `enableChicken`, `enableAllay` | bool | true | |
| `enableShinyRabbit`, `enableShinyLlama`, `enableShinyDolphin`, `enableShinySlime`, `enableShinyArmadillo`, `enableShinyFrog` | bool | true | |
| `enableLGBTBees` | bool | true | |
| `everyBeeIsLGBT` | bool | false | |
| `shinyAnimalChance` | int | 2048 | 1 in X; 0 disables |
| `shinySparkles` | bool | true | |
| `enableBucketsShowInhabitants` | bool | true | (client) |
| `showAxolotls`, `showCrabs`, `showTropicalFish`, `showShinySlime` | bool | true | |
| `enableSoulCandles` | bool | true | (client) |
| `enableWoolShutsUpMinecarts` | bool | true | (client) |
| `enableUsesForCurses` | bool | true | |
| `vanishPumpkinOverlay` | bool | true | (client) |
| `bindArmorStandsWithPlayerHeads` | bool | true | (server) |
| `enableMoreVillagers` | bool | true | |
| `oceanVillager`, `beachVillager` | bool | true | |
| `enableCamera` | bool | true | (client) |
| `dateFormat` | string | `"MM/dd/yyyy"` | Java `SimpleDateFormat` pattern (letters `yMdHhmsaEc`... see §7); an invalid pattern falls back to the default |

Client keys are read by the client mod from its own `config/cosmetics.json`; server keys from the
server's (in a local world both are the same file).

## 6. Strings

Mod lang (`assets/lang/*/cosmetics.json`), Quark's keys and wording:
`quark.camera.filter` "Filter: ", `quark.camera.filter.<none|grayscale|sepia|desaturate|oversaturate|cool|warm|conjugate|redfocus|greenfocus|bluefocus|yellowfocus|creeper|enderman|bits|monochrome|deuteranopia|protanopia|tritanopia|achromatopsia>`
("None", "Grayscale", "Sepia", "Desaturate", "Oversaturate", "Cool", "Warm", "Conjugate", "Red Focus",
"Green Focus", "Blue Focus", "Yellow Focus", "Creeper-Vision", "Enderman-Vision", "Pixelize",
"Monochrome", "Simulated Deuteranopia", "Simulated Protanopia", "Simulated Tritanopia",
"Simulated Achromatopsia"), `quark.camera.rulers` "Rulers: " + `rulers0..3` ("None", "Rule of Thirds",
"Golden Ratio", "Crosshair"), `quark.camera.borders` "Borders: " + `borders0..5` ("None", "Square",
"4:3", "16:9", "21:9", "Polaroid"), `quark.camera.overlay` "Overlay: " + `overlay0..4` ("None", "Date",
"Postcard", "Watermark", "Held Item"), `quark.camera.header` "Camera Mode", `quark.camera.info`
"[{} to exit, ENTER to shoot]", `quark.camera.reset` "Reset Settings (Must Sneak)",
`quark.camera.greetings` "Greetings from {}!", `quark.keybind.camera_mode` "Camera Mode".
RP `texts.d/*/cosmetics.lang`: `key.lepton.camera_mode` "Camera Mode", `quark.subtitles.camera_shutter`
"Camera shoots", hidden icon item names.

## 7. Bedrock differences / not ported

- Animal variants apply to adult temperate cows/pigs/chickens: Bedrock 26.x has climate variants
  (warm/cold) and separate baby models that Quark 4.1 (MC 1.21.1) does not know; they keep their
  vanilla skins. Shiny rabbits/llamas/frogs replace every colour variant like in Quark; baby
  rabbits/llamas/dolphins/armadillos keep vanilla skins.
- Variants come from the server (synced actor data) instead of being computed by each client, so they
  also work for vanilla clients; the client-side config of Quark becomes server config.
- GreenerGrass, SoulCandles, WoolShutsUpMinecarts, pumpkin overlay, BucketsShowInhabitants, Camera need
  the client mod (as in Quark they are client modules).
- Bucket icons change in GUI slots (inventory, containers, hotbar); the held-in-hand / dropped item
  keeps the vanilla icon (Bedrock caches in-hand item meshes per item id).
- Camera filters: Quark uses Minecraft post shaders; Lepton runs an equivalent full-screen pass on
  the frame before it is presented (colour matrices, focus, posterize/mosaic for Pixelize/
  Creeper/Enderman), so the camera HUD text is filtered too. Quark's commented-out filters (bumpy,
  notch, blobs, pencil, watercolor, sobel) are not in its list either. Screenshots are saved by the
  game's screenshot function into the game's screenshot folder (Bedrock has no `screenshots/` folder
  in the game directory) and include the filter. The date pattern supports the `SimpleDateFormat`
  letters `y M d H h m s S a E` and quoted text.
- MoreVillagers: types are assigned to villagers that spawn or are born in those biomes (natural
  spawns, spawn eggs, structures, breeding); Java's 50% parent-type inheritance on breeding is replaced
  by Bedrock's breeding rule plus the biome rule. The boat trade is added natively.
- UsesForCurses binding: Bedrock player heads have no owner skin, so the body uses the standard
  player skin, like the head itself.
- GrabChickens render: the head position is computed from the vanilla player seat positions
  (`minecraft:rideable` seats, 0.4 blocks to the side) in the chicken's client animation.

## 8. Tests

Selftests (dx 1800..1899): `cosmetics.config` (defaults, matrices), `cosmetics.variant_rules` (variant /
shiny / bee formulas against fixed ids), `cosmetics.animal_skins` (spawned cow/pig/chicken/bee/rabbit get
the expected `skin_id`, disabled keys write 0), `cosmetics.sparkles`, `cosmetics.armor_stand_binding`,
`cosmetics.villager_types` (spawn in beach/ocean → tag + mark variant 7/8, plains untouched),
`cosmetics.fisherman_boat`, `cosmetics.carried_chicken` (rides a simulated player → flags and offset
code), `cosmetics.tint_matrix` (matrix math).
E2E: `tools/e2e/server/scenarios/cosmetics.ps1` runs `cosmetics.*`; `tools/e2e/client/scenarios/cosmetics.ps1`
spawns every animal variant and shiny, pride bees, beach/ocean villagers, a bound armor stand and a
carried chicken, places candles on soul sand, toggles camera mode with each border/overlay/filter and
fills the inventory with axolotl/tropical fish buckets, saving a screenshot of each.
