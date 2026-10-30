# Pack generators (`tools/pack`)

`lib.ps1` writes the behavior and resource pack JSON of blocks, shapes and wood sets. A unit calls
it from `tools/pack/<unit>.ps1` and **commits the generated output**. The native side of the
shapes (stairs corners, connections, doors, ...) is `src/game/Shapes.cpp`: it works for every block
the generator writes, so units need no code of their own for them.

```powershell
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/lib.ps1"          # if the script also draws textures
Start-PackUnit 'stones'                        # deletes what the last run of this unit wrote
New-ShapeSet -Material 'polished_calcite' -Shapes block, stairs, slab, vertical_slab, wall `
    -Textures 'textures/blocks/polished_calcite' -Name @{ en = 'Polished Calcite'; zh = '磨制方解石' } `
    -Hardness 1.5 -Resistance 6 -MapColor '#dfe0dc' -Tool pickaxe -Stonecutter
New-ShapeSet -Material 'stone_bricks' -Prefix 'stone_brick' -BaseBlock 'minecraft:stone_bricks' `
    -Slab 'minecraft:stone_brick_slab' -Shapes vertical_slab -Textures 'textures/blocks/stonebrick' `
    -Name @{ en = 'Stone Brick'; zh = '石砖' } -Tool pickaxe -Stonecutter
Complete-PackUnit                              # writes the fragments and tools/pack/generated/<unit>.txt
```

`tools/pack/u0.ps1` (test-only probe blocks `lepton:u0_probe_*`, hidden from the creative inventory
and commands) is a complete example.

## What gets written

| File | Owner |
| ---- | ----- |
| `bp/blocks/<path>.json`, `bp/items/<path>.json` (doors) | the unit |
| `bp/loot_tables/blocks/<path>.json` (+ `<path>_double.json` for slabs) | the unit |
| `bp/recipes/<name>.json` (crafting with `unlock`, stonecutter) | the unit |
| `rp/blocks.d/<unit>-gen.json` (sounds), `rp/terrain_texture.d/<unit>-gen.json`, `rp/item_texture.d/<unit>-gen.json` | the unit |
| `rp/texts.d/{en_US,zh_CN}/<unit>-gen.lang` (`tile.<id>.name`, `item.<id>.name`) | the unit |
| `rp/models/blocks/lepton_<shape>.geo.json` | shared, identical for everyone (committed by U0) |
| `tools/pack/generated/<unit>.txt` | list of the unit's generated files (for `Start-PackUnit`) |

Fragments are named `<unit>-gen` so a unit can still keep hand-written `<unit>.json` fragments.
Terrain/item texture keys are `lepton_<unit>_<texture file>`, so two units can point at the same
(vanilla) texture without a duplicate key. Output is deterministic: stable key order, LF, no
timestamps. Block JSON uses `format_version` 1.26.0 (needed for `minecraft:connection_rule`;
`minecraft:redstone_producer` needs 1.21.120+).

## Functions

| Function | Writes |
| -------- | ------ |
| `Start-PackUnit <unit>` / `Complete-PackUnit` | session around everything below |
| `New-ShapeSet` | a material's shapes, see parameters below; returns `@{ <shape> = <id> }` |
| `New-WoodSet -Wood -Name -Textures [-Parts] [-LogWord stem -WoodWord hyphae] [-NonFlammable]` | log, wood, stripped log/wood, planks, stairs, slab, vertical slab, fence, fence gate, door, trapdoor, button, pressure plate (vanilla ids `<wood>_log`, `stripped_<wood>_log`, `<wood>_planks`, ...), with vanilla recipes; returns part -> id |
| `New-FullBlock -Id -Textures -Name [-Axis] [-Loot] ...` | one cube block; `-Axis` makes a pillar (logs) |
| `New-DoorBlock`, `New-TrapdoorBlock`, `New-RedstoneInputBlock -Kind button|pressure_plate` | single pieces (iron-like: `-RedstoneOnly`, stone plates: `-MobsOnly`, `-PressTicks 20|30`) |
| `Write-ShapedRecipe`, `Write-ShapelessRecipe`, `Write-LootTable`, `Add-PackText`, `Add-TerrainTexture`, `Add-ItemTexture`, `Add-PackFile`, `Write-PackJson`, `New-Box`, `Write-Geometry` | building blocks for unit-specific blocks (ladders, bookshelves, posts, ...) |

`New-ShapeSet` parameters: `-Material` (path of the full block), `-Prefix` (shape id prefix,
default `-Material`), `-Shapes block,stairs,slab,vertical_slab,wall,fence,fence_gate,pane`,
`-Textures` (`'textures/blocks/x'` or `@{ side; top; bottom }`), `-Name @{ en; zh }` (full block;
shapes append " Stairs"/"楼梯" ...), `-Names @{ <shape> = @{ en; zh } }`, `-Ids @{ <shape> = 'lepton:x' }`,
`-BaseBlock` (vanilla id = shapes only, e.g. U1's vertical slabs), `-Slab` (slab item for the
vertical slab recipes), `-Sound` (blocks.json sound: stone, wood, ...), `-Hardness`, `-Resistance`,
`-MapColor`, `-Flammable`, `-Tool pickaxe|axe|shovel|hoe` (adds the `minecraft:is_<tool>_item_destructible`
tag), `-Tags`, `-Stonecutter`, `-NoCrafting`, `-FenceStick` (default `minecraft:stick`; nether
brick fences: `minecraft:netherbrick`), `-RenderMethod` (opaque, alpha_test, blend), `-Category`,
`-Hidden`.

Recipes follow vanilla counts: 6 blocks -> 4 stairs, 3 -> 6 slabs, 6 -> 6 walls, 4 planks + 2
sticks -> 3 fences, 2 + 4 sticks -> 1 gate, 6 -> 16 panes, 6 planks -> 3 doors / 2 trapdoors; Quark's
vertical slabs: 3 slabs in a column -> 3 vertical slabs, 1 vertical slab -> 1 slab; stonecutter 1
block -> 1 stairs / 2 slabs / 2 vertical slabs / 1 wall.

## Block contract (read by `src/game/Shapes.cpp`)

| Tag | States |
| --- | ------ |
| `lepton:stairs` | `minecraft:cardinal_direction` (trait, the high side = the player's look direction), `minecraft:vertical_half` (trait), `lepton:shape` straight / inner_left / inner_right / outer_left / outer_right |
| `lepton:slab` | `minecraft:vertical_half` (trait), `lepton:double` |
| `lepton:vertical_slab` | `lepton:type` north / south / west / east / double (Quark's VerticalSlabType: the open side) |
| `lepton:wall` | `lepton:north/east/south/west` none / short / tall, `lepton:post` |
| `lepton:fence`, `lepton:pane` | `lepton:north/east/south/west` (bool) |
| `lepton:fence_gate` | `minecraft:cardinal_direction` (trait), `lepton:open`, `lepton:in_wall` |
| `lepton:door` | `minecraft:cardinal_direction` (trait), `lepton:open`, `lepton:upper`, `lepton:hinge_right` |
| `lepton:trapdoor` | `minecraft:cardinal_direction` (trait), `minecraft:vertical_half` (trait), `lepton:open` |
| `lepton:button`, `lepton:pressure_plate` | `lepton:pressed` (+ `minecraft:block_face` trait for buttons); tags `lepton:press_ticks_<n>`, `lepton:mobs_only` |
| `lepton:pillar` | `minecraft:block_face` (trait; the model is rotated along the clicked axis) |
| `lepton:strippable` | an axe turns `lepton:<x>` into `lepton:stripped_<x>`, keeping the states |
| `lepton:wooden`, `lepton:redstone_only` | wooden fences connect only to wooden fences; redstone-only doors ignore clicks |

Geometry uses bone visibility per state and world-aligned ("uv locked") face UVs, so textures line
up like vanilla. Collision and selection boxes are set per permutation (stairs: several boxes).

## Not possible on Bedrock (documented gaps)

- **Signs, hanging signs**: need a block entity (text); custom blocks cannot have one. Not generated.
- **Boats, chest boats**: entities with vanilla-only renderers; not generated (a unit can add a
  custom entity if it needs one).
- Custom collision boxes stay within the block (max 16 px high): fences and walls are not 1.5
  blocks high for collision as in Java (players can jump over them).
- Redstone: pressed buttons/plates emit power through `minecraft:redstone_producer`. Doors,
  trapdoors and gates open from redstone sources directly next to them (wire, torches, levers,
  buttons, plates, blocks of redstone, repeaters/comparators, observers); power that reaches them
  through another block is not seen.
- Vanilla walls/panes/fences connect to ours through `minecraft:connection_rule`; ours connect to
  vanilla ones natively.
