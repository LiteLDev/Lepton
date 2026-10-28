# U16 hud: usage ticker, elytra indicator, improved tooltips, auto-walk and back-button keybinds

All five modules are **client** modules in Quark 4.1-488 (category `client`): they only change
what the local player sees and how the client reacts to input. In Lepton they live in the client
build (`src/client/hud/`); the shared logic that decides *what* to show (counts, food tiers, fuel
times, attribute values, enchantment targets, shulker grid sizes, the ticker animation) is in
`src/feature/hud/` and is compiled into both builds so the server harness can selftest it.

## 1. Modules covered

### UsageTicker
Shows, just above the hotbar, the item in the main hand, in the off hand and the four armour pieces
together with the **total** number of that item in the player's inventory, whenever that changes.
Each slot is a "ticker element" that slides up from below the screen, stays for 3 s and slides back.
Bows and crossbows (without Infinity) show the ammunition they would fire instead of themselves.

### ElytraIndicator
When the chest slot holds an elytra that can still fly, an elytra icon is drawn at the left end
of the armour bar. If the armour is below 20 the armour bar is shifted 8 px to the right and its
10th icon is dropped, so the bar keeps its width.

### ImprovedTooltips
Adds graphical components to item tooltips:
- **Attributes**: replaces the vanilla attribute modifier lines with a row of icons + values
  (hold Shift for the vanilla lines). Values are coloured green/red against the item currently
  worn/held in the same slot, with an animated up/down arrow.
- **Food**: a row of hunger shanks (one per `foodDivisor` nutrition points, compressed to one shank
  + "xN" above `foodCompressionThreshold`), and a saturation tier text.
- **Shulker boxes**: a grid preview of the contents in a frame tinted with the box colour; the
  vanilla "contents" lines are removed.
- **Maps**: a 64x64 preview of the map.
- **Enchanted books**: under each enchantment, small icons of the items it can go on.
- **Fuel**: in furnace screens, a flame icon and how many items the fuel smelts ("x1.5").

### AutoWalkKeybind
A keybind (unbound by default) toggles walking forward automatically. Pressing the forward key
stops it. While auto-walking, auto-jump is forced on (the player's step height is below 1), and
the HUD shows an animated "Auto-Walking" banner.

### BackButtonKeybind
A keybind (default: mouse button 5, Quark's GLFW mouse button index 4) that acts as the "Back"
button of the open screen; with no back button, it closes the screen.

## 2. Blocks and items

None (no blocks, items or entities). Resource-pack textures used by the client mod (original pixel
art drawn by `tools/textures/hud.ps1`), under `textures/lepton/hud/`:

| File | Size | Use |
|---|---|---|
| `elytra.png` | 9x9 | ElytraIndicator icon |
| `armor_full.png`, `armor_half.png`, `armor_empty.png` | 9x9 | armour icons (attribute tooltips) |
| `attribute/<name>.png` | 9x9 | attribute icons: `attack_damage`, `attack_speed`, `armor`, `armor_toughness`, `knockback_resistance`, `max_health`, `speed`, `luck`, `attack_knockback`, `reach_distance`, `attack_range`, `gravity`, `step_height`, `swim_speed`, `unknown` |
| `upgrade.png`, `downgrade.png` | 13x13 | upgrade / downgrade arrows |
| `slot/<group>.png` | 9x9 | equipment group icons (`mainhand`, `offhand`, `head`, `chest`, `legs`, `feet`, `any`, `armor`, `body`) |
| `food_full.png`, `food_half.png`, `food_empty.png`, `food_full_hunger.png`, `food_half_hunger.png`, `food_empty_hunger.png` | 9x9 | food shanks (normal / poisonous) |
| `fuel.png` | 13x13 | fuel flame |
| `shulker_widget.png` | 30x30 | 9-slice frame of the shulker preview (5 px corners, 18 px cells, tinted) |
| `map_background.png` | 71x71 | map preview background |

## 3. Mechanics

### UsageTicker (`enableUsageTicker`)
Elements, in order: main hand (`enableMainHand`), off hand (`enableOffHand`), head, chest, legs,
feet (`enableArmor`). Each client tick (20/s, `ClientLevelTickEvent`):
- `real` = the item in the slot; `count` = total count (below); `shown` = logical stack (below).
- If `shown` is empty: `liveTicks = 0`.
- Else if the item, the damage (damageable items) or the total changed since the last tick
  (`real` or `shown` compared with the previous ones): restart the animation: `MAX_TIME = 60`,
  `ANIM_TIME = 5`; when animating out (`liveTicks < 5` and > 0) `liveTicks = 60 - liveTicks`; when
  not animating in (`liveTicks <= 55`): `liveTicks = 55` if it was visible, else `60`.
- Else `liveTicks--` while > 0.

Logical stack: bows and crossbows without Infinity show their projectile (off hand first, then
the inventory: arrows, tipped arrows, spectral arrows; crossbows also fireworks in the off hand).
Otherwise, in the hand slots, a non-stackable non-block item shows nothing; a stackable item whose
total equals the count of the stack in the slot shows nothing (no point showing what the slot
already shows). Armour always shows.

Count: for stackable items and block items, the sum of the counts of all stacks of the same item,
aux and user data in the inventory (36 slots + armour + off hand), at least the stack's own count;
otherwise 1.

Render (HUD, every frame, not when the HUD is hidden): `p` = animation progress (0..1, eased in /
out over 5 ticks with the frame's partial tick), `anim = -p (p - 2) * 20`; `x = width/2`,
`y = height - anim`. `armor = slot is armour`; Bedrock has no main-arm option (main arm = right),
so the side is right for armour, left for hands (swapped by `invert`). `barWidth = 190`, plus 58
when the side is the off-hand side (left) and the off hand is not empty. `index = slots - i - 1`
(slots = 4 for armour, 2 for hands; `i` = 0 main hand / feet ... see Quark's `EquipmentSlot`
index: mainhand 0, offhand 1, feet 0, legs 1, chest 2, head 3). `x += barWidth/2 * side + index*20`;
left side: `x -= slots*20 + 0` then `+ shiftLeft`; right side: `+ shiftRight`. The stack is drawn
at `(x, y)` with the total count as its count text and the durability bar.

### ElytraIndicator (`enableElytraIndicator`)
Condition: the chest slot holds `minecraft:elytra` that can fly (damage < max damage - 1). Then in
the game's armour bar renderer (`HudArmorRenderer`): `shift = armor >= 20 ? 0 : 8`; the bar's icons
move right by `shift` and, when shifted, only 9 icons are drawn; the elytra icon is drawn at the
bar's left end (`x = barX - 8 + shift`). Shown even when the armour value is 0.

### ImprovedTooltips (`enableImprovedTooltips`)
Hook: the game's hover-text builder (`ClientInstanceModel::getFormattedHoverText`) for the lines,
and the hover-text renderer (`HoverTextRenderer::render`) to draw icons, items and maps into blank
lines reserved for them (Quark's tooltip components). Components are inserted at line 1 (just below
the name), in Quark's order: attributes, food, saturation, shulker, map; enchanting rows go after
their enchantment line; the fuel row goes last.

- **Attributes** (`attributeTooltips`): not while Shift is held. Bedrock items have no attribute
  modifier components; the values come from the item: attack damage (`Item::getAttackDamage`, main
  hand, shown as base 1 + bonus, like Java's `ADD_VALUE` display), armour (`getArmorValue`), armour
  toughness (`getToughnessValue`), knockback resistance (netherite armour 10 %, shown "+10%"), in
  the item's equipment group (main hand for weapons/tools, head/chest/legs/feet for armour).
  Entries of the same attribute and group are merged. Each entry: 9x9 icon, value at +12 px, next
  entry 20 px after the text. With more than one group, each group gets its own line starting with
  the group icon. The vanilla modifier lines are removed. `showUpgradeStatus`: when the player's
  item in that equipment slot is a different non-empty item with attributes, values are green when
  higher, red when lower, white when equal, with a 13x13 up/down arrow at (-2,-2) that bobs 1 px
  every half second (`animateUpDownArrows`). Without comparison, negative values are red.
- **Food** (`foodTooltips`, `showSaturation`): items with nutrition > 0 (ominous bottles excluded).
  `count = ceil(nutrition / foodDivisor)`; poisonous = one of its effects is harmful (poison,
  hunger, nausea, wither, ...: those shanks use the "hunger" sprites). If `count >
  foodCompressionThreshold`: one shank and `"x" + (count - (odd ? 1 : 0)) + (odd ? ".5" : "")` in
  grey (#666666). The last shank is a half when nutrition is odd. Saturation (Java's absolute
  value, `min(20, nutrition * saturationModifier * 2)`): >= 19 -> tier 5, < 10: >= 8 -> 1, >= 6 -> 2,
  >= 2 -> 3, else 4; 10..19 -> 0. Text `quark.misc.saturation<N>` (or `bad_saturation<N>` when
  poisonous), grey.
- **Shulker boxes** (`shulkerTooltips`): shulker boxes with at least one item. The vanilla content
  lines are removed. Unless `shulkerBoxRequireShift` and Shift is up (then the line
  `quark.misc.shulker_box_shift`), a grid: `size` = number of non-empty stacks; dims = the first of
  `{1,1},{9,3},{9,5},{9,6},{9,8},{9,9},{12,9}` whose product is `size`, else `{min(size,9),
  1 + (size-1)/9}`; frame 5 px corners + 18 px cells, tinted with the dye colour
  (`shulkerBoxUseColors`; the undyed box keeps the frame white); items at
  `(6 + (i%9)*18, 6 + (i/9)*18)`; the frame is moved left if it would leave the screen. Items not
  matching the ChestSearching filter (U15) are darkened (when U15 exposes one; until then none are).
- **Maps** (`mapTooltips`): filled maps whose data the client has. Unless `mapRequireShift` and Shift
  is up (then `quark.misc.map_shift`), the map at half scale (64x64) over its background (71x71 at
  half scale, 3.5 px border). When the client has no data for the map it asks the server
  (vanilla `MapInfoRequestPacket`), so remote servers work too.
- **Enchanted books** (`enchantingTooltips`): after each enchantment line of an enchanted book, the
  items of `enchantingStacks` (Quark's default list, unknown ids skipped) that the enchantment's
  primary slots allow and that are enchantable, plus `enchantingAdditionalStacks`
  (`"enchant_id=item1,item2"`), at half scale, 10 per row (9 px apart), one line per row.
- **Fuel** (`fuelTimeTooltips`): only in furnace / blast furnace / smoker screens; burn time `t`
  ticks (`FurnaceBlockActor::getBurnDuration`); text `x%.0f` or `x%.1f` of `t / fuelTimeDivisor`
  in #FFB600 next to a 13x13 flame.

### AutoWalkKeybind (`enableAutoWalkKeybind`)
Keybind `key.lepton.autorun` (Quark `quark.keybind.autorun`), unbound by default. On press:
toggle auto-walk (only in a world). While auto-walking, every client tick sets the forward flag
of the local player's move input, and `LocalPlayer::isAutoJumpEnabled` returns true (Bedrock's step
height is 0.5625 < 1, so Quark always forces auto-jump; the option itself is never changed, so it is
"restored" automatically). Pressing the forward key (`key.forward` of the current keyboard layout)
stops it. `drawHud`: centered text at `y = hudHeight`, alternating every 500 ms between
`"OoO " + text + " oOo"` and `"oOo " + text + " OoO"`, text `quark.misc.autowalking`, white with
shadow, x from the width of `"OoO" + text + "oOo"`.

### BackButtonKeybind (`enableBackButtonKeybind`)
Default binding: mouse button 5 (`ActionX2`, Quark's mouse button value 4), plus the keyboard
keybind `key.lepton.back` (unbound by default, rebindable). When pressed while a screen (not the
HUD) is on top: the screen receives the game's "back" input (an Escape press), which presses its
Back / Done / Cancel button or closes it — exactly what Quark's search for those buttons does on
Bedrock, where every screen maps Escape to its back action. In the game's key-binding screen the
press is ignored (rebinding must not close the screen).

## 4. Screens

All features are drawn by the client mod (HUD, tooltips) or react to client input. **Vanilla-client
fallback: none** — these modules are purely client-side in Quark, too (SPEC §1: HUD, tooltips and
keybinds need the client mod). On a BDS with Lepton they work for every player with the client mod
(no server data is needed: the client has the item NBT of shulker boxes, and map data through the
vanilla map request). In local worlds they always work.

## 5. Config

Section `hud` (`config/hud.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableUsageTicker` | bool | true | UsageTicker module |
| `invert` | bool | false | Armour on the off-hand side, hands on the main-hand side |
| `shiftLeft` | int | 0 | Extra x offset of the left elements |
| `shiftRight` | int | 0 | Extra x offset of the right elements |
| `enableMainHand` | bool | true | |
| `enableOffHand` | bool | true | |
| `enableArmor` | bool | true | |
| `enableElytraIndicator` | bool | true | ElytraIndicator module |
| `enableImprovedTooltips` | bool | true | ImprovedTooltips module |
| `attributeTooltips` | bool | true | |
| `foodTooltips` | bool | true | |
| `shulkerTooltips` | bool | true | |
| `mapTooltips` | bool | true | |
| `enchantingTooltips` | bool | true | |
| `fuelTimeTooltips` | bool | true | |
| `shulkerBoxUseColors` | bool | true | |
| `shulkerBoxRequireShift` | bool | false | |
| `mapRequireShift` | bool | false | |
| `foodDivisor` | int | 2 | The value of each shank of food |
| `showSaturation` | bool | true | |
| `foodCompressionThreshold` | int | 4 | |
| `fuelTimeDivisor` | int | 200 | |
| `showUpgradeStatus` | bool | true | Colour attributes against the equipped item |
| `animateUpDownArrows` | bool | true | |
| `enchantingStacks` | string[] | Quark's list (diamond tools/armour, mace, shears, bow, fishing rod, crossbow, trident, elytra, shield, `lepton:pickarang`, ...) | Items shown under each enchantment (missing in SPEC §5.1's generated table; Quark option) |
| `enchantingAdditionalStacks` | string[] | [] | `enchant_id=item1,item2,...` |
| `enableAutoWalkKeybind` | bool | true | AutoWalkKeybind module |
| `drawHud` | bool | true | Show the "Auto-Walking" banner |
| `hudHeight` | int | 10 | Its y position |
| `enableBackButtonKeybind` | bool | true | BackButtonKeybind module |
| `backMouseButton` | int | 5 | Lepton: mouse button bound to Back (4 = X1, 5 = X2, 0 = none); Quark's default binding as a config value, because Bedrock key bindings cannot hold side mouse buttons |

Keybinds (`config/keybinds.json`, Controls screen): `autorun` (unbound), `back` (unbound keyboard key).

## 6. Strings

| Key | en_US |
|---|---|
| `quark.misc.autowalking` | Auto-Walking |
| `quark.misc.saturation0` .. `5` | Fulfilling Meal, Hearty Meal, Meal, Light Meal, Light Snack, Full-Course Meal |
| `quark.misc.bad_saturation0` .. `5` | Worryingly Fulfilling, Disturbingly Hearty, Ill-Advised Meal, Questionable Rations, Disgusting Snack, Dangerously Tantalizing |
| `quark.misc.shulker_box_shift` | Hold Shift to see contents |
| `quark.misc.map_shift` | Hold Shift to see the map |
| `key.lepton.autorun` (RP texts) | Walk Automatically |
| `key.lepton.back` (RP texts) | Back |

## 7. Bedrock differences / not ported

- **Main arm**: Bedrock has no left-handed option; the main hand is always on the right.
- **Attributes**: Bedrock items carry no attribute-modifier components. The attributes Bedrock
  items really have are shown (attack damage, armour, toughness, knockback resistance); attack
  speed, reach, luck, gravity, step height and swim speed do not exist on Bedrock items (their icons
  are drawn for completeness but no Bedrock item produces them). Bedrock has no attack cooldown,
  so there is no attack speed entry.
- **Auto-jump**: instead of switching the option on and back, the client reports auto-jump as on
  while auto-walking; the saved option is never touched (same visible behaviour).
- **Back button**: Bedrock screens are JSON-UI scenes without Java's button objects; the back action
  is the scene's Escape handling, which is what the Back/Done/Cancel buttons do there.
- **Side mouse buttons** cannot be stored in Bedrock key bindings, so the default mouse binding of
  Back is the config key `backMouseButton`; the keyboard binding is a normal rebindable keybind.
- **ChestSearching darkening** in the shulker preview depends on U15's search filter; it is applied
  when U15 publishes its filter (not yet available: no darkening).
- **Quark API hooks** (`IUsageTickerOverride`, `UsageTickerEvent`, `quark:no_tooltip` data component)
  are Java-mod API: not ported.

## 8. Tests

Selftests (dx 1600..1699; logic, run on BDS and in local worlds):
`hud.ticker_anim`, `hud.ticker_count`, `hud.ticker_bow`, `hud.food_tiers`, `hud.food_shanks`,
`hud.fuel`, `hud.shulker_dims`, `hud.shulker_contents`, `hud.enchant_items`, `hud.attributes`,
`hud.elytra`, `hud.config`; client build only: `hud.tooltip_lines`, `hud.keybinds`.

E2E: `tools/e2e/server/scenarios/hud.ps1` runs `hud.*`. `tools/e2e/client/scenarios/hud.ps1`
gives items and takes screenshots: `ticker` (main hand stack + counts above the hotbar),
`ticker-armor` (armour pieces), `elytra` (armour bar with the elytra icon), `tooltip-sword`
(attribute icons), `tooltip-food`, `tooltip-shulker` (grid preview), `tooltip-book` (enchanting
items), `tooltip-map`, `tooltip-fuel` (furnace screen), `autowalk` (banner, the player moved),
`back` (inventory closed by the back keybind).
