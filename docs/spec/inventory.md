# U15 inventory: sorting, transferring, searching and other inventory management

Reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `management/InventorySorting`,
`management/EasyTransferring`, `client/ChestSearching`, `management/HotbarChanger`,
`management/AutomaticToolRestock`, `management/QuickArmorSwapping`,
`management/ExpandedItemInteractions`, `management/ItemSharing`, plus the shared Quark classes
they use (`SortingHandler`, `InventoryTransferHandler`, `InventoryButtonHandler`,
`MiniInventoryButton`, `HeldShulkerBoxMenu`, the `ChangeHotbar` / `SortInventory` /
`InventoryTransfer` / `ShareItem` / `ScrollOnBundle` messages, `QuarkGeneralConfig`
`chestButtonSlotTarget` / `shulkerBoxes` / `interpretShulkerBoxLikeBlocks`).
Behaviour only; no Quark code or assets.

Code: `src/feature/inventory/` (server side, both builds) and `src/client/inventory/` (client
build). Selftest dx range: **1500..1599**.

## 1. Modules covered

### InventorySorting
Sort buttons and keybinds. In the player inventory a button above the top-right slot of the main
inventory sorts the player's 27 main slots (hotbar untouched). In a container screen that accepts
buttons (see "Accepted containers") a button at the top right of the container sorts the
container, and a button above the player inventory part sorts the player inventory. Keybinds
"Sort Player Inventory" and "Sort Target Inventory" (unbound by default). With `satisfyingClick`
a UI click sound plays when sorting. Sorting merges equal stacks, then orders them by Quark's
order (§3.1); empty slots go last; locked slots (the slot of a held shulker box that is open)
keep their place.

### EasyTransferring
Buttons in container screens above the player inventory: **Insert** (move every stack of the
player's main inventory, not the hotbar, into the container) and **Extract** (move every stack of
the container into the player's inventory, last slot first). With Shift held they become
**Insert (Filtered)** / **Extract (Filtered)**: only stacks of items the target already contains
move. **Shift Lock** (`enableShiftLock`): a toggle button / keybind that makes every slot click
behave as a Shift-click (quick move). Keybinds "Insert Items", "Extract Items", "Shift Lock".

### ChestSearching
A **Filter** button left of the container sort button toggles a search bar (117 x 10 at the top of
the container panel). While the bar is shown and not empty, every slot of the screen whose item
does not match is covered with `overlayColor` (default black, alpha 0.67); the text turns red
(`#FF5555`) when nothing matches. Right-click on the bar clears it. Matching (§3.3): name,
enchantments, potion effects, stored (book) enchantments, creative tab name, mod name, contents
of shulker boxes; `"exact"` and `/regex/` forms.

### HotbarChanger
Key **Z** ("Hotbar Swapper") slides three rows (the three main inventory rows) up above the hotbar
over `animationTime` ticks (default 7, height 90 units). Pressing hotbar key 1, 2 or 3 while it is
open swaps the hotbar with that row (all nine slots) on the server and closes it; the selected
hotbar slot stays. Pressing Z again closes it. Closed automatically in spectator mode.

### AutomaticToolRestock
When an item in the player's hand is destroyed (a tool breaks; the last item of a stack is placed
or used up), the server looks for a replacement in the player's main inventory (and the hotbar
with `checkHotbar`) and swaps it into the hand slot (main hand or off hand) at the end of the tick.
Matching order (§3.4): same item + same important enchantments, same item, then (with
`enableLooseMatching`) same tool class + important enchantments, same tool class. Armour never
restocks; `unstackablesOnly` limits it to unstackable items; `ignoredItems` are never touched.
When the Backpack (U14) is worn and nothing was found, the backpack contents are searched too.

### QuickArmorSwapping
Sneak + use (right-click) an armour stand (not in creative or spectator): the player's four armour
slots (and, with `swapOffHand`, the off hand) swap with the stand's. A slot whose player item has
Curse of Binding is skipped. If the stand's slot is empty and the main-hand item belongs in that
slot, the held item goes onto the stand.

### ExpandedItemInteractions
Right-click interactions inside inventory screens (left-click with `invertClicks`):
- **Armour** (`enableArmorInteraction`): right-click an armour piece (single item; also items of
  the `counts_as_armor` lists: elytra, carved pumpkin, skulls/heads) with an empty cursor to equip
  it; the worn piece goes to its slot. Not when the worn piece has Curse of Binding. Tooltip
  "Right-Click to Equip".
- **Shulker boxes** (`enableShulkerBoxInteraction`): right-click a shulker box slot while holding a
  stack on the cursor to insert the stack into the box ("Right-Click to Insert Item"); holding
  another shulker box dumps its contents in ("Right-Click to Dump Contents"); right-click with the
  shulker box on the cursor onto an item to insert that item. With `allowOpeningShulkerBoxes`,
  right-click a shulker box in the player inventory with an empty cursor (and no other container
  open) to open it ("Right-Click to Open"); that slot is locked while open.
- **Lava bucket** (`enableLavaInteraction`): right-click a lava bucket slot while holding a stack
  (not fire resistant, not a shulker box, not in creative) to destroy the stack, with the lava
  extinguish sound (volume 0.25, pitch 2-3). Tooltip "Right-Click to Trash Item" (red).
- **Bundles** (`allowRotatingBundles`): scrolling over a bundle with more than one item rotates its
  contents.

### ItemSharing
Shift + the chat key over an item in a container screen shares the item: the server (cooldown
`cooldown` ticks per player, default 100) sends every player the chat line
`<Sender> [Item Name]`. With `renderItemsInChat` the item icon is drawn with the message.

## 2. Blocks and items

None: this unit adds no blocks, items or recipes. It reads these item lists:

| List | Items (Bedrock ids) |
|---|---|
| shulker boxes | `minecraft:undyed_shulker_box`, `minecraft:shulker_box`, the 16 `minecraft:<colour>_shulker_box`, and any id containing `shulker_box` (Quark `interpretShulkerBoxLikeBlocks = true`) with max stack size 1 |
| counts as armour (head) | `minecraft:carved_pumpkin`, `minecraft:skull` and every `*_head` / `*_skull` item (`#minecraft:skulls`) |
| counts as armour (chest) | `minecraft:elytra` |
| counts as armour (legs, feet) | none |

## 3. Mechanics

### 3.1 Sort order (Quark `SortingHandler`)
Categories, in this order (the first that fits wins):
`TORCH` (`minecraft:torch`), `TOOL_PICKAXE`, `TOOL_SHOVEL`, `TOOL_AXE`, `TOOL_SWORD`,
`TOOL_GENERIC` (hoes), `ARMOR` (humanoid armour, not elytra), `BOW`, `CROSSBOW`, `TRIDENT`,
`ARROWS` (arrow, tipped arrows, spectral arrow), `POTION` (potion, splash, lingering),
`MINECART` (all minecarts), `RAIL` (in the order rail, powered rail, detector rail, activator rail),
`DYE` (the 16 dyes), `FOOD`, `ENCHANTED_BOOK`, `ANCIENT_TOME` (`lepton:ancient_tome`), `ANY`
(non-block items), `BLOCK`.

Within a category (each comparator falls back to the next; all end with the fallback):
- tools: higher tier speed first (wood 2, stone 4, iron 6, diamond 8, netherite 9, gold 12), more
  enchantment levels first, less damage first;
- swords: higher tier attack bonus first (wood 0, gold 0, stone 1, iron 2, diamond 3, netherite 4),
  enchantments, damage;
- armour: head, chest, legs, feet; then enchantments, damage (Quark's defence comparison compares
  a piece with itself and is always equal, so it is not used);
- bow / crossbow / trident: enchantment levels, damage;
- potions: total power (sum of amplifier x duration of the effects) higher first, then potion type
  id (higher first);
- food: nutrition higher first, then saturation `min(20, nutrition x modifier x 2)` higher first;
- enchanted books / tomes: more enchantments first, then enchantment names (descending), then levels;
- rails: list order;
- fallback: numeric item id, damage (aux) ascending, larger stacks first, stacks without extra
  data first, then a hash of the extra data.

Merging: equal stacks (same item and data) are merged up to the max stack size before sorting.

### 3.2 Accepted containers / transfer
A container screen gets the container buttons (sort container, insert, extract, shift lock,
filter) when its container part has at least `chestButtonSlotTarget` = 27 slots (Quark
`QuarkGeneralConfig`), and it is not a crafting table: chests, double chests, barrels, shulker
boxes, ender chests, minecart chests, Lepton virtual containers. The server resolves the open
container from the player's container manager (block entity at a position, ender chest, or an
entity's inventory).
- Insert: player slots 9..35 in order; each stack is inserted into the container (merge first,
  then empty slots). Filtered: only stacks whose item is already in the container (same item and
  data; or same item for damageable unstackables).
- Extract: container slots from last to first into player slots 0..35 (hotbar included),
  merge first. Filtered: only items already present in the player inventory.
- Spectators cannot transfer. Sorting: `forcePlayer` sorts the player inventory, otherwise the
  container (container sort requires `enableChests`, player sort requires `enablePlayerInventory`
  in the inventory screen / `enablePlayerInventoryInChests` in containers).
- Shift Lock: clicks on slots are sent as "quick move" requests; the server moves the stack to the
  other side (container <-> player; in the player inventory hotbar <-> main).

### 3.3 Search matching (Quark `ChestSearchingModule.namesMatch`)
`search` = trimmed, lower-case, formatting codes removed. Empty: everything matches. Empty slots
never match. If the search is `"..."` (length >= 3) the strings must be equal; if `/.../` it is a
regular expression (find); otherwise `contains`. A stack matches when any of these match:
shulker box contents (recursively), each enchantment `Name Level` (e.g. `sharpness v`), each potion
effect line, each stored enchantment of a book, the creative category name (Construction,
Equipment, Items, Nature), the mod name (`Minecraft`, `Lepton`/`Quark` for `lepton:` items), and
finally the item's display name.

### 3.4 Tool restock (Quark `AutomaticToolRestockModule`)
Triggers: a tool in a hand breaks (durability), or the hand slot becomes empty in the same tick in
which the player placed its last block or used up its last item. Not for armour; with
`unstackablesOnly` only for unstackable items. Search range: player slots 9..35, or 0..35 with
`checkHotbar`, never the hand slot itself. Predicates in order (first hit wins):
1. `enableEnchantMatching`: same item (and same damage if not damageable) and shares at least
   one important enchantment with the broken item (`enchantNames`, default silk touch, fortune,
   infinity, luck of the sea, looting) — note Quark's `retainAll` returns true when *any* is shared
   or the old one had none;
2. same item (same damage if not damageable);
3. `enableLooseMatching` and the item has tool classes (axe, hoe, shovel, pickaxe, sword, shears,
   fishing_rod, bow, crossbow): `enableEnchantMatching` and important enchantments on the old
   item -> same class + enchantment; then same class.
The swap happens at the end of the tick; `ignoredItems` (ids) on either side cancel it.

### 3.5 Hotbar change
Message `bar` in 1..3: hotbar slot `i` (0..8) swaps with slot `i + 9 * bar`.

### 3.6 Held shulker box
The shulker box is taken out of its slot while open and kept in the world DB
(`lp:inventory:held:<uuid>`) together with its contents, updated on every change; the box is shown
in a virtual chest screen (`game/VirtualContainer`, 27 slots, title = the box's name, shulker boxes
are rejected). On close the contents are written into the box, which goes back into its slot (or
the first free slot, or is dropped). A crash leaves the DB record: the box is given back when the
player joins again.

### 3.7 Item sharing
Client: Shift + chat key (T by default, keybind `share_item`) over a non-empty slot in a container
screen, at most once per `cooldown` ticks on the client too. Server: validates that the player
owns such an item (inventory, armour, off hand or the open container), throttles per player
(`cooldown`, ticks of server time), then sends every player the chat line
`<Sender> §r[§bItem Name§r]` (name colour from the item rarity: aqua for enchanted, white otherwise)
followed by the enchantments in grey; mod clients with `renderItemsInChat` also draw the icon in
an item card next to the chat for 10 s.

## 4. Screens

| Screen | Mod client | Vanilla client fallback |
|---|---|---|
| Inventory (`inventory_screen`) | Sort button above the top-right main slot (10 x 10, tooltip "Sort") | `/quark sort` |
| Container screens (chests, barrels, shulker boxes, ender chests, minecart chests, Lepton containers) | Top right of the container: Sort Container; left of it: Filter. Above the player part (right to left): Sort Inventory, Insert, Extract, Shift Lock. Search bar at the top of the container panel. Tooltips per §6 | `/quark sort container`, `/quark transfer insert|extract [filtered]` (container looked at, within 6 blocks); search and shift lock: none (purely visual / input features) |
| Hotbar changer (HUD) | Three translucent rows above the hotbar with row labels 1-3, item icons and counts | `/quark hotbar` (form with the three rows) or `/quark hotbar <1-3>` |
| Item interactions | Right-click handling in container / inventory screens, tooltips "Right-Click to ..." | `/quark shulker` opens the shulker box in the main hand in a chest screen; armour / lava / insert: none |
| Item sharing | Shift + T over a slot; item card on the HUD | `/quark share` shares the main-hand item |

Keybinds (`config/keybinds.json`, Controls screen): `sort_player` (unbound), `sort_container`
(unbound), `transfer_insert` (unbound), `transfer_extract` (unbound), `shift_lock` (unbound),
`change_hotbar` (Z), `share_item` (T; Quark uses the chat key).

## 5. Config

Section `inventory` (`config/inventory.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableInventorySorting` | bool | true | module |
| `enablePlayerInventory` | bool | true | sort button in the inventory screen |
| `enablePlayerInventoryInChests` | bool | true | player sort button in containers |
| `enableChests` | bool | true | container sort button |
| `satisfyingClick` | bool | true | click sound when sorting |
| `enableEasyTransferring` | bool | true | module |
| `enableShiftLock` | bool | true | shift lock button |
| `enableChestSearching` | bool | true | module |
| `overlayColor` | object `{r,g,b,a}` (0..1) | `{0,0,0,0.67}` | grey-out colour |
| `enableHotbarChanger` | bool | true | module |
| `animationTime` | double | 7 | ticks |
| `enableAutomaticToolRestock` | bool | true | module |
| `enchantNames` | string[] | `["minecraft:silk_touch","minecraft:fortune","minecraft:infinity","minecraft:luck_of_the_sea","minecraft:looting"]` | important enchantments |
| `enableLooseMatching` | bool | true | |
| `enableEnchantMatching` | bool | true | |
| `checkHotbar` | bool | false | |
| `unstackablesOnly` | bool | false | |
| `ignoredItems` | string[] | `["quark:trowel", "botania:exchange_rod", "botania:dirt_rod", "botania:skydirt_rod", "botania:cobble_rod"]` (Lepton also matches `lepton:` for `quark:`) | |
| `enableQuickArmorSwapping` | bool | true | module |
| `swapOffHand` | bool | true | |
| `enableExpandedItemInteractions` | bool | true | module |
| `enableArmorInteraction` | bool | true | |
| `enableShulkerBoxInteraction` | bool | true | |
| `enableLavaInteraction` | bool | true | |
| `allowOpeningShulkerBoxes` | bool | true | |
| `allowRotatingBundles` | bool | true | |
| `invertClicks` | bool | false | left clicks instead of right clicks |
| `enableItemSharing` | bool | true | module |
| `cooldown` | int | 100 | ticks |
| `renderItemsInChat` | bool | true | |
| `chestButtonSlotTarget` | int | 27 | Quark general config: minimum container slots for buttons |

## 6. Strings

| Key | en_US |
|---|---|
| `quark.gui.button.sort` | Sort |
| `quark.gui.button.sort_inventory` | Sort Inventory |
| `quark.gui.button.sort_container` | Sort Container |
| `quark.gui.button.insert` | Insert |
| `quark.gui.button.extract` | Extract |
| `quark.gui.button.insert_filtered` | Insert (Filtered) |
| `quark.gui.button.extract_filtered` | Extract (Filtered) |
| `quark.gui.button.filter` | Filter |
| `quark.gui.button.shift_lock` | Shift Lock |
| `quark.misc.trash_item` / `.invert` | Right-Click to Trash Item / Left-Click to Trash Item |
| `quark.misc.insert_shulker_box` / `.invert` | Right-Click to Insert Item / Left-Click to Insert Item |
| `quark.misc.merge_shulker_box` / `.invert` | Right-Click to Dump Contents / Left-Click to Dump Contents |
| `quark.misc.equip_armor` / `.invert` | Right-Click to Equip / Left-Click to Equip |
| `quark.misc.open_shulker` / `.invert` | Right-Click to Open / Left-Click to Open |
| `lp.inventory.shared` | <{}> {} |
| `lp.inventory.sorted` | Sorted. |
| `lp.inventory.no_container` | Look at a container within reach. |
| `lp.inventory.no_shulker` | Hold a shulker box in your main hand. |
| `lp.inventory.cooldown` | You can share an item again in {} seconds. |
| `lp.inventory.hotbar.title` / `.content` / `.row` | Hotbar Swapper / Swap the hotbar with a row of your inventory. / Row {} |
| `lp.inventory.disabled` | This feature is disabled. |
| `lp.inventory.transferred` | Moved {} items. |
| `lp.inventory.tab.*` | Construction / Equipment / Items / Nature |

Keybind names (RP `texts.d/*/inventory.lang`): `key.lepton.sort_player` Sort Player Inventory,
`key.lepton.sort_container` Sort Target Inventory, `key.lepton.transfer_insert` Insert Items,
`key.lepton.transfer_extract` Extract Items, `key.lepton.shift_lock` Shift Lock,
`key.lepton.change_hotbar` Hotbar Swapper, `key.lepton.share_item` Share Item.

## 7. Bedrock differences / not ported

- Buttons are drawn by the client mod (filled rectangles with pixel icons) instead of Quark's
  sprite sheet; positions follow Quark's offsets from the top-right slots.
- Container detection uses Bedrock's container screen context: block entity, ender chest or an
  entity inventory. Modded `ITransferManager` / `ICustomSorting` APIs have no Bedrock equivalent.
- Shift Lock sends quick-move requests to the server instead of faking the Shift key (Bedrock's
  client has no hook into its click handling); the effect (stack moves to the other inventory) is
  the same.
- Item sharing: Bedrock chat cannot embed item icons or hover tooltips; every player gets a text
  line with the name and enchantments, mod clients additionally see an item card (icon + name +
  enchantments) next to the chat. Default key: Shift + T in container screens (keybind
  `share_item`), because the client mod cannot read the game's chat key mapping.
- Held shulker boxes open in a virtual chest screen (`game/VirtualContainer`); the box is held by
  the server while open (§3.6) instead of locking the menu slot.
- Restock triggers come from the tool damage hook and from block placement / item use events (the
  Forge `PlayerDestroyItemEvent` does not exist on Bedrock).
- Quick armour swapping: Bedrock armour stands have one hand; `swapOffHand` swaps the player's off
  hand with the stand's hand item.
- Chest search text field keys: letters, digits, space and the US-layout punctuation
  (`" / . - _ : ( ) [ ] *` etc.).
- Vanilla clients get commands/forms (§4) for sorting, transferring, the hotbar changer, opening a
  held shulker box and sharing. Search, shift lock, right-click item interactions and bundle
  scrolling need the client mod (they are input features of the client).

## 8. Tests

Selftests (dx 1500..1599):
- `inventory.sort.order` (no world) — category order, tool/armour/food comparators.
- `inventory.sort.player` — sorting the player inventory merges and orders, hotbar untouched.
- `inventory.sort.container` (dx 1500) — net message sorts an open virtual container.
- `inventory.transfer` (dx 1505) — insert / extract, filtered and unfiltered.
- `inventory.quickmove` — shift-lock quick move between hotbar and main.
- `inventory.hotbar` — row swap via the message and the command handler.
- `inventory.restock` — broken pickaxe replaced; same-item / loose / enchant matching; ignored items.
- `inventory.armorswap` (dx 1510) — sneak interact with an armour stand swaps armour; binding curse.
- `inventory.search` — matcher rules (contains, quotes, regex, enchantments, shulker contents).
- `inventory.shulker` — insert into / dump into / read shulker contents; lava trash; equip armour.
- `inventory.heldshulker` (dx 1515) — opening a held shulker box, editing, closing writes back.
- `inventory.share` — cooldown, validation, broadcast text.

E2E: `tools/e2e/server/scenarios/inventory.ps1` runs `inventory.*`. Client
`tools/e2e/client/scenarios/inventory.ps1`: inventory screen with the sort button, a chest screen
with all buttons and the search bar filtering, the hotbar changer HUD; screenshots
`out/inventory/{inventory,chest,chest-search,hotbar}.png`.
