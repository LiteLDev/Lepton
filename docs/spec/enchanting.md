# U13 enchanting: matrix enchanting, ancient tomes and color runes

## 1. Modules covered

### MatrixEnchanting (oddities)

The enchanting table is replaced by the *matrix enchanter*. The table keeps the item being
enchanted (slot 0, one item) and lapis (slot 1). Lapis is burnt into *charge* (`chargePerLapis`
pieces per lapis). Pressing **+** generates an enchantment *piece* (costs XP levels and one charge)
that lands on the bench (the list on the right). Pieces are polyomino shapes that the player places
on a 5x5 grid, rotates and merges (dropping a piece on a placed piece of the same enchantment adds
its value as XP to the placed one, levelling it up). Every placed piece becomes an enchantment of
the output item (slot 2); taking the output finishes the enchanting. The pending matrix is stored
on the item, so an item taken out keeps its pieces ("Has Pending Enchantments" tooltip). Bookshelves
around the table raise the number of pieces and their cost; lit dyed candles *influence* (or, over
soul sand, *dampen*) the odds of enchantments by colour; extra blocks can be configured as
influencers. Air gaps may be water or short blocks.

### AncientTomes (tools)

Ancient Tomes are rare chest loot, each holding one enchantment (Quark's valid list, max level > 1).
In an anvil a tome raises that enchantment of an item or enchanted book by one level, up to
`maxLimitBreakLevels` levels past the normal maximum, for `normalUpgradeCost` levels
(+`limitBreakUpgradeExtraCost` per level past the cap). Overlevelled books combine with items in
the anvil (vanilla refuses levels above the cap). Overlevelled books glow rainbow, Efficiency VI
diamond/netherite pickaxes instamine deepslate under Haste II, master librarians exchange tomes,
and tomes can optionally curse the gear.

### ColorRunes (tools)

The Rune smithing template (rare dungeon / fortress / temple loot, duplicated with corundum or
amethyst + cobblestone) changes the colour of an enchanted item's glint in the smithing table: dye
(16 colours), blaze powder (rainbow) or nothing (blank = no glint). Purely visual.

## 2. Blocks and items

| Id | English name | Notes |
|---|---|---|
| `lepton:matrix_enchanter` | Enchanting Table | Quark's `quark:matrix_enchanter`. Enchanting-table shape (12 px high, vanilla enchanting table textures, light 0, hardness 5, resistance 1200, pickaxe), drops `minecraft:enchanting_table`. Creative: Functional ("construction") only matters when `automaticallyConvert` is false (see §3.1). No recipe (as in Quark). |
| `lepton:ancient_tome` | Ancient Tome | Stack 1, uncommon rarity (yellow name), enchantment glint. The tome's enchantment is stored as the item's `ench` list (one entry, the enchantment's max level) plus the marker `lepton:tome = 1`. Creative: Items group (the plain tome, "(Any Enchantment)"); `/quark enchanting tome <enchant>` gives a specific one. |
| `lepton:smithing_template_rune` | Smithing Template | Quark's rune template, glint. Creative next to the spire armor trim template (Items group, `itemGroup.name.smithing_templates`). |

Recipes (crafting table, `bp/recipes/rune_duplication*.json`):

| Recipe | Pattern | Result |
|---|---|---|
| `lepton:rune_duplication` | `#S#` / `#C#` / `###`, `#` = item tag `lepton:corundum` (U12's nine plain corundum blocks, Quark's `#quark:corundum`), `S` = cobblestone (tag `minecraft:stone_tool_materials`), `C` = rune template | 2 rune templates |
| `lepton:rune_duplication_no_corundum` | same, `#` = `minecraft:amethyst_block` | 2 rune templates |

Quark enables exactly one of the two (recipe flags `color_runes` and `corundum`): the corundum one
when the Corundum module is on, else the amethyst one; neither without ColorRunes. Lepton also
honours U12's `crystalsCraftRunes` (Quark declares that option but its recipe does not read it):
the corundum recipe needs `enableColorRunes`, U12 `enableCorundum` and `crystalsCraftRunes`, the
amethyst one `enableColorRunes` and not the corundum one. The disabled recipe is taken out of the
world's recipe list at world load (`src/feature/enchanting/Recipes.cpp`).

Smithing "recipes" (applied natively, §3.3): template `lepton:smithing_template_rune` + base =
any enchanted item or lodestone compass + addition = dye of a colour (→ that colour), blaze powder
(→ rainbow) or nothing (→ blank).

Loot table `bp/loot_tables/blocks/matrix_enchanter.json`: 1 enchanting table.

## 3. Mechanics

### 3.1 Matrix enchanter blocks

- `automaticallyConvert` (default true): Quark turns every placed / right-clicked vanilla
  enchanting table into the matrix enchanter. Lepton keeps the vanilla `minecraft:enchanting_table`
  block and makes **it** the matrix enchanter: using it opens the matrix screen instead of the
  vanilla one. The look (with the animated book) and the drop are the same as in Quark, without a
  block swap (Bedrock custom blocks cannot have block entities or the book animation).
- `lepton:matrix_enchanter` is also a matrix enchanter (Quark's block). When the module is disabled,
  using it turns it back into a vanilla enchanting table (Quark).
- Table state (world DB `lp:enchanting:table:<dim>:<x>:<y>:<z>`): the item (SNBT), the lapis stack
  (SNBT), `charge`. Breaking / replacing the table drops the item and the lapis (Quark `onRemove`).
- A custom name of the table (Quark's `CustomName`) is not kept: Bedrock's enchanting table name
  is the vanilla one ("Enchant" screen title `container.enchant`).
- Sneak + use with an item in hand places the item as usual (vanilla rule).

### 3.2 The matrix (Quark's `EnchantmentMatrix`, exact)

Grid 5x5. A *target* is a book (count 1) or an unenchanted item with max stack size 1 that has
an enchant slot (`Item::getEnchantSlot() != 0`). Other items stay in slot 0 without a matrix.

Per table, recomputed every 20 ticks and when the item changes:

- `enchantability` = `Item::getEnchantValue()`; `zeroEnchantabilityButInTag` = enchantability 0 and
  the item is enchantable.
- Bookshelf power: for each of the 8 positions (dx,dz) in {-1,0,1}² \ (0,0) whose *air gap* at
  `table + (dx,0,dz)` and `+ (dx,1,dz)` is air, or water (`allowUnderwaterEnchanting`), or a short
  block (`allowShortBlockEnchanting`: visual shape ≤ 3/16 high from the bottom, or ≥ 13/16 from
  the top, or no shape), add the power of the blocks at `(2dx, y, 2dz)` for y in 0..1, and for
  corners also `(2dx, y, dz)` and `(dx, y, 2dz)`. Power: a bookshelf (`minecraft:bookshelf` or any
  block id ending in `_bookshelf` except `chiseled_bookshelf`, i.e. U6/U7 variant bookshelves) = 1;
  an *influencer* = 1 (instead of its own power). `bookshelfPower = min(floor(power), maxBookshelves)`.
- Influencers (`allowInfluencing`): a **lit dyed candle** (16 colours; plain candles are not) with
  stack = number of candles (1..4); its colour's entry of `influencesList` boosts (`id`) or dampens
  (`-id`) enchantments. With `soulCandlesInvert`, a candle on soul sand / soul soil, or on a
  bookshelf that stands on soul sand / soul soil, swaps boost and dampen. `statesToInfluences`
  entries `"<block>[state=v,...];<strength>;<hex colour>;<enchant list>"` make other block states
  influencers with stack = strength. Each influenced enchantment gets `influence += stack`,
  each dampened one `influence -= stack`.
- Particles: Quark's client `animateTick` coloured dust trails from influencers to the table and
  enchant glyphs from bookshelves. Lepton spawns the vanilla enchanting glyph particles itself
  (vanilla table) and coloured dust trails (`minecraft:colored_flame_particle`-free:
  `minecraft:redstone_wire_dust_particle` is not colourable on Bedrock, so the trail uses the
  dust-plume particle tinted by molang `variable.color` through the `lepton:influence_dust` particle
  in the resource pack) from lit influencing candles, every 10 ticks per table that a player is
  near (8 blocks), 1/16 chance per candle like Quark.

Piece generation (button **+**, Quark `generateAndPay`):

- Allowed when the matrix exists, `canGeneratePiece`, `validateXp`, and (charge > 0 or creative).
  Creative players pay nothing; others lose `getNewPiecePrice()` levels and 1 charge.
- `canGeneratePiece`: false if enchantability 0, `!allowZeroEnchantability` and
  `zeroEnchantabilityButInTag`; false if no enchantment can be rolled. Book: needs `allowBooks`,
  `count < baseMaxPieceCountBook + max(0, min(power - 1, maxBookshelves)) / 7`. Item:
  `shelves = min(power, maxBookshelves)`, `e = round(enchantability * shelves / maxBookshelves)`,
  `count < baseMaxPieceCount + (shelves + 1) / 2 + e / 2`.
- `getMinXpLevel(power)`: book `(int)(min(power, maxBookshelves) * minLevelScaleFactorBook)`; item
  `count > minLevelCutoff ? (int)(cutoff * scale) - cutoff + count : (int)(count * scale)`.
- `getNewPiecePrice() = 1 + (piecePriceScale == 0 ? 0 : count / piecePriceScale)`.
- `validateXp`: creative, or level ≥ min level and level ≥ price.
- Roll: candidate enchantments are all Bedrock enchantments that are discoverable (Java tag
  `#in_enchanting_table`; else need `allowUndiscoverableEnchantments` or `undiscoverableWhitelist`),
  not treasure (else `allowTreasures`, or a book and `treasureWhitelist`), not in
  `disallowedEnchantments`, and applicable to the item (primary or secondary enchant slots; books
  take everything). Book level: `L = bookEnchantability + rand(max(1, power) * 2)`; the highest
  level i (max..min) with `minCost(i) ≤ L ≤ maxCost(i)`, else 1. Items always level 1. Skip it if
  `value(level) + totalValue[ench] > value(max) + maxXP(max)` where `maxXP(l) = l < max ? l : 0`
  and `value(l) = 1 + Σ_{i<l} maxXP(i)`.
- Weight (`normalizeRarity`): Java weight × 8, with 8 → 5 and 16 → 25; × `1 + clamp(influence,
  ±influenceMax) * influencePower`; then against the *marked* pieces: same enchantment ×
  `dupeMultiplier`, incompatible × `incompatibleMultiplier` (first match only); a candidate not
  matching any marked piece becomes marked. Without `normalizeRarity` the plain Java weight.
  Java weights from Bedrock frequencies: Common 30 → 10, Uncommon 10 → 5, Rare 3 → 2, VeryRare 1 → 1.
  If all weights are 0 every weight becomes 1. Weighted pick (truncated to int).
- The new piece: same *type* (texture variant 0..7) as an existing piece of that enchantment, else
  `typeCount % 8` (typeCount++); colour from the enchantment's hash (HSB h random, s 0.8..1,
  b 0.75..1, seeded by the enchantment id); a random shape of the 12 Quark shapes (Plus, Block, U,
  S, T, Twig, Squiggle, Fish, Stairs, J, H, the odd block) rotated 0..3 times. Pieces get ids
  `count`, go to the front of the bench; count++. A book's first piece adds up to 2 extra counts
  (each with 50%), as in Quark. Influenced pieces mark the matrix as influenced.
- Place (piece benched, every cell inside and free) / remove (back to the bench) / rotate (benched
  only, `(x, y) → (y, -x)`) / merge (placed + benched of the same enchantment below max level: xp
  += value of the benched piece, level up while xp ≥ maxXP, benched piece removed, marked carried).
- Output: a copy of the item (books become enchanted books) with every placed piece as an
  enchantment at its level; nothing if two placed pieces are the same / incompatible enchantments.
  The matrix tag is removed from the output. Taking it empties slot 0, plays the enchanting sound
  (`random.enchant`/`block.enchanting_table.use`), counts the enchant statistic.
- The matrix is written to the item (`lepton:matrix` string tag, JSON) after every operation.

### 3.3 Ancient tomes

- Loot (`lootTables`, `ancientTomesItemQuality`): each entry `table,weight` (Java ids, matched to
  Bedrock `loot_tables/<path>.json`; `minecraft:` may be omitted; weight 0 = off). Lepton hooks
  `LootTable::fill` + `LootPool::addRandomItems`: for every roll of the table's **first pool** the
  tome replaces that roll's item with probability `weight / (poolWeight + weight)` (the exact
  effect of Quark's extra pool entry). The tome gets a random valid enchantment.
- Valid enchantments: `enchantNames` (Java ids mapped to Bedrock's), minus max level 1 ones when
  `sanityCheck`. Unknown names are logged and skipped.
- Anvil (`AnvilHelper`, see §4.3): left = item / enchanted book / tome, right = tome or enchanted
  book. Quark's `onAnvilUpdate` exactly: merge left + right enchantments (upgrade = max level),
  incompatible → no result; right tome (needs `combineWithBooks` when left is a book) raises the
  tome's enchantment by 1 if left has it at level ≥ 1 and < max + `maxLimitBreakLevels`; cost
  `normalUpgradeCost` (+ `limitBreakUpgradeExtraCost` × levels past max). Book + book / item +
  book is only handled by Lepton when one side is overlevelled (cost = Σ levels + repair costs);
  everything else is vanilla. Renaming is not offered (vanilla anvil text not readable).
  `curseGear`: the result also gets a random curse (binding / vanishing). Using it costs XP levels,
  consumes the inputs and damages the anvil like vanilla (12% chance, 3 steps).
- `overleveledBooksGlowRainbow`: enchanted books with a level above the max show Quark's rainbow
  name on mod clients (tooltip), see §7.
- `deepslateTweak`: deepslate breaks instantly with a pickaxe with Efficiency ≥ 6 whose speed ≥ 45
  (diamond / netherite) and Haste ≥ II (`deepslateTweakNeedsHaste2`).
- `librariansExchangeAncientTomes`: a master (level 5) librarian, when traded with, gets one extra
  offer: a random valid tome + max-level book of that enchantment → that tome (3 uses, 3 XP, price
  multiplier 0.2), once per villager. Lepton adds it natively to the villager's offers (every 10 s
  for villagers whose unlocked trade tier, actor data `TradeTier`, is 4 and that sell enchanted
  books). Bedrock matches the book of a trade exactly: the exchange takes a book with exactly that
  enchantment at max level (Quark also takes higher levels and books with more enchantments);
  any ancient tome is accepted as the first item, as in Quark.

### 3.4 Color runes

- Loot (first pool, same method as tomes): dungeon `dungeonWeight` (10), nether fortress
  `netherFortressWeight` (8), jungle temple `jungleTempleWeight` (8), desert pyramid
  `desertTempleWeight` (8), quality `colorRunesItemQuality` (0).
- Smithing: template + enchanted item (or lodestone compass) + dye/blaze powder/nothing → copy of
  the item with `lepton:rune = <colour>`; no result if the item already has that colour. Applied
  through the workstation helper (§4.3). The template is consumed.
- The colour is shown in the tooltip (`Upgrade:` + coloured "<Colour> Runic Etching", rainbow
  letters for rainbow) on mod clients.

## 4. Screens

### 4.1 Matrix enchanting screen (mod clients)

`ui::CustomScreen` drawn like Quark's 176x166 panel (grey frame, title, inventory label), centred:
item slot (15,20), lapis slot (15,44), charge bar (7,32 4x18, blue), arrow, output slot (59,32),
the 5x5 grid at (86,11) of 10 px cells (checkered), the **+** button (86,63 50x12, greyed when not
allowed), the bookshelf count with a book icon (50,55, green `0xc8ff8f`), the XP price
(`quark.gui.enchanting.min` when below the min level; green/red), the bench list (139,11, 28x64,
pieces drawn at half scale, ▲/▼ to scroll), player inventory (8,84) + hotbar (8,142).
Mouse: list click selects a piece; grid left click places / merges / picks up (shift = remove
only); right click rotates the selection; the hovered piece shows Quark's tooltip (name in gold,
`quark.gui.enchanting.influence/dampen`, `quark.gui.enchanting.upgrade`, `left_click`,
`right_click`, `merge`). Slots are click-to-move: an inventory stack goes to the item slot
(1 item) or the lapis slot (lapis), slot items go back to the inventory, the output is taken into
the inventory. Every action is a request to the server (`enchanting:op`, `enchanting:slot`); the
server answers with `enchanting:state` (full state JSON). Esc closes; the server closes the screen
when the table is broken or the player is more than 8 blocks away.

### 4.2 Vanilla-client fallback (forms)

`SimpleForm` "Enchanting Table": text summary (item, lapis, charge, bookshelves, cost, the grid as
5 lines of coloured `■`/`□` with piece letters, the bench list), buttons: **Insert item** (form
listing the inventory's matrix targets), **Add lapis**, **Take item back**, **Take lapis**,
**+ Generate piece**, one button per benched piece (opens a `CustomForm`: column, row, rotation
step slider, or "merge into" dropdown), one button per placed piece (**Remove**), **Take result**.
Every action re-opens the form with the new state.

### 4.3 Anvil and smithing helper

While an anvil or smithing table screen is open, the server reads the screen's input slots every
4 ticks (`PlayerUIContainer` slots AnvilInput/AnvilMaterial, SmithingTableInput/Material/Template).
When they form a tome / overlevel / rune combination:

- mod clients get `enchanting:craft_preview` and draw (overlay on the anvil / smithing screen) the
  result item, the XP cost and a **Craft** button that sends `enchanting:craft`; the server
  re-validates, consumes the inputs, gives the result.
- vanilla clients: the vanilla client keeps the anvil / smithing screen open against server close
  packets and a form can't show over it, so the player is told in chat to close the screen
  ("Close the anvil to apply the Ancient Tome."); once it is closed (inputs back in the inventory)
  the server sends a `ModalForm` "Apply Ancient Tome?" / "Apply Rune?" with result name and cost;
  **Apply** re-finds the same stacks in the inventory and applies.
- mod clients: after **Apply** the server answers `enchanting:craft_preview` with kind `applied` and
  the client leaves the screen itself (server close packets don't close it either).

## 5. Config

Section `enchanting` (`config/enchanting.json`), keys and defaults as SPEC §5.1, plus
`influencesList` (string[16], Quark's private `influencesList` option, wool colour order):
`["minecraft:unbreaking", "minecraft:fire_protection", "minecraft:knockback,minecraft:punch",
"minecraft:feather_falling", "minecraft:looting,minecraft:fortune,minecraft:luck_of_the_sea",
"minecraft:blast_protection", "minecraft:silk_touch,minecraft:channeling",
"minecraft:bane_of_arthropods", "minecraft:protection",
"minecraft:respiration,minecraft:loyalty,minecraft:infinity",
"minecraft:sweeping_edge,minecraft:multishot",
"minecraft:efficiency,minecraft:sharpness,minecraft:lure,minecraft:power,minecraft:impaling,minecraft:quick_charge",
"minecraft:aqua_affinity,minecraft:depth_strider,minecraft:riptide",
"minecraft:thorns,minecraft:piercing", "minecraft:fire_aspect,minecraft:flame",
"minecraft:smite,minecraft:projectile_protection"]`. Fewer than 16 entries disables candle
influence (Quark logs an error).

`lootTables` default: `["minecraft:chests/stronghold_library,20", "minecraft:chests/simple_dungeon,20",
"minecraft:chests/bastion_treasure,25", "minecraft:chests/woodland_mansion,15",
"minecraft:chests/nether_bridge,0", "minecraft:chests/underwater_ruin_big,0",
"minecraft:chests/underwater_ruin_small,0", "minecraft:chests/ancient_city,4", "lepton:monster_box,5"]`.
`enchantNames` default: feather_falling, thorns, sharpness, smite, bane_of_arthropods, knockback,
fire_aspect, looting, sweeping_edge, efficiency, unbreaking, fortune, power, punch, luck_of_the_sea,
lure, loyalty, riptide, impaling, piercing, density, breach, wind_burst (all `minecraft:`).
Enchantment ids are Java ids (`minecraft:sweeping_edge`); Bedrock ids (`minecraft:sharpness`) and
Bedrock spellings are accepted too. Item quality options are kept for parity (Bedrock pools ignore
luck for chests, so quality has no effect, as luck is 0 for chest loot in Java too).

## 6. Strings

| Key | en_US |
|---|---|
| `quark.gui.enchanting.min` | `{}+` |
| `quark.gui.enchanting.influence` | `Candle Influence: +{}%` |
| `quark.gui.enchanting.dampen` | `Candle Influence: {}%` |
| `quark.gui.enchanting.upgrade` | `Upgrade: {}/{}` |
| `quark.gui.enchanting.left_click` | `Left Click to Select` |
| `quark.gui.enchanting.right_click` | `Right Click to Rotate` |
| `quark.gui.enchanting.merge` | `Click to Upgrade` |
| `quark.gui.enchanting.pending` | `Has Pending Enchantments` |
| `quark.misc.ancient_tome_tooltip` | `+I {} (Max. {})` |
| `quark.misc.ancient_tome_tooltip_curse` | `Applies a random curse` |
| `quark.misc.ancient_tome_tooltip_any` | `(Any Enchantment)` |
| `upgrade.quark.rune_upgrade` | `Runic Etching` |
| `item.quark.smithing_template.rune.applies_to` / `ingredients` / `base_slot_description` / `additions_slot_description` | `Enchanted Items` / `Blaze Powder, Dye, or Nothing` / `Add an enchanted item` / `Add blaze powder, dye, or leave empty` |
| `rune.quark.<colour>` | `<Colour> Runic Etching` (16 dyes, rainbow, blank) |
| `lp.enchanting.*` | Lepton-only text (container titles, form texts, enchantment names for forms) |

## 7. Bedrock differences / not ported

- **Glint colour (ColorRunes rendering, rainbow overlevelled books).** Bedrock draws the glint in
  its renderer with a fixed texture; LeviLamina 26.51.7 exposes no glint colour, texture or
  render-type symbol (`mce::framebuilder::GlintDescription` is an empty opaque struct, no glint
  function in `mc/client`), so the client mod cannot recolour or hide a glint per item. The rune is
  applied, stored and shown in the tooltip (Quark's `Upgrade:` lines), but the glint keeps the
  vanilla purple. Same for the rainbow glint of overlevelled books; their name rarity (EPIC) cannot
  be changed per stack either.
- **Matrix enchanter block**: the vanilla table itself is the matrix enchanter (no conversion);
  the floating item rendered over the book (Quark's renderer) is not drawn (no block entity
  renderer hook for vanilla tables in the exposed client API).
- **Anvil/smithing results** are offered through the helper (§4.3) instead of appearing in the
  vanilla result slot: the vanilla client computes anvil/smithing previews itself and only accepts
  vanilla recipes. Renaming in the same step is not supported.
- **Advancements** (influence, overlevel, instamine_deepslate, full_rainbow): not ported (SPEC §8).
- **Hints (JEI)**: not ported (SPEC §8).
- Disabled ColorRunes keeps the rune duplication recipes in the pack (no runes are obtainable
  without loot, so nothing new can be crafted).
- Item quality has no effect (chest loot has luck 0 in Java as well).

## 8. Tests

Selftests (dx 1300..1399): `enchanting.matrix_rules` (formulas: price, min level, max pieces,
value/maxXP), `enchanting.matrix_ops` (generate, place, rotate, remove, merge, output),
`enchanting.matrix_power` (bookshelves, air gaps, water, carpet, candle influence and soul
inversion, dx 1300..1310), `enchanting.table_flow` (use a table, slot moves, generate, take output,
XP and lapis paid; mod client via net testing; dx 1320), `enchanting.table_form` (vanilla fallback
forms; dx 1330), `enchanting.table_drop` (breaking drops the contents; dx 1340),
`enchanting.tome_anvil` (tome upgrade, limit break, cost, incompatible), `enchanting.tome_loot`
(loot injection rules), `enchanting.rune_smithing`, `enchanting.rune_duplication` (which of the
two recipes the config enables, the world's recipe list, the corundum item tag, the corundum
recipe's ingredients and result), `enchanting.config_off` (dx 1390),
`enchanting.table_use` (a simulated player uses both table blocks; walking away closes; dx 1350),
`enchanting.anvil_helper` / `enchanting.smithing_helper` (preview message, Craft, cost, consumed
inputs; dx 1360 / 1365; simulated players get no server-side container manager, so these name the
open station through `testing::openStation`), `enchanting.loot_chest` (real chest loot unpacking;
dx 1370), `enchanting.deepslate`, `enchanting.librarian` (a real librarian's trade list, tier, exchange
matching; dx 1380).
E2E: `tools/e2e/server/scenarios/enchanting.ps1` (selftests), `tools/e2e/client/scenarios/enchanting.ps1`
(items; the matrix screen: select, rotate, place, generate, tooltip, take; a real anvil and smithing
table with the craft overlay or the vanilla form; screenshots).
