# U14 backpack: Backpack, Totem of Holding and Seed Pouch

Reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `oddities/BackpackModule`,
`oddities/TotemOfHoldingModule`, `tools/SeedPouchModule` and their item / entity / menu classes.
Behaviour only; no Quark code or assets are used.

## 1. Modules covered

### Backpack (oddities)
A Backpack worn in the chest armour slot gives three extra rows (27 slots) of inventory. It gives no
protection, can't be enchanted and never breaks. While it holds items it can't be taken off (Quark
puts a hidden Curse of Binding on it); a backpack holding items that is not worn by a survival
player spills its contents; a dropped backpack (item entity) spills its contents on the ground.
Ravagers drop Ravager Hide (rare), nine of which make a Bonded Ravager Hide block. The backpack is
dyeable like leather armour. Players with an empty hand can swap their backpack with the chest slot
of an armour stand.

### TotemOfHolding (oddities)
When a player dies, the items they would drop go into a floating Totem of Holding entity at the
death position (named after the player). Hitting the totem gives back 3 to 6 stacks per hit (or
everything, `dropAllItemsOnHit`), re-equipping armour and shields. When it is empty it shrinks away.
In "Dark Souls mode" a totem shatters (dropping its items) when its owner dies again.

### SeedPouch (tools)
A pouch holding up to `maxItems` (640) of one seed type (or bone meal). Using it on the ground
plants the seed; sneaking plants a `shiftRange` x `shiftRange` (3x3) area. Holding it in a hand
picks up matching seeds automatically. Sneak-using it on a chest empties it into the chest.

## 2. Blocks and items

| Id | English name | Notes |
|---|---|---|
| `lepton:backpack` | Backpack | item; stack 1; rarity rare; wearable `slot.armor.chest`, protection 0; dyeable, default colour `#A06540` (Quark's -6265536); no durability (never breaks); not enchantable; equip sound leather. Creative: Equipment, next to the saddle. Tag `lepton:backpack_blocked` (items with this tag can't go inside a backpack; the backpack itself has it, like Quark's `quark:backpack_blocked`). Worn model: attachable `lepton:backpack` (bag on the back, leather-tinted). |
| `lepton:ravager_hide` | Ravager Hide | item; stack 64; rarity rare. Creative: Items, after rabbit hide. |
| `lepton:bonded_ravager_hide` | Bonded Ravager Hide | block; hardness 1, wool sound, map colour black, burns (flammable), drops itself. Creative: Construction. |
| `lepton:seed_pouch` | Seed Pouch | item; stack 1; not enchantable. Creative: Equipment, after the lead. |
| `lepton:totem` | Totem of Holding | entity (no spawn egg); 0.5 x 1 box, no gravity, fire immune, no damage; holds up to 64 stacks. |

Recipes (crafting table; Bedrock JSON in `bp/recipes`, loaded only when the module/options are on,
see §3.4):

| Recipe | Pattern | Condition |
|---|---|---|
| `lepton:backpack`, `lepton:backpack_trapped_chest` | `LRL / LCL / LIL`; L leather, R ravager hide, C chest (or trapped chest; Quark: `#c:chests/wooden`), I iron ingot -> 1 backpack | `enableBackpack` and `enableRavagerHide` |
| `lepton:backpack_no_hide`, `lepton:backpack_no_hide_trapped_chest` | `LIL / LCL / LIL` -> 1 backpack | `enableBackpack` and not `enableRavagerHide` |
| `lepton:bonded_ravager_hide` | 3x3 ravager hide -> 1 block | `enableBackpack` and `enableRavagerHide` |
| `lepton:bonded_ravager_hide_uncompress` | 1 block -> 9 ravager hide (shapeless) | `enableBackpack` and `enableRavagerHide` |
| `lepton:seed_pouch_<seed>` (one per holdable seed) | ` S / HXH / H ` (stretched to 3 rows: `" S "`, `"HXH"`, `" H "`); S string, H leather, X any holdable seed (Quark: `#quark:seed_pouch_holdable`) | `enableSeedPouch` |

Loot: `bonded_ravager_hide` drops itself.

Item lists (Quark tags, translated to Bedrock ids; other units add items through the item tags):

- Seed pouch holdable (`quark:seed_pouch_holdable`): wheat, beetroot, melon, pumpkin, torchflower seeds,
  pitcher pod, potato, carrot (`#c:seeds`, `#minecraft:villager_plantable_seeds`), brown / red
  mushroom (`#c:mushrooms`), warped / crimson fungus, every sapling (oak, spruce, birch, jungle, acacia,
  dark oak, cherry, pale oak, mangrove propagule, azalea, flowering azalea), sweet berries, kelp,
  bamboo, sugar cane, nether wart, cocoa beans, plus any item with the item tag
  `lepton:seed_pouch_holdable` (other units' saplings, e.g. blossom saplings).
- Fertilizers (`quark:seed_pouch_fertilizers`): bone meal + tag `lepton:seed_pouch_fertilizers`.
- Vertical placement (`quark:seed_pouch_vertical_placement`): cocoa beans + tag
  `lepton:seed_pouch_vertical_placement`.

## 3. Mechanics

### 3.1 Backpack

- **Storage.** The 27 stacks live in the backpack item's own data (user NBT `lepton:backpack`,
  a list of `{Slot, Item}`), like Quark's container component: they travel with the item (death,
  drop, armour stand, ender chest, relog).
- **Opening (every client).** Opening the inventory (key E / inventory button) while wearing a
  backpack, not in creative and not sneaking opens the **backpack screen** instead: the vanilla chest
  screen titled "Backpack" with the 27 backpack slots above the player's inventory and hotbar (same
  order as Quark's screen: backpack rows above the inventory). Sneak + E opens the normal inventory
  (armour slots, 2x2 crafting), which Quark's screen also contains. The screen is served by a hidden
  helper entity `lepton:backpack_storage` next to the player; every move is server-authoritative and
  is written back to the backpack item on every change. Creative players get the normal inventory,
  like Quark.
- **Blocked items.** Items tagged `lepton:backpack_blocked` (backpacks) put into the screen are
  pushed back into the player's inventory (or dropped).
- **Locked while full** (`superOpMode` false). Quark adds Curse of Binding while a worn backpack
  holds items and removes it when empty. Lepton enforces the same without the visible enchantment:
  if a survival/adventure player takes a non-empty worn backpack out of the chest slot (inventory
  screen, armour swap), it is put back on the next tick.
- **Not worn, not empty.** A non-empty backpack anywhere else in a survival player's inventory
  (e.g. from a creative session) spills its contents at the player and is emptied (Quark's
  `inventoryTick`). In creative / spectator it keeps them and gets the lore warning
  "WARNING" / "This backpack has items in it, it will be wiped if its not equipped whilst in survival."
  (removed again when it no longer applies). `superOpMode` true disables the lock, the spill and the
  warning.
- **Items tick** (`itemsInBackpackTick`, default true): every tick, the items inside a worn non-empty
  backpack get their item `inventoryTick` (e.g. maps update), as in Quark.
- **Dropped backpack.** A backpack item entity holding items spills them at its position and is
  emptied (unless `superOpMode`).
- **Armour stand** (`allowArmorStandUnloading`): using an armour stand with both hands empty and not
  sneaking, when the player's or the stand's chest slot holds a backpack, swaps the two chest
  slots (with the backpack's contents), plays the leather equip sound and cancels the vanilla action.
- **Ravager hide** (`enableRavagerHide`): a ravager killed drops `baseRavagerHideDrop` (1) hide plus,
  with `chance = lootingLevel * extraChancePerLooting` (0.5): while `chance > baseRavagerHideDrop`
  one more hide and `chance -= 1`; then one more with probability `chance`. Looting level = the
  killer's weapon's Looting (Quark's code reads the Fortune level of the weapon, an apparent typo for
  Looting; Lepton uses the higher of Looting and Fortune, so both readings work).

### 3.2 Totem of Holding

- **Spawn.** When a player dies and drops items (inventory, armour, off-hand; Curse of Vanishing
  items are destroyed first, as vanilla), and the killer is not another player (or `enableOnPK`, or the
  player killed themselves), the items go into a new totem instead of the ground, in drop order
  (hotbar/inventory first, then armour, then off-hand). Position: the player's feet, y + 1, at least
  the dimension's minimum height + 3. The totem shows the player's name. The player's record
  (world DB `lp:backpack:player:<uuid>`) stores the totem's id (`lastTotem`) and the death position
  and dimension (Quark's `deathX/deathZ/deathDim`). A death without drops (empty inventory,
  keepInventory) sets `lastTotem` to none.
- **Collecting.** Hitting (attacking) the totem: if `allowAnyoneToCollect` is false only the owner
  (or a creative player) can collect. Each hit takes the first `3 + random(0..3)` stacks
  (`dropAllItemsOnHit`: all). Armour pieces are equipped if the slot is empty; a stack with Curse of
  Binding replaces a worn piece that is not a backpack and not cursed (the worn piece then goes to the
  inventory). Shields go to an empty off-hand. Everything else goes to the inventory; what doesn't fit
  drops at the totem. Particles: damage indicator and enchanted hit, one per stack.
- **Dark Souls mode** (`darkSoulsMode`): while its owner is online and the owner's `lastTotem` is not
  this totem, the totem drops everything (or destroys it with `destroyLostItems`), sends the owner
  "You feel a sense of dread as a previous Totem of Holding shatters" (bold) and disappears.
- **Dying.** An empty totem shrinks, spins faster and rises for 40 ticks (`DEATH_TIME`), then is
  removed. Alive it spins (1 degree per tick), bobs (`sin(t * 0.03) * 0.1`), glows (full brightness)
  and emits portal particles.
- **Not a soul compass.** Quark 4.1-488 has no Soul Compass item (it was removed before 4.x); the
  stored death position is kept for parity only (Bedrock's recovery compass covers it natively).

### 3.3 Seed Pouch

Contents (user NBT `lepton:pouch` = `{Item, Count}`): one item type with count 0..`maxItems`.

- **Fill / empty.** Quark uses bundle-style clicks in the inventory (right-click the pouch with seeds
  on the cursor, or right-click seeds with the pouch). Bedrock's inventory screen does not let items
  react to clicks, so Lepton maps them to item use:
  - use the pouch in the air (not sneaking): takes one stack out (up to the item's max stack size)
    into the inventory (drops what doesn't fit); sound `bundle.remove_one` (volume 0.8, pitch
    0.8..1.2).
  - sneak + use in the air: absorbs matching stacks from the inventory (empty pouch: the first
    holdable/fertilizer stack decides the type) until full; sound `bundle.insert`.
  - mod clients additionally get an "Insert" / "Take" pair of buttons on the inventory screen when a
    pouch is selected (they send the same requests).
- **Accepts:** empty pouch: a holdable seed, or a fertilizer if `allowFertilizer`; else the same item
  (with the same data) while `count < maxItems`.
- **Auto pickup.** A player holding a non-empty pouch in the main or off hand who picks up a fitting
  item puts it into the pouch first (main hand checked first); sound `bundle.insert` volume 0.2.
  What does not fit is picked up normally.
- **Planting.** Using the pouch on a block uses the content (a stack of up to its max size) on that
  block as the player would (seeds, saplings, bone meal, cocoa...): each successful use takes one from
  the pouch (none in creative). Sneaking: a `shiftRange` (seeds) or `fertilizerShiftRange` (bone meal)
  square centred on the clicked block, offsets `-floor(range/2) .. ` row by row; items in the vertical
  placement list (cocoa) use the square in the plane of the clicked face. Stops when the pouch is
  empty.
- **Chest insert.** Sneak-using on a chest (or trapped chest) first inserts the whole content into
  it; if nothing fits, the action bar says "The inventory you are trying to insert into is full..."
  (red). Then the 3x3 planting runs as in Quark (it does nothing on a chest).
- **Display.** Quark shows the contents in the name ("Seed Pouch (Wheat Seeds)"), a fill bar
  (`round(count * 13 / maxItems)`, blue for seeds, light grey for fertilizer) and an icon tooltip.
  Lepton writes a lore line `<item name> x<count>` (white) and a text fill bar for every client;
  mod clients get a tooltip line "<count>/<maxItems>".
- **Creative** (`showAllVariantsInCreative`): Quark lists a full pouch of every holdable seed in the
  creative tab. Bedrock's creative inventory is built from item JSON without per-stack data, so this
  is not possible (see §7); `/quark seedpouch <item>` (operator) gives a full pouch instead.

### 3.4 Disabled modules

Recipes of disabled modules are not loaded (a hook on the recipe loader skips them when the world
loads; changing the enable keys needs a world reload / restart). Mechanics stop immediately.
Existing items stay (inert), like Quark.

## 4. Screens

| Screen | Mod client | Vanilla client |
|---|---|---|
| Backpack inventory | vanilla chest screen "Backpack" (27 slots + inventory + hotbar), plus an overlay button "Inventory" (opens the normal inventory with armour/crafting); the normal inventory gets a "Backpack" button while a backpack is worn | the same chest screen; sneak + E opens the normal inventory |
| Seed pouch | inventory overlay buttons "Insert" / "Take" for the selected pouch + tooltip line | item use / sneak-use (§3.3) |
| Totem | none (entity) | none |

## 5. Config

Section `backpack` (`config/backpack.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enableBackpack` | bool | true | Backpack module |
| `superOpMode` | bool | false | Allow unequipping backpacks with items (no lock, no spill, no warning) |
| `enableRavagerHide` | bool | true | Ravager hide drop, bonded hide, hide recipe of the backpack |
| `itemsInBackpackTick` | bool | true | Items inside a worn backpack tick |
| `baseRavagerHideDrop` | int | 1 | Hides per ravager |
| `extraChancePerLooting` | double | 0.5 | Extra chance per Looting level |
| `allowArmorStandUnloading` | bool | true | Swap backpacks with armour stands |
| `enableTotemOfHolding` | bool | true | TotemOfHolding module |
| `darkSoulsMode` | bool | true | Totems shatter when the owner dies again |
| `enableOnPK` | bool | false | Spawn a totem on PVP kills too |
| `destroyLostItems` | bool | false | A shattered totem destroys its items |
| `allowAnyoneToCollect` | bool | true | Anyone may collect a totem |
| `dropAllItemsOnHit` | bool | false | One hit empties the totem |
| `enableSeedPouch` | bool | true | SeedPouch module |
| `maxItems` | int | 640 | Pouch capacity |
| `showAllVariantsInCreative` | bool | true | Creative variants (see §3.3 / §7) |
| `shiftRange` | int | 3 | Square side when sneaking (seeds) |
| `allowFertilizer` | bool | true | Bone meal (fertilizers) may go into the pouch |
| `fertilizerShiftRange` | int | 3 | Square side when sneaking (fertilizer) |

## 6. Strings

| Key | en_US |
|---|---|
| `item.quark.backpack` | Backpack (screen title) |
| `item.quark.backpack.warning.line_1` | WARNING |
| `item.quark.backpack.warning.line_2` | This backpack has items in it, it will be wiped if its not equipped whilst in survival. |
| `quark.misc.totem_darksouls` | You feel a sense of dread as a previous Totem of Holding shatters |
| `lp.backpack.pouch_full_chest` | The inventory you are trying to insert into is full... (Quark's literal text) |
| `lp.backpack.button.inventory` | Inventory |
| `lp.backpack.button.backpack` | Backpack |
| `lp.backpack.button.insert` | Insert |
| `lp.backpack.button.take` | Take |
| `lp.backpack.pouch_count` | {}/{} |
| `lp.backpack.pouch_lore` | {} x{} |

Names (RP texts): `item.lepton:backpack` Backpack, `item.lepton:ravager_hide` Ravager Hide,
`item.lepton:seed_pouch` Seed Pouch, `tile.lepton:bonded_ravager_hide.name` Bonded Ravager Hide,
`entity.lepton:totem.name` Totem of Holding.

## 7. Bedrock differences / not ported

- **Backpack screen layout**: Quark draws one screen (armour, crafting, backpack rows, inventory).
  Bedrock can't add slots to the inventory screen, so the backpack rows are a chest screen and armour /
  crafting stay in the normal inventory (sneak + E, or the mod client's button).
- **Binding curse** is enforced natively (the backpack is put back) instead of a real hidden
  enchantment, which Bedrock would render with a glint and an enchantment line.
- **Lock icon** (`has_items` model) and the **fill bar**: Bedrock item icons can't depend on item data
  for custom items, and a durability bar would make the pouch repairable (the crafting repair recipe
  would merge two pouches and lose contents). Lepton uses lore lines and tooltips instead.
- **Creative variants of the seed pouch**: creative items come from item JSON (no per-stack data);
  `/quark seedpouch <item>` gives a full pouch.
- **Bundle-style inventory clicks** of the seed pouch: mapped to item use (§3.3).
- **Curios** compatibility: Java-only mod, not ported.
- **Recipe toggles** apply on world load (Bedrock sends recipes once).

## 8. Tests

Selftests (dx 1400..1499):

| Name | dx | Checks |
|---|---|---|
| `backpack.items` | - | items/blocks/entity registered, recipes loaded |
| `backpack.storage` | 1400 | contents round-trip through the item data |
| `backpack.open_screen` | 1405 | opening the inventory while wearing opens the helper container; changes write back; closing removes the helper |
| `backpack.lock` | 1410 | taking a full backpack off puts it back; a full backpack in the inventory spills; creative warning lore |
| `backpack.dropped_spills` | 1415 | a dropped full backpack spills its contents |
| `backpack.armor_stand` | 1420 | empty-hand use swaps the chest slots |
| `backpack.ravager_hide` | 1425 | drop amount formula |
| `backpack.totem_spawn` | 1430 | death -> totem with the items, player record |
| `backpack.totem_collect` | 1435 | hits return stacks, armour equipped, empty totem dies |
| `backpack.totem_darksouls` | 1440 | second death shatters the first totem |
| `backpack.pouch_contents` | 1445 | absorb / split / accepts rules, capacity |
| `backpack.pouch_plant` | 1450 | 1x1 and 3x3 planting on farmland |
| `backpack.pouch_pickup` | 1455 | auto pickup into a held pouch |
| `backpack.pouch_chest` | 1460 | sneak-use on a chest inserts |
| `backpack.net` | - | mod-client switch requests |

E2E: `tools/e2e/server/scenarios/backpack.ps1` (runs `backpack.*`, asserts the pass count and log
lines); `tools/e2e/client/scenarios/backpack.ps1` (gives the items, wears the backpack, opens the
backpack screen, spawns a totem; screenshots `backpack-items.png`, `backpack-worn.png`,
`backpack-screen.png`, `backpack-totem.png`, `backpack-pouch.png`).
