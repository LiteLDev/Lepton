# U4 bricks: MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori, DuskboundBlocks (Quark 4.1-488).
# Writes the blocks, items, recipes, loot tables and fragments, draws the textures
# (tools/textures/bricks.ps1) and generates src/feature/bricks/Content.gen.inc (the content table the
# native side uses to gate recipes and creative entries by config).
#
#   pwsh -NoProfile -File tools/pack/bricks.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"

Start-PackUnit 'bricks'

foreach ($p in (& "$PSScriptRoot/../textures/bricks.ps1" -Root $script:RepoRoot)) { $null = Add-PackFile $p }

# content group of every block/item id (see docs/spec/bricks.md §5), filled as blocks are written
$script:GroupOf = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
$script:CurrentGroup = $null
function Set-Group([string]$Group) { $script:CurrentGroup = $Group }
function Update-Groups {
    foreach ($id in $script:Pack.Blocks.Keys) {
        if (-not $script:GroupOf.ContainsKey($id)) { $script:GroupOf[$id] = $script:CurrentGroup }
    }
}

$suffix = @{ stairs = @(' Stairs', '楼梯'); slab = @(' Slab', '台阶'); vertical_slab = @(' Vertical Slab', '竖直台阶'); wall = @(' Wall', '墙') }
# A Quark brick-style family: the full block plus shapes, shape names from a (singular) shape stem.
function Add-Family {
    param([string]$Material, [hashtable]$Name, [hashtable]$Stem, [string[]]$Shapes, $Textures, [hashtable]$Props, [switch]$Stonecutter, [string[]]$Tags = @())
    $names = @{}
    foreach ($s in $Shapes) { if ($suffix[$s]) { $names[$s] = @{ en = $Stem.en + $suffix[$s][0]; zh = $Stem.zh + $suffix[$s][1] } } }
    $null = New-ShapeSet -Material $Material -Shapes $Shapes -Textures $Textures -Name $Name -Names $names -Tags (@($Tags) + $rt) -Stonecutter:$Stonecutter @Props
    Update-Groups
}

function P([double]$H, [double]$R, [string]$Sound, [string]$Map, [string]$Tool = 'pickaxe') { @{ Hardness = $H; Resistance = $R; Sound = $Sound; MapColor = $Map; Tool = $Tool } }
$all4 = @('block', 'stairs', 'slab', 'vertical_slab', 'wall')
$soulTags = @('lepton:soul_fire_base', 'lepton:soul_speed')
# requiresCorrectToolForDrops (every block of the unit): drops only with the block's tool, native side
$rt = 'lepton:requires_tool'

# ---------------------------------------------------------------------------------------------
# MoreBrickTypes
# ---------------------------------------------------------------------------------------------
Set-Group 'blue_nether_bricks'
Add-Family 'blue_nether_bricks' @{ en = 'Blue Nether Bricks'; zh = '蓝色下界砖' } @{ en = 'Blue Nether Brick'; zh = '蓝色下界砖' } $all4 'textures/blocks/blue_nether_bricks' (P 2 6 'nether_brick' '#700200') -Stonecutter
Write-ShapelessRecipe 'blue_nether_bricks' @('minecraft:nether_brick', 'minecraft:warped_wart_block') 'lepton:blue_nether_bricks' 2

Set-Group 'sandstone_bricks'
Add-Family 'sandstone_bricks' @{ en = 'Sandstone Bricks'; zh = '砂岩砖' } @{ en = 'Sandstone Brick'; zh = '砂岩砖' } $all4 'textures/blocks/sandstone_bricks' (P 0.8 0.8 'stone' '#f7e9a3') -Stonecutter
Write-ShapedRecipe 'sandstone_bricks' @('##', '##') @{ '#' = 'minecraft:cut_sandstone' } 'lepton:sandstone_bricks' 4
Write-ShapelessRecipe 'sandstone_bricks_stonecutter' @('minecraft:sandstone') 'lepton:sandstone_bricks' 1 'stonecutter'
Write-ShapelessRecipe 'sandstone_bricks_cut_stonecutter' @('minecraft:cut_sandstone') 'lepton:sandstone_bricks' 1 'stonecutter'

Set-Group 'red_sandstone_bricks'
Add-Family 'red_sandstone_bricks' @{ en = 'Red Sandstone Bricks'; zh = '红砂岩砖' } @{ en = 'Red Sandstone Brick'; zh = '红砂岩砖' } $all4 'textures/blocks/red_sandstone_bricks' (P 0.8 0.8 'stone' '#d87f33') -Stonecutter
Write-ShapedRecipe 'red_sandstone_bricks' @('##', '##') @{ '#' = 'minecraft:cut_red_sandstone' } 'lepton:red_sandstone_bricks' 4
Write-ShapelessRecipe 'red_sandstone_bricks_stonecutter' @('minecraft:red_sandstone') 'lepton:red_sandstone_bricks' 1 'stonecutter'
Write-ShapelessRecipe 'red_sandstone_bricks_cut_stonecutter' @('minecraft:cut_red_sandstone') 'lepton:red_sandstone_bricks' 1 'stonecutter'

Set-Group 'soul_sandstone_bricks'
Add-Family 'soul_sandstone_bricks' @{ en = 'Soul Sandstone Bricks'; zh = '灵魂砂岩砖' } @{ en = 'Soul Sandstone Brick'; zh = '灵魂砂岩砖' } $all4 'textures/blocks/soul_sandstone_bricks' (P 0.8 0.8 'stone' '#f7e9a3') -Stonecutter -Tags $soulTags
Write-ShapedRecipe 'soul_sandstone_bricks' @('##', '##') @{ '#' = 'lepton:cut_soul_sandstone' } 'lepton:soul_sandstone_bricks' 4
Write-ShapelessRecipe 'soul_sandstone_bricks_stonecutter' @('lepton:soul_sandstone') 'lepton:soul_sandstone_bricks' 1 'stonecutter'
Write-ShapelessRecipe 'soul_sandstone_bricks_cut_stonecutter' @('lepton:cut_soul_sandstone') 'lepton:soul_sandstone_bricks' 1 'stonecutter'

Set-Group 'cobblestone_bricks'
Add-Family 'cobblestone_bricks' @{ en = 'Cobblestone Bricks'; zh = '圆石砖' } @{ en = 'Cobblestone Brick'; zh = '圆石砖' } $all4 'textures/blocks/cobblestone_bricks' (P 2 6 'stone' '#707070') -Stonecutter
Write-ShapedRecipe 'cobblestone_bricks' @('##', '##') @{ '#' = 'minecraft:cobblestone' } 'lepton:cobblestone_bricks' 4
Write-ShapelessRecipe 'cobblestone_bricks_stonecutter' @('minecraft:cobblestone') 'lepton:cobblestone_bricks' 1 'stonecutter'

Set-Group 'mossy_cobblestone_bricks'
Add-Family 'mossy_cobblestone_bricks' @{ en = 'Mossy Cobblestone Bricks'; zh = '苔圆石砖' } @{ en = 'Mossy Cobblestone Brick'; zh = '苔圆石砖' } $all4 'textures/blocks/mossy_cobblestone_bricks' (P 2 6 'stone' '#707070') -Stonecutter
Write-ShapedRecipe 'mossy_cobblestone_bricks' @('##', '##') @{ '#' = 'minecraft:mossy_cobblestone' } 'lepton:mossy_cobblestone_bricks' 4
Write-ShapelessRecipe 'mossy_cobblestone_bricks_from_moss_block' @('lepton:cobblestone_bricks', 'minecraft:moss_block') 'lepton:mossy_cobblestone_bricks' 1
Write-ShapelessRecipe 'mossy_cobblestone_bricks_from_vine' @('lepton:cobblestone_bricks', 'minecraft:vine') 'lepton:mossy_cobblestone_bricks' 1
Write-ShapelessRecipe 'mossy_cobblestone_bricks_stonecutter' @('minecraft:mossy_cobblestone') 'lepton:mossy_cobblestone_bricks' 1 'stonecutter'

Set-Group 'blackstone_bricks'
Add-Family 'blackstone_bricks' @{ en = 'Blackstone Bricks'; zh = '黑石砖' } @{ en = 'Blackstone Brick'; zh = '黑石砖' } $all4 'textures/blocks/blackstone_bricks' (P 1.5 6 'stone' '#191919') -Stonecutter
Write-ShapedRecipe 'blackstone_bricks' @('C#', '##') @{ '#' = 'minecraft:blackstone'; 'C' = 'lepton:cobblestone_bricks' } 'lepton:blackstone_bricks' 4

# dirt bricks: shovel, gravel sounds, no stonecutter recipes (as in Quark)
Set-Group 'dirt_bricks'
Add-Family 'dirt_bricks' @{ en = 'Dirt Bricks'; zh = '泥土砖' } @{ en = 'Dirt Brick'; zh = '泥土砖' } $all4 'textures/blocks/dirt_bricks' (P 0.5 0.5 'gravel' '#976d4d' 'shovel')
Write-ShapedRecipe 'dirt_bricks' @('C#', '##') @{ '#' = 'minecraft:dirt'; 'C' = 'lepton:cobblestone_bricks' } 'lepton:dirt_bricks' 4

Set-Group 'netherrack_bricks'
Add-Family 'netherrack_bricks' @{ en = 'Netherrack Bricks'; zh = '下界岩砖' } @{ en = 'Netherrack Brick'; zh = '下界岩砖' } $all4 'textures/blocks/netherrack_bricks' (P 0.4 0.4 'netherrack' '#700200') -Stonecutter -Tags @('lepton:infiniburn')
Write-ShapedRecipe 'netherrack_bricks' @('C#', '##') @{ '#' = 'minecraft:netherrack'; 'C' = 'lepton:cobblestone_bricks' } 'lepton:netherrack_bricks' 4

# ---------------------------------------------------------------------------------------------
# SoulSandstone
# ---------------------------------------------------------------------------------------------
Set-Group 'soul_sandstone'
$soulProps = P 0.8 0.8 'stone' '#664c33'
$soulTex = @{ side = 'textures/blocks/soul_sandstone'; top = 'textures/blocks/soul_sandstone_top'; bottom = 'textures/blocks/soul_sandstone_bottom' }
Add-Family 'soul_sandstone' @{ en = 'Soul Sandstone'; zh = '灵魂砂岩' } @{ en = 'Soul Sandstone'; zh = '灵魂砂岩' } $all4 $soulTex $soulProps -Stonecutter -Tags $soulTags
Add-Family 'cut_soul_sandstone' @{ en = 'Cut Soul Sandstone'; zh = '切制灵魂砂岩' } @{ en = 'Cut Soul Sandstone'; zh = '切制灵魂砂岩' } @('block', 'slab', 'vertical_slab') `
    @{ side = 'textures/blocks/cut_soul_sandstone'; top = 'textures/blocks/soul_sandstone_top' } $soulProps -Stonecutter -Tags $soulTags
Add-Family 'smooth_soul_sandstone' @{ en = 'Smooth Soul Sandstone'; zh = '平滑灵魂砂岩' } @{ en = 'Smooth Soul Sandstone'; zh = '平滑灵魂砂岩' } @('block', 'stairs', 'slab', 'vertical_slab') `
    'textures/blocks/smooth_soul_sandstone' $soulProps -Stonecutter -Tags $soulTags
New-FullBlock -Id 'lepton:chiseled_soul_sandstone' -Name @{ en = 'Chiseled Soul Sandstone'; zh = '雕纹灵魂砂岩' } `
    -Textures @{ side = 'textures/blocks/chiseled_soul_sandstone'; top = 'textures/blocks/soul_sandstone_top' } -Tags (@($soulTags) + $rt) @soulProps
Update-Groups
Write-ShapedRecipe 'soul_sandstone' @('##', '##') @{ '#' = 'minecraft:soul_sand' } 'lepton:soul_sandstone' 1
Write-ShapedRecipe 'chiseled_soul_sandstone' @('#', '#') @{ '#' = 'lepton:soul_sandstone' } 'lepton:chiseled_soul_sandstone' 1
Write-ShapedRecipe 'cut_soul_sandstone' @('##', '##') @{ '#' = 'lepton:soul_sandstone' } 'lepton:cut_soul_sandstone' 1
Write-ShapelessRecipe 'chiseled_soul_sandstone_stonecutter' @('lepton:soul_sandstone') 'lepton:chiseled_soul_sandstone' 1 'stonecutter'
Write-ShapelessRecipe 'cut_soul_sandstone_stonecutter' @('lepton:soul_sandstone') 'lepton:cut_soul_sandstone' 1 'stonecutter'
Write-ShapelessRecipe 'cut_soul_sandstone_slab_stonecutter2' @('lepton:soul_sandstone') 'lepton:cut_soul_sandstone_slab' 2 'stonecutter'
Write-ShapelessRecipe 'cut_soul_sandstone_vertical_slab_stonecutter2' @('lepton:soul_sandstone') 'lepton:cut_soul_sandstone_vertical_slab' 2 'stonecutter'

function Write-FurnaceRecipe([string]$Name, [string]$In, [string]$Out) {
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version            = $script:RecipeFormat
            'minecraft:recipe_furnace' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @('furnace')
                input       = $In
                output      = $Out
            }
        })
}
Write-FurnaceRecipe 'smooth_soul_sandstone' 'lepton:soul_sandstone' 'lepton:smooth_soul_sandstone'

# ---------------------------------------------------------------------------------------------
# MoreMudBlocks
# ---------------------------------------------------------------------------------------------
Set-Group 'mud'
$mudProps = P 1.5 3 'mud_bricks' '#876b62'
New-FullBlock -Id 'lepton:carved_mud_bricks' -Name @{ en = 'Carved Mud Bricks'; zh = '刻制泥砖' } -Textures 'textures/blocks/carved_mud_bricks' -Tags $rt @mudProps
New-FullBlock -Id 'lepton:mud_pillar' -Name @{ en = 'Mud Pillar'; zh = '泥柱' } -Axis `
    -Textures @{ side = 'textures/blocks/mud_pillar'; top = 'textures/blocks/mud_pillar_top' } -Tags $rt @mudProps

# lattice: a glass-like cutout cube that can hold water (Quark's MudBrickLatticeBlock)
$lc = Get-BaseComponents @{ Hardness = 1.5; Resistance = 3; MapColor = '#876b62'; Tool = 'pickaxe'; Tags = @('lepton:lattice', $rt) }
$lc['minecraft:geometry'] = 'minecraft:geometry.full_block'
$lc['minecraft:material_instances'] = Get-MaterialInstances 'textures/blocks/mud_brick_lattice' 'alpha_test'
$lc['minecraft:light_dampening'] = 0
$lc['minecraft:liquid_detection'] = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
$lc['minecraft:loot'] = Write-LootTable 'mud_brick_lattice' 'lepton:mud_brick_lattice'
Write-BlockJson -Id 'lepton:mud_brick_lattice' -Menu ([ordered]@{ category = 'construction' }) -Components $lc
Register-BlockClient 'lepton:mud_brick_lattice' 'mud_bricks' @{ en = 'Mud Brick Lattice'; zh = '泥砖格栅' }
Update-Groups
Write-ShapedRecipe 'carved_mud_bricks' @('##', '##') @{ '#' = 'minecraft:mud_brick_slab' } 'lepton:carved_mud_bricks' 2
Write-ShapedRecipe 'mud_pillar' @('#', '#') @{ '#' = 'minecraft:mud_brick_slab' } 'lepton:mud_pillar' 1
Write-ShapedRecipe 'mud_brick_lattice' @(' # ', '# #', ' # ') @{ '#' = 'minecraft:mud_bricks' } 'lepton:mud_brick_lattice' 4
Write-ShapelessRecipe 'carved_mud_bricks_stonecutter' @('minecraft:mud_bricks') 'lepton:carved_mud_bricks' 1 'stonecutter'
Write-ShapelessRecipe 'mud_pillar_stonecutter' @('minecraft:mud_bricks') 'lepton:mud_pillar' 1 'stonecutter'
Write-ShapelessRecipe 'mud_brick_lattice_stonecutter' @('minecraft:mud_bricks') 'lepton:mud_brick_lattice' 1 'stonecutter'

# ---------------------------------------------------------------------------------------------
# Midori
# ---------------------------------------------------------------------------------------------
Set-Group 'midori'
$midoriProps = P 1.5 6 'stone' '#7fcc19'
Add-Family 'midori_block' @{ en = 'Midori Block'; zh = '绿块' } @{ en = 'Midori'; zh = '绿' } @('block', 'stairs', 'slab', 'vertical_slab') 'textures/blocks/midori_block' $midoriProps -Stonecutter
New-FullBlock -Id 'lepton:midori_pillar' -Name @{ en = 'Midori Pillar'; zh = '绿柱' } -Axis `
    -Textures @{ side = 'textures/blocks/midori_pillar'; top = 'textures/blocks/midori_pillar_top' } -Tags $rt @midoriProps
Update-Groups
Write-ShapedRecipe 'midori_block' @('##', '##') @{ '#' = 'lepton:moss_paste' } 'lepton:midori_block' 4
Write-ShapedRecipe 'midori_pillar' @('#', '#') @{ '#' = 'lepton:midori_block_slab' } 'lepton:midori_pillar' 1
Write-ShapelessRecipe 'midori_pillar_stonecutter' @('lepton:midori_block') 'lepton:midori_pillar' 1 'stonecutter'
Write-FurnaceRecipe 'moss_paste' 'minecraft:moss_block' 'lepton:moss_paste'
# moss paste: an ingredient, composts with a 50% chance (Quark's CompostManager.addChance 0.5)
$icon = Add-ItemTexture 'textures/items/moss_paste'
Write-PackJson (Join-Path $script:BP 'items/moss_paste.json') ([ordered]@{
        format_version  = $script:ItemFormat
        'minecraft:item' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:moss_paste'; menu_category = [ordered]@{ category = 'items' } }
            components  = [ordered]@{
                'minecraft:icon'            = $icon
                'minecraft:max_stack_size'  = 64
                'minecraft:compostable'     = [ordered]@{ composting_chance = 50 }
            }
        }
    })
Add-PackText 'item.lepton:moss_paste' @{ en = 'Moss Paste'; zh = '苔藓泥' }
$script:GroupOf['lepton:moss_paste'] = 'midori'

# ---------------------------------------------------------------------------------------------
# DuskboundBlocks
# ---------------------------------------------------------------------------------------------
Set-Group 'duskbound'
$duskProps = P 1.5 6 'stone' '#b24cd8'
Add-Family 'duskbound_block' @{ en = 'Duskbound Block'; zh = '黯缚块' } @{ en = 'Duskbound'; zh = '黯缚' } @('block', 'stairs', 'slab', 'vertical_slab') 'textures/blocks/duskbound_block' $duskProps -Stonecutter -Tags @('lepton:dragon_immune')
# lantern: light level 15
$dl = Get-BaseComponents @{ Hardness = 1.5; Resistance = 6; MapColor = '#b24cd8'; Tool = 'pickaxe'; Tags = @('lepton:dragon_immune', $rt) }
$dl['minecraft:geometry'] = 'minecraft:geometry.full_block'
$dl['minecraft:material_instances'] = Get-MaterialInstances 'textures/blocks/duskbound_lantern'
$dl['minecraft:light_emission'] = 15
$dl['minecraft:loot'] = Write-LootTable 'duskbound_lantern' 'lepton:duskbound_lantern'
Write-BlockJson -Id 'lepton:duskbound_lantern' -Menu ([ordered]@{ category = 'construction' }) -Components $dl
Register-BlockClient 'lepton:duskbound_lantern' 'stone' @{ en = 'Duskbound Lantern'; zh = '黯缚灯' }
Update-Groups
$duskKey = @{ 'P' = 'minecraft:purpur_block'; 'O' = 'lepton:dusky_myalite' }
Write-ShapedRecipe 'duskbound_block' @('PPP', 'POP', 'PPP') $duskKey 'lepton:duskbound_block' 16
Write-ShapedRecipe 'duskbound_block_without_myalite' @('PPP', 'POP', 'PPP') @{ 'P' = 'minecraft:purpur_block'; 'O' = 'minecraft:obsidian' } 'lepton:duskbound_block' 16
Write-ShapedRecipe 'duskbound_lantern' @('DDD', 'DED', 'DDD') @{ 'D' = 'lepton:duskbound_block'; 'E' = 'minecraft:ender_pearl' } 'lepton:duskbound_lantern' 4

# ---------------------------------------------------------------------------------------------
# Content table for src/feature/bricks: every item -> group, every recipe -> result item.
# ---------------------------------------------------------------------------------------------
$lines = @('// Generated by tools/pack/bricks.ps1. Do not edit.', '// clang-format off', '#define LP_BRICKS_ITEMS \')
foreach ($kv in $script:GroupOf.GetEnumerator()) { $lines += "    X(`"$((Get-Path $kv.Key))`", `"$($kv.Value)`") \" }
$lines += '' ; $lines += '#define LP_BRICKS_RECIPES \'
foreach ($rel in @($script:Pack.Files)) {
    if ($rel -notmatch '^assets/pack/bp/recipes/(.+)\.json$') { continue }
    $name = $Matches[1]
    $j = Get-Content (Join-Path $script:RepoRoot $rel) -Raw | ConvertFrom-Json
    $body = $j.PSObject.Properties | Where-Object { $_.Name -like 'minecraft:recipe_*' } | Select-Object -First 1
    $out = if ($body.Value.result) { $body.Value.result.item } else { $body.Value.output }
    $lines += "    X(`"$name`", `"$(Get-Path $out)`") \"
}
$lines += ''; $lines += '// clang-format on'
Write-PackText (Join-Path $script:RepoRoot 'src/feature/bricks/Content.gen.inc') ($lines -join "`n")

Complete-PackUnit
