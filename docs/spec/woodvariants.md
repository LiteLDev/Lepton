# U6 woodvariants: wooden variants of the vanilla woods

Ports seven Quark 4.1-488 building modules: VerticalPlanks, VariantBookshelves, VariantLadders,
WoodenPosts, Hedges, LeafCarpet and HollowLogs, for every **vanilla** wood Quark covers. The
variants of Quark's own woods (azalea, blossom/trumpet, ancient/ashen) belong to U7 (SPEC §2.3);
U7 adds them with the generator functions of `tools/pack/woodvariants.ps1` and the block tags of
§2.8, and gets the behaviour of this unit for free.

Quark's wood lists (`VanillaWoods`):

| List | Woods |
|---|---|
| ALL | oak, spruce, birch, jungle, acacia, dark_oak, mangrove, cherry, bamboo, crimson, warped |
| NON_OAK | ALL without oak |
| ALL_WITH_LOGS | ALL without bamboo |
| OVERWORLD_WITH_TREE | oak, spruce, birch, jungle, acacia, dark_oak, mangrove, cherry |

Pale oak is not in Quark 4.1-488 (Minecraft 1.21.1) and gets no variants.

## 1. Modules covered

### VerticalPlanks
A "vertical" planks block for every wood of ALL: the planks texture turned 90 degrees. Crafted from
three planks in a column, reverts to one planks block.

### VariantBookshelves
A bookshelf for every wood of NON_OAK (vanilla's bookshelf counts as the oak one). Each gives
enchanting power 1 like the vanilla bookshelf, is flammable except crimson/warped, and drops 3
books (the shelf itself with Silk Touch). `changeNames`: the vanilla Bookshelf is renamed
"Oak Bookshelf". Quark also adds a lectern recipe that takes any bookshelf.

### VariantLadders
A ladder for every wood of NON_OAK, behaving exactly like the vanilla ladder (wall placement,
climbable, breaks without support, waterloggable), flammable except crimson/warped, usable as
furnace fuel (wood: 300 ticks). `changeNames`: the vanilla Ladder is renamed "Oak Ladder".

### WoodenPosts
A post and a stripped post for every wood of ALL: a 4x4 pixel log beam placed along the axis of the
clicked face. On its four other sides it connects to chains (along that axis), to other posts
(along that axis) and, downwards, to hanging lanterns and ceiling hanging signs; upwards it
connects to a hedge. Lanterns and hanging signs can hang below a post. An axe strips a post.

### Hedges
A fence-like leaf block for every wood of OVERWORLD_WITH_TREE plus azalea and flowering azalea:
connects only to other hedges, shows its log post unless a hedge is below ("extend"), and lets
plants grow on top. Leaves are tinted like the leaves they come from.

### LeafCarpet
A 1-pixel carpet of leaves for every wood of OVERWORLD_WITH_TREE plus azalea and flowering azalea:
no collision, breaks instantly, needs a non-air block below, replaced like grass when a block is
placed into it (except by another leaf carpet), composts with a 20% chance.

### HollowLogs
A hollow log/stem for every wood of ALL_WITH_LOGS: a log with 2-pixel walls and an open tube
along its axis. Players crawl into horizontal ones by sneaking towards the open side
(`enableAutoCrawl`) and climb inside vertical ones like a ladder.

## 2. Blocks and items

`<w>` is the wood. Vanilla textures are referenced, not copied; textures marked "drawn" are
original pixel art from `tools/textures/woodvariants.ps1`.

| Module | Id | English name | States | Count |
|---|---|---|---|---|
| VerticalPlanks | `lepton:vertical_<w>_planks` (ALL) | Vertical Oak Planks, ... | - | 11 |
| VariantBookshelves | `lepton:<w>_bookshelf` (NON_OAK) | Spruce Bookshelf, ... | - | 10 |
| VariantLadders | `lepton:<w>_ladder` (NON_OAK) | Spruce Ladder, ... | `minecraft:block_face` (trait; north/south/west/east = facing) | 10 |
| WoodenPosts | `lepton:<w>_post`, `lepton:stripped_<w>_post` (ALL) | Oak Post, Stripped Oak Post, ... | `minecraft:block_face` (trait; axis), `lepton:side_0..3` none/chain/other_post: the four directions off the axis (y: N,S,W,E; x: down,up,N,S; z: down,up,W,E), 486 permutations per post | 22 |
| Hedges | `lepton:<w>_hedge` (OVERWORLD_WITH_TREE), `lepton:azalea_hedge`, `lepton:flowering_azalea_hedge` | Oak Leaf Hedge, ..., Mangrove Leaf Hedge, Azalea Leaf Hedge, Flowering Azalea Leaf Hedge | `lepton:north/east/south/west` bool, `lepton:extend` bool | 10 |
| LeafCarpet | `lepton:<w>_leaf_carpet` (OVERWORLD_WITH_TREE), `lepton:azalea_leaf_carpet`, `lepton:flowering_azalea_leaf_carpet` | Oak Leaf Carpet, ... | - | 10 |
| HollowLogs | `lepton:hollow_<w>_log` (8 overworld woods), `lepton:hollow_crimson_stem`, `lepton:hollow_warped_stem` | Hollow Oak Log, ..., Hollow Crimson Stem | `minecraft:block_face` (trait; axis) | 10 |

English names are Quark's (`block.quark.*`), zh_CN Quark's `zh_cn.json`.

### 2.1 Properties

| Block | Hardness / resistance | Sound | Flammable | Tool | Shape |
|---|---|---|---|---|---|
| vertical planks | 2 / 3 | wood, cherry_wood, bamboo_wood, nether_wood | not crimson/warped | axe | full cube |
| bookshelf | 1.5 / 1.5 | wood, cherry_wood, bamboo_wood, nether_wood | not crimson/warped | axe | full cube |
| ladder | 0.4 / 0.4 | as the wood's planks | not crimson/warped | axe | 3 px against the wall (Java LadderBlock) |
| post | 2 / 3 | as the wood's log (stem for crimson/warped) | not crimson/warped | axe | 4x4 px along the axis (6..10) |
| hedge | 2 / 3 | as the wood's fence | yes (azalea too) | axe, hoe | log post 6..10 x 0..15 + leaves 2..14 x 1..16, side arms 2..14 wide; extend: leaves 2..14 from 0 |
| leaf carpet | 0 / 0 | grass | yes | hoe | 0..1 px, no collision |
| hollow log | 2 / 2 | as the log | not crimson/warped | axe | four 2 px walls along the axis |

Hedges and leaf carpets use the leaves texture with the leaves' tint (`default_foliage`;
spruce `evergreen_foliage`, birch `birch_foliage`; cherry and azalea are not tinted) and the
pre-tinted `*_carried` texture for the item.

Posts, ladders, hedges and hollow logs are waterloggable (`minecraft:liquid_detection`).

### 2.2 Recipes (crafting table)

| Result | Pattern | Count |
|---|---|---|
| vertical planks | `#` `#` `#` (planks of the wood) | 3 |
| planks (revert) | 1 vertical planks (shapeless) | 1 |
| `<w>_bookshelf` | `###` `XXX` `###` (# = the wood's planks, X = book) | 1 |
| `minecraft:bookshelf` | same, oak planks (Quark's oak_bookshelf; vanilla's recipe stays) | 1 |
| `minecraft:lectern` | `SSS` ` B ` ` S ` (S = wooden slab, B = any variant bookshelf) | 1 |
| `<w>_ladder` | `# #` `#W#` `# #` (# = stick, W = the wood's planks) | 4 |
| `minecraft:ladder` | same with oak planks (Quark's oak_ladder) | 4 |
| `<w>_post` | `F` `F` `F` (the wood's wood / hyphae; bamboo: block of bamboo) | 8 |
| `stripped_<w>_post` | same with the stripped wood / hyphae / bamboo block | 8 |
| `<w>_hedge` | `L` over `W` (L = the wood's leaves, W = any log or wood of that wood, incl. stripped) | 2 |
| `azalea_hedge` / `flowering_azalea_hedge` | (flowering) azalea leaves over an oak log/wood (Quark's recipe when AzaleaWood is off; with U7's AzaleaWood on, U7's azalea-log recipe replaces it, see §3.8) | 2 |
| `<w>_leaf_carpet` | `##` (the wood's leaves) | 3 |
| `hollow_<w>_log` | ` L ` `L L` ` L ` (L = the log / stem) | 4 |

Java tag ingredients (`#minecraft:oak_logs`) become one recipe per member (log, wood, stripped
log, stripped wood), since Bedrock has no per-wood item tags.

The variant bookshelf / ladder recipes from the wood's planks have priority over the vanilla
bookshelf / ladder recipes that accept any planks (recipe `priority` -1).

### 2.3 Drops

Every block drops itself (no drop when the explosion destroys it: Bedrock applies explosion decay
itself), except bookshelves: 3 books, or the bookshelf with Silk Touch.

### 2.4 Creative inventory

Vertical planks and posts: construction, next to planks / logs; bookshelves and ladders:
items (functional); hedges and leaf carpets: nature, next to leaves; hollow logs: nature, logs group.

### 2.5 Block tags (contract for other units)

| Tag | Meaning (native behaviour in `src/feature/woodvariants`) |
|---|---|
| `lepton:vertical_planks` | data only |
| `lepton:bookshelf` | enchanting power 1 (vanilla enchanting table; U13's matrix enchanting and U18's soul candles read it too) |
| `lepton:ladder` | climbable ladder: `minecraft:block_face` = facing (away from the wall); breaks when the block behind is gone |
| `lepton:post` | wooden post: axis from `minecraft:block_face`, `lepton:side_<k>` connections (the four directions off the axis), lanterns and hanging signs hang below it; `lepton:strippable` for the axe |
| `lepton:hedge` | hedge connections (`lepton:<dir>` to other `lepton:hedge` blocks), `lepton:extend` |
| `lepton:leaf_carpet` | survival check (non-air below), 20% composting |
| `lepton:hollow_log` | hollow log: axis from `minecraft:block_face`, climbable, auto crawl |
| `lepton:wv_<module>` | which config module a block belongs to (`lepton:wv_vertical_planks`, `..._variant_bookshelves`, `..._variant_ladders`, `..._wooden_posts`, `..._hedges`, `..._leaf_carpet`, `..._hollow_logs`): disabled modules' blocks can't be placed |

## 3. Mechanics

The module `lp::feature::woodvariants::WoodVariantsModule` implements everything below for every
block carrying the tags of §2.5. Neighbour updates come from `BlockChangedEvent` and are batched
per tick (like `game/Shapes`).

### 3.1 Vertical planks
Full blocks; no behaviour.

### 3.2 Bookshelves: enchanting power
Quark: `getEnchantPowerBonus = 1`. The vanilla enchanting table counts `minecraft:bookshelf`
only. Lepton hooks `EnchantUtils::getBookCasePositions` (positions used by the table's particles and
power) to add `lepton:bookshelf` blocks with the Java rule: offsets with |dx| = 2 or |dz| = 2 in
-2..2, dy 0..1, and an air block halfway between. If the game computes the power without that
function, `EnchantUtils::getEnchantCosts` adds the variant bookshelves to the count instead (one
or the other, never both; the total stays capped at 15 by the game).

### 3.3 Ladders
- Placement: only on the side of a block; `minecraft:block_face` (the clicked face) is the facing.
  Placing on the top/bottom face is refused (Java), unless U17's EnhancedLadders allows it.
- Survival: when the block behind (opposite the facing) can't give full support on the face
  towards the ladder (air, liquid, glass pane, fence, ...), the ladder pops off (drops itself).
- Climbing: the ladder's block type gets the game's `Climbable` property, so movement is the
  vanilla ladder's (also for EnhancedLadders, U17).
- Fuel: 300 ticks in furnaces (`FurnaceBlockActor::getBurnDuration`).
- Waterlogging: yes.

### 3.4 Posts
- Placement: the axis is the clicked face's axis.
- Connections (`lepton:side_0..3` for the 4 directions not on the axis; Java has one property per direction, Lepton keeps only the four that can connect to stay at 486 block permutations per post), Java
  `PostSideType.get`: `chain` if the neighbour is a chain along that direction's axis, or (down
  only) a hanging lantern / soul lantern or a ceiling hanging sign; `other_post` if the neighbour is
  a post along that axis, or (up only) a hedge; else `none`. Recomputed on every neighbour change.
- Lanterns / soul lanterns / copper lanterns and ceiling hanging signs can hang below a post (the
  post's block type gets the game's `CanSupportCenterHangingBlock` property).
- Stripping: axe on a post -> `lepton:stripped_<w>_post`, states kept (`game/Shapes`'s
  `lepton:strippable`).
- Shape: the 4x4 beam along the axis (Java adds the connection stubs to the outline; Bedrock has one
  selection box).

### 3.5 Hedges
- Connections: `lepton:<dir>` is true when the neighbour carries `lepton:hedge` (Java
  `connectsTo`: hedges tag only; not fences, walls or solid blocks).
- `lepton:extend`: true when the block below is a hedge.
- Plants: flowers, saplings, short grass and ferns (Java `canSustainPlant` UP, plains plants) can be
  placed on top and survive there: `FlowerBlock::mayPlaceOn` and `FoliageBlock::mayPlaceOn` (the
  latter only for those plant names, since its code is folded) accept a hedge below. The hedges
  also carry the `dirt` block tag for data-driven plants.
- Collision: the hedge shape, max 16 px (Java's fence collision is 24 px; Bedrock caps custom
  collision at one block).

### 3.6 Leaf carpets
- No collision, 1 px selection; breaks instantly; drops itself.
- Replaceable: placing another block into it replaces it (`minecraft:replaceable`).
- Survival: popped off (drops) when the block below becomes air.
- Composting: chance 20% per item (`ComposterBlock::getFillChance`).

### 3.7 Hollow logs
- Placement: axis of the clicked face; 4 walls of 2 px; the inside is open (collision follows the
  walls, so players and mobs can walk/fall through the tube).
- Climbing (`isLadder`): the block type gets the game's `Climbable` property: a player inside a
  hollow log climbs like on a ladder.
- Auto crawl (`enableAutoCrawl`, Quark `HollowLogsModule.playerTick`): each tick, a player that
  starts sneaking while walking ("trying" edge: sneaking and not already crawling) looks in their
  horizontal facing `dir`; tries, in order, crawling out of the block at their feet (towards
  `-dir`), the block above it, then into the block in front and the one above it. A hollow log
  accepts if its axis is `dir`'s axis. The player is put into the crawling pose and moved to
  `(x+0.5 - dir.x*0.3, y+0.13, z+0.5 - dir.z*0.3)`. Logged: `woodvariants: <player> crawled into
  a hollow log`.

### 3.8 Recipes and disabled modules
- `Recipes::_loadRecipe` is hooked at mod load: a `lepton:` recipe whose result belongs to a
  disabled module is skipped (no recipe), so a disabled module's blocks can't be crafted. The
  config is read at world load (changing an `enable*` key takes effect on the next world load).
- The Quark recipe "azalea hedge from oak logs" only exists while U7's AzaleaWood is disabled:
  Lepton reads `config/newwoods.json` `enableAzaleaWood` (missing file/key = disabled).
- Placing a block of a disabled module is cancelled (items already owned stay as items).

### 3.9 Names (`changeNames`)
With the module enabled and `variantBookshelvesChangeNames` / `variantLaddersChangeNames` true,
the client mod renames the vanilla Bookshelf / Ladder to "Oak Bookshelf" / "Oak Ladder"
(`Localization` hook on `tile.bookshelf.name` / `tile.ladder.name`, client build, client config).

## 4. Screens

None. (Quark has no screen in these modules.)

## 5. Config

Section `woodvariants` (`config/woodvariants.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableVerticalPlanks` | bool | true | VerticalPlanks module |
| `enableVariantBookshelves` | bool | true | VariantBookshelves module |
| `variantBookshelvesChangeNames` | bool | true | VariantBookshelves `changeNames`: rename Bookshelf to Oak Bookshelf |
| `enableVariantLadders` | bool | true | VariantLadders module |
| `variantLaddersChangeNames` | bool | true | VariantLadders `changeNames`: rename Ladder to Oak Ladder |
| `enableWoodenPosts` | bool | true | WoodenPosts module |
| `enableHedges` | bool | true | Hedges module |
| `enableLeafCarpet` | bool | true | LeafCarpet module |
| `enableHollowLogs` | bool | true | HollowLogs module |
| `enableAutoCrawl` | bool | true | HollowLogs `enableAutoCrawl` |

## 6. Strings

| Key | en_US |
|---|---|
| `block.quark.oak_bookshelf` | Oak Bookshelf |
| `block.quark.oak_ladder` | Oak Ladder |
| `quark.jei.hint.hollow_logs` | Can be crawled into by sneaking while walking towards the hollow side, can also be climbed when you're inside. |
| `quark.jei.hint.posts` | Can be placed in any axis. Chains and Lanterns can connect to this block. |

(The hints are shown in the item tooltip by the client mod, like Quark's JEI hints are the only
place Quark shows them.)

## 7. Bedrock differences / not ported

- **Advancement** "Crawling in my Bark" (crawl into a hollow log): Bedrock has no custom
  advancements (SPEC §8); the crawl is logged instead.
- **changeNames for vanilla clients**: the rename is done by the client mod; players without it
  see "Bookshelf" / "Ladder".
- **Climbing / crawling on vanilla clients**: player movement is predicted by the client. Without
  the client mod the client doesn't know that variant ladders and hollow logs are climbable or
  that it was put into the crawl pose; the server corrects the position, so climbing is jerky.
- **Hollow log climbing** applies to every axis (the property is per block type); in Java only
  vertical ones are ladders. A player can only be inside a horizontal one while crawling, where
  climbing has no effect.
- **Outline shapes**: one selection box per state (the post's beam, the hedge's bounding box, the
  hollow log's full cube); Java shows the exact outline.
- **Hedge / post collision height** is at most 16 px (Java fences/hedges collide at 24 px).
- **Item tags**: Java puts vertical planks into `#minecraft:planks`; Bedrock block items get no item
  tags, so vertical planks don't work as "any planks" in vanilla recipes.
- **Recipe conditions** are applied at world load from the config (no data-pack conditions).

## 8. Tests

Selftests (`woodvariants.*`, dx 600..699):

| Test | dx | Checks |
|---|---|---|
| `woodvariants.blocks` | - | every block id of §2 is registered |
| `woodvariants.recipes` | - | recipes are loaded for enabled modules |
| `woodvariants.post_axis` | 600 | a post placed on a side face lies along that axis |
| `woodvariants.post_connections` | 604 | chain, other post, hanging lantern, hedge above set the connections |
| `woodvariants.post_strip` | 610 | an axe strips a post keeping its axis |
| `woodvariants.post_lantern` | 614 | a lantern hangs below a post |
| `woodvariants.hedge_connections` | 620 | hedges connect to hedges only, extend above a hedge |
| `woodvariants.hedge_plant` | 626 | a flower can be placed on a hedge |
| `woodvariants.ladder_place` | 630 | ladder facing from the clicked face; refused on top faces; pops off when its wall is removed |
| `woodvariants.ladder_climb` | 638 | the ladder's block type is climbable |
| `woodvariants.carpet_survive` | 642 | a leaf carpet pops off over air |
| `woodvariants.carpet_compost` | 646 | composter fill chance 20 |
| `woodvariants.bookshelf_power` | 650 | variant bookshelves around a table are counted |
| `woodvariants.hollow_axis` | 660 | hollow log axis + climbable |
| `woodvariants.hollow_crawl` | 664 | sneaking towards a horizontal hollow log crawls into it |
| `woodvariants.fuel` | - | variant ladders burn 300 ticks |
| `woodvariants.gallery` | 670..699 (rows stacked up to +18) | builds a gallery of every block and checks its connections; with a real player, shows each row and waits for the player to crawl into a hollow log |

E2E: `tools/e2e/server/scenarios/woodvariants.ps1` runs `woodvariants.*` and checks the pass
count. `tools/e2e/client/scenarios/woodvariants.ps1` runs the gallery (vertical planks,
bookshelves, ladders on a wall, posts with chains and lanterns, stripped posts, connected hedges
with extended ones, a post under a hedge, leaf carpets, hollow logs on each axis), saves a
screenshot of each row, then holds sneak in front of a horizontal hollow log and checks that the
player crawled into it.
