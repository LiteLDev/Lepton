# U5 palette: Shingles, FramedGlass, GoldBars, IndustrialPalette, JapanesePalette,
# NetherBrickFenceGate, Thatch, Grate (docs/spec/palette.md).
#
#   pwsh -NoProfile -File tools/pack/palette.ps1      # regenerates and overwrites the committed output
#
# Also writes src/feature/palette/PaletteIds.inc (every block and recipe id of the unit with the
# config part that enables it), read by src/feature/palette/Palette.cpp.
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/palette.ps1"

Start-PackUnit 'palette'

# ---- textures ------------------------------------------------------------------------------------
$texDir = Join-Path $script:RP 'textures/blocks/palette'
Write-PaletteTextures -Dir $texDir
foreach ($f in Get-ChildItem $texDir -Filter *.png | Sort-Object Name) { $null = Add-PackFile $f.FullName }
function T([string]$Name) { return "textures/blocks/palette/$Name" }

# ---- id tracking (block / recipe id -> config part) ---------------------------------------------
$script:PartOf = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
$script:Seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
function Set-Part([string]$Part) {
    foreach ($rel in $script:Pack.Files) {
        if (-not $script:Seen.Add($rel)) { continue }
        if ($rel -match '^assets/pack/bp/blocks/(.+)\.json$') { $script:PartOf["lepton:$($Matches[1])"] = $Part }
        elseif ($rel -match '^assets/pack/bp/recipes/(.+)\.json$') { $script:PartOf["lepton:$($Matches[1])"] = $Part }
    }
}
Set-Part 'None'   # textures

# Reads bp/blocks/<path>.json, lets $Fn change the components (ordered hashtable), writes it back.
function Update-BlockComponents([string]$Id, [scriptblock]$Fn) {
    $path = Join-Path $script:BP "blocks/$(Get-Path $Id).json"
    $j = Get-Content -Raw $path | ConvertFrom-Json -AsHashtable
    & $Fn $j['minecraft:block']['components'] $j['minecraft:block']
    Write-PackJson $path $j
}
function Set-Flammable([string]$Id, [int]$Catch, [int]$Destroy) {
    Update-BlockComponents $Id { param($c) $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = $Catch; destroy_chance_modifier = $Destroy } }
}

# Geometry with named material instances per cube: $Cubes = list of @{ box; faces = @{ up = 'top'; '*' = 'side' } }.
function Write-PaletteGeometry([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) {
        $cubes = foreach ($cube in $kv.Value) {
            $c = New-GeoCube $cube.Box
            foreach ($f in @($c.uv.Keys)) {
                $mi = if ($cube.Faces -and $cube.Faces[$f]) { $cube.Faces[$f] } elseif ($cube.Faces -and $cube.Faces['*']) { $cube.Faces['*'] } else { $f }
                $c.uv[$f].material_instance = $mi
            }
            $c
        }
        [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($cubes) }
    }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = "geometry.lepton.$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($bonesJson)
            })
    }
    Write-PackJson -Path (Add-PackFile (Join-Path $script:RP "models/blocks/lepton_$Name.geo.json")) -Object $geo
    return "geometry.lepton.$Name"
}

function New-Mi([string]$Texture, [string]$Render = 'opaque') { return [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = $Render } }

$colors = [ordered]@{
    white = @('White', '白色'); orange = @('Orange', '橙色'); magenta = @('Magenta', '品红色'); light_blue = @('Light Blue', '淡蓝色')
    yellow = @('Yellow', '黄色'); lime = @('Lime', '黄绿色'); pink = @('Pink', '粉红色'); gray = @('Gray', '灰色')
    light_gray = @('Light Gray', '淡灰色'); cyan = @('Cyan', '青色'); purple = @('Purple', '紫色'); blue = @('Blue', '蓝色')
    brown = @('Brown', '棕色'); green = @('Green', '绿色'); red = @('Red', '红色'); black = @('Black', '黑色')
}

# ---- Shingles ------------------------------------------------------------------------------------
$shingleSets = [ordered]@{ '' = @('', '') }
foreach ($kv in $colors.GetEnumerator()) { $shingleSets[$kv.Key] = $kv.Value }
foreach ($kv in $shingleSets.GetEnumerator()) {
    $c = $kv.Key
    $pre = if ($c) { "${c}_" } else { '' }
    $en = if ($c) { "$($kv.Value[0]) " } else { '' }
    $zh = $kv.Value[1]
    $material = "${pre}shingles"
    $terracotta = if ($c) { "minecraft:${c}_terracotta" } else { 'minecraft:hardened_clay' }
    $ids = New-ShapeSet -Material $material -Shapes block, stairs, slab, vertical_slab -Textures (T $material) `
        -Name @{ en = "${en}Terracotta Shingles"; zh = "${zh}陶瓦砖瓦" } -Names @{
        stairs        = @{ en = "${en}Terracotta Shingle Stairs"; zh = "${zh}陶瓦砖瓦楼梯" }
        slab          = @{ en = "${en}Terracotta Shingle Slab"; zh = "${zh}陶瓦砖瓦台阶" }
        vertical_slab = @{ en = "${en}Terracotta Shingle Vertical Slab"; zh = "${zh}陶瓦砖瓦竖直台阶" }
    } -Hardness 1.25 -Resistance 4.2 -MapColor $script:PaletteTerracotta[$c] -Tool pickaxe -Tags 'lepton:shingles' -Stonecutter
    Write-ShapedRecipe $material @('##') @{ '#' = $terracotta } $ids.block 2
    if ($c) { Write-ShapedRecipe "${material}_dye" @('SSS', 'SDS', 'SSS') @{ S = 'lepton:shingles'; D = "minecraft:${c}_dye" } $ids.block 8 }
    $tp = Get-Path $terracotta
    Write-ShapelessRecipe "${material}_from_${tp}_stonecutting" @($terracotta) $ids.block 1 'stonecutter'
    Write-ShapelessRecipe "${material}_stairs_from_${tp}_stonecutting" @($terracotta) $ids.stairs 1 'stonecutter'
    Write-ShapelessRecipe "${material}_slab_from_${tp}_stonecutting" @($terracotta) $ids.slab 2 'stonecutter'
    Write-ShapelessRecipe "${material}_vertical_slab_from_${tp}_stonecutting" @($terracotta) $ids.vertical_slab 2 'stonecutter'
}
Set-Part 'Shingles'

# ---- FramedGlass ---------------------------------------------------------------------------------
$glassSets = [ordered]@{ '' = @('', '') }
foreach ($kv in $colors.GetEnumerator()) { $glassSets[$kv.Key] = $kv.Value }
foreach ($kv in $glassSets.GetEnumerator()) {
    $c = $kv.Key
    $pre = if ($c) { "${c}_" } else { '' }
    $en = if ($c) { "$($kv.Value[0]) " } else { '' }
    $material = "${pre}framed_glass"
    $render = if ($c) { 'blend' } else { 'alpha_test' }
    $ids = New-ShapeSet -Material $material -Shapes block, pane -Textures (T $material) -Name @{ en = "${en}Framed Glass"; zh = "$($kv.Value[1])镶框玻璃" } `
        -Sound glass -Hardness 3 -Resistance 10 -Tool pickaxe -Tags 'lepton:framed_glass' -RenderMethod $render -NoCrafting
    Update-BlockComponents $ids.block { param($comp) $comp['minecraft:light_dampening'] = 0 }
    if ($c) {
        Write-ShapelessRecipe $material (@('lepton:framed_glass') * 8 + @("minecraft:${c}_dye")) $ids.block 8
        Write-ShapelessRecipe "${material}_pane_dye" (@('lepton:framed_glass_pane') * 8 + @("minecraft:${c}_dye")) $ids.pane 8
    } else {
        Write-ShapedRecipe $material @('IGI', 'G G', 'IGI') @{ I = 'minecraft:iron_ingot'; G = 'minecraft:glass' } $ids.block 8
    }
    Write-ShapedRecipe "${material}_pane" @('###', '###') @{ '#' = $ids.block } $ids.pane 16
}
Set-Part 'FramedGlass'

# ---- GoldBars ------------------------------------------------------------------------------------
$null = New-ShapeSet -Material 'gold_bars' -Shapes pane -Ids @{ pane = 'lepton:gold_bars' } -BaseBlock 'minecraft:gold_block' `
    -Name @{ en = 'Gold Bars'; zh = '金栏杆' } -Names @{ pane = @{ en = 'Gold Bars'; zh = '金栏杆' } } -Textures (T 'gold_bars') `
    -Sound metal -Hardness 5 -Resistance 6 -MapColor '#f8d93a' -Tool pickaxe -RenderMethod alpha_test -NoCrafting
Write-ShapedRecipe 'gold_bars' @('###', '###') @{ '#' = 'minecraft:gold_ingot' } 'lepton:gold_bars' 16
Set-Part 'GoldBars'

# ---- IndustrialPalette ---------------------------------------------------------------------------
$plate = @{ Hardness = 5; Resistance = 6; MapColor = '#a7a7a7'; Tool = 'pickaxe'; Sound = 'metal' }
$null = New-ShapeSet @plate -Material 'iron_plate' -Shapes block, stairs, slab, vertical_slab -Textures (T 'iron_plate') -Name @{ en = 'Iron Plate'; zh = '铁板块' } `
    -Names @{ vertical_slab = @{ en = 'Iron Plate Vertical Slab'; zh = '铁板块竖直台阶' } } -Stonecutter
$null = New-ShapeSet @plate -Material 'rusty_iron_plate' -Shapes block, stairs, slab, vertical_slab -Textures (T 'rusty_iron_plate') -Name @{ en = 'Rusty Iron Plate'; zh = '锈化铁板块' } `
    -Names @{ vertical_slab = @{ en = 'Rusty Iron Plate Vertical Slab'; zh = '锈化铁板块竖直台阶' } } -Stonecutter
New-FullBlock -Id 'lepton:iron_pillar' -Textures @{ side = (T 'iron_pillar'); top = (T 'iron_pillar_top') } -Name @{ en = 'Iron Pillar'; zh = '铁柱' } `
    -Sound metal -Hardness 5 -Resistance 6 -MapColor '#a7a7a7' -Tool pickaxe -Axis
Write-ShapedRecipe 'iron_plate' @('###', '# #', '###') @{ '#' = 'minecraft:iron_ingot' } 'lepton:iron_plate' 24
Write-ShapedRecipe 'rusty_iron_plate' @('###', '#W#', '###') @{ '#' = 'minecraft:iron_ingot'; W = 'minecraft:water_bucket' } 'lepton:rusty_iron_plate' 24
Write-ShapedRecipe 'rusty_iron_plate_from_iron_plate' @('###', '#W#', '###') @{ '#' = 'lepton:iron_plate'; W = 'minecraft:water_bucket' } 'lepton:rusty_iron_plate' 8
Write-ShapedRecipe 'iron_pillar' @('#', '#') @{ '#' = 'lepton:iron_plate_slab' } 'lepton:iron_pillar' 1
Write-ShapelessRecipe 'iron_pillar_from_iron_plate_stonecutting' @('lepton:iron_plate') 'lepton:iron_pillar' 1 'stonecutter'
Set-Part 'IronPlates'

# Iron ladder: a 1 px panel against the block it hangs on (minecraft:block_face = the clicked face,
# the ladder's open side). Collision / selection 3 px like Java's LadderBlock.
$ladderBones = [ordered]@{
    north = @(@{ Box = (New-Box 0 0 15 16 16 16); Faces = @{ '*' = 'ladder' } })
    south = @(@{ Box = (New-Box 0 0 0 16 16 1); Faces = @{ '*' = 'ladder' } })
    west  = @(@{ Box = (New-Box 15 0 0 16 16 16); Faces = @{ '*' = 'ladder' } })
    east  = @(@{ Box = (New-Box 0 0 0 1 16 16); Faces = @{ '*' = 'ladder' } })
}
$ladderGeo = Write-PaletteGeometry 'palette_ladder' $ladderBones
$ladderBox = @{ north = (New-Box 0 0 13 16 16 16); south = (New-Box 0 0 0 16 16 3); west = (New-Box 13 0 0 16 16 16); east = (New-Box 0 0 0 3 16 16) }
$spec = @{ Hardness = 0.8; Resistance = 0.8; MapColor = '#a7a7a7'; Tool = 'pickaxe'; Tags = @('lepton:ladder', 'lepton:climbable'); Category = 'items' }
$comp = Get-BaseComponents $spec
$comp['minecraft:light_dampening'] = 0
$comp['minecraft:geometry'] = [ordered]@{ identifier = $ladderGeo; bone_visibility = [ordered]@{
        north = Get-StateCond 'minecraft:block_face' 'north'; south = Get-StateCond 'minecraft:block_face' 'south'
        west = Get-StateCond 'minecraft:block_face' 'west'; east = Get-StateCond 'minecraft:block_face' 'east' } }
$comp['minecraft:material_instances'] = [ordered]@{ '*' = (New-Mi (T 'iron_ladder') 'alpha_test'); ladder = (New-Mi (T 'iron_ladder') 'alpha_test') }
$comp['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('side') }) }
$comp['minecraft:loot'] = Write-LootTable 'iron_ladder' 'lepton:iron_ladder'
$perms = foreach ($d in 'north', 'south', 'west', 'east') {
    [ordered]@{ condition = Get-StateCond 'minecraft:block_face' $d; components = [ordered]@{
            'minecraft:collision_box' = ConvertTo-BoxJson $ladderBox[$d]; 'minecraft:selection_box' = ConvertTo-BoxJson $ladderBox[$d] } }
}
Write-BlockJson -Id 'lepton:iron_ladder' -Menu ([ordered]@{ category = 'items'; group = 'minecraft:itemGroup.name.ladder' }) `
    -Traits ([ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }) -Components $comp -Permutations $perms
Register-BlockClient 'lepton:iron_ladder' 'metal' @{ en = 'Iron Ladder'; zh = '铁梯子' }
Write-ShapedRecipe 'iron_ladder' @('N N', 'NIN', 'N N') @{ N = 'minecraft:iron_nugget'; I = 'minecraft:iron_ingot' } 'lepton:iron_ladder' 3
Set-Part 'IronLadder'

# ---- JapanesePalette -----------------------------------------------------------------------------
$lanternGeo = Write-PaletteGeometry 'palette_paper_lantern' ([ordered]@{
        lantern = @(
            @{ Box = (New-Box 2 2 2 14 14 14); Faces = @{ '*' = 'side'; up = 'top'; down = 'top' } },
            @{ Box = (New-Box 6 14 6 10 16 10); Faces = @{ '*' = 'inside' } },
            @{ Box = (New-Box 6 0 6 10 2 10); Faces = @{ '*' = 'inside' } })
    })
foreach ($l in @(@('paper_lantern', 'paper_lantern_side', 'Paper Lantern', '纸灯笼'), @('paper_lantern_sakura', 'paper_lantern_sakura_side', 'Decorated Paper Lantern', '雕花纸灯笼'))) {
    $id = "lepton:$($l[0])"
    $comp = Get-BaseComponents @{ Hardness = 1.5; Resistance = 1.5; MapColor = '#f9fffe'; Tool = 'axe'; Tags = @('lepton:paper_lantern') }
    $comp['minecraft:light_emission'] = 15
    $comp['minecraft:light_dampening'] = 0
    $comp['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = 60; destroy_chance_modifier = 30 }
    $comp['minecraft:geometry'] = $lanternGeo
    $comp['minecraft:material_instances'] = [ordered]@{
        '*' = (New-Mi (T $l[1])); side = (New-Mi (T $l[1])); top = (New-Mi (T 'paper_lantern_top')); inside = (New-Mi (T 'paper_lantern_inside'))
    }
    $comp['minecraft:collision_box'] = Get-CollisionJson @((New-Box 6 0 6 10 16 10), (New-Box 2 2 2 14 14 14))
    $comp['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 2 0 2 14 16 14)
    $comp['minecraft:loot'] = Write-LootTable $l[0] $id
    Write-BlockJson -Id $id -Menu ([ordered]@{ category = 'construction' }) -Components $comp
    Register-BlockClient $id 'wood' @{ en = $l[2]; zh = $l[3] }
}
Write-ShapedRecipe 'paper_lantern' @('PSP', 'PGP', 'PSP') @{ P = 'minecraft:paper'; S = 'minecraft:bamboo'; G = 'minecraft:glowstone_dust' } 'lepton:paper_lantern' 1
$saplings = @('minecraft:oak_sapling', 'minecraft:spruce_sapling', 'minecraft:birch_sapling', 'minecraft:jungle_sapling', 'minecraft:acacia_sapling',
    'minecraft:dark_oak_sapling', 'minecraft:cherry_sapling', 'minecraft:pale_oak_sapling', 'minecraft:mangrove_propagule', 'minecraft:azalea', 'minecraft:flowering_azalea')
foreach ($s in $saplings) {
    Write-ShapelessRecipe "paper_lantern_sakura_from_$(Get-Path $s)" @('lepton:paper_lantern', $s) 'lepton:paper_lantern_sakura' 1
    Write-ShapelessRecipe "paper_wall_sakura_from_$(Get-Path $s)" @('lepton:paper_wall', $s) 'lepton:paper_wall_sakura' 1
}
foreach ($w in @(@('paper_wall', 'Paper Wall', '纸墙'), @('paper_wall_big', 'Big Paper Wall', '大纸墙'), @('paper_wall_sakura', 'Decorated Paper Wall', '雕花纸墙'))) {
    $null = New-ShapeSet -Material $w[0] -Shapes pane -Ids @{ pane = "lepton:$($w[0])" } -BaseBlock 'lepton:paper_lantern' `
        -Name @{ en = $w[1]; zh = $w[2] } -Names @{ pane = @{ en = $w[1]; zh = $w[2] } } -Textures (T $w[0]) `
        -Sound wood -Hardness 1.5 -Resistance 1.5 -MapColor '#f9fffe' -Tool axe -Tags 'lepton:paper_wall' -NoCrafting
    Set-Flammable "lepton:$($w[0])" 60 30
}
Write-ShapedRecipe 'paper_wall' @('###', 'PPP', '###') @{ '#' = 'minecraft:bamboo'; P = 'minecraft:paper' } 'lepton:paper_wall' 6
Write-ShapedRecipe 'paper_wall_big' @('##', '##') @{ '#' = 'lepton:paper_wall' } 'lepton:paper_wall_big' 4
Set-Part 'PaperBlocks'

# Bamboo mats: lepton:facing; textures per face so the stalks run along the facing (down: upright).
$faceTex = @{
    # facing -> face -> texture (u / v: stalks along the face's horizontal / vertical texture axis)
    north = @{ up = 'bamboo_mat_v'; down = 'bamboo_mat_v'; north = 'bamboo_mat_end'; south = 'bamboo_mat_end'; west = 'bamboo_mat_u'; east = 'bamboo_mat_u' }
    south = @{ up = 'bamboo_mat_v'; down = 'bamboo_mat_v'; north = 'bamboo_mat_end'; south = 'bamboo_mat_end'; west = 'bamboo_mat_u'; east = 'bamboo_mat_u' }
    west  = @{ up = 'bamboo_mat_u'; down = 'bamboo_mat_u'; north = 'bamboo_mat_u'; south = 'bamboo_mat_u'; west = 'bamboo_mat_end'; east = 'bamboo_mat_end' }
    east  = @{ up = 'bamboo_mat_u'; down = 'bamboo_mat_u'; north = 'bamboo_mat_u'; south = 'bamboo_mat_u'; west = 'bamboo_mat_end'; east = 'bamboo_mat_end' }
    down  = @{ up = 'bamboo_mat_end'; down = 'bamboo_mat_end'; north = 'bamboo_mat_v'; south = 'bamboo_mat_v'; west = 'bamboo_mat_v'; east = 'bamboo_mat_v' }
}
$carpetGeo = Write-PaletteGeometry 'palette_carpet' ([ordered]@{ carpet = @(@{ Box = (New-Box 0 0 0 16 1 16) }) })
foreach ($m in @(@('bamboo_mat', 'Bamboo Mat Block', '竹席块', $false), @('bamboo_mat_carpet', 'Bamboo Mat', '竹席', $true))) {
    $id = "lepton:$($m[0])"
    $comp = Get-BaseComponents @{ Hardness = 0.5; Resistance = 0.5; MapColor = '#e5e533'; Tool = 'axe'; Tags = @('lepton:bamboo_mat') }
    $mis = { param($f) $mi = [ordered]@{}; foreach ($face in 'up', 'down', 'north', 'south', 'west', 'east') { $mi[$face] = New-Mi (T $faceTex[$f][$face]) }; $mi['*'] = $mi.up; $mi }
    $comp['minecraft:material_instances'] = & $mis 'north'
    if ($m[3]) {
        $comp['minecraft:geometry'] = $carpetGeo
        $comp['minecraft:light_dampening'] = 0
        $comp['minecraft:collision_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 1 16)
        $comp['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 1 16)
    } else {
        $comp['minecraft:geometry'] = 'minecraft:geometry.full_block'
    }
    $comp['minecraft:loot'] = Write-LootTable $m[0] $id
    $perms = foreach ($f in 'south', 'west', 'east', 'down') { [ordered]@{ condition = Get-StateCond 'lepton:facing' $f; components = [ordered]@{ 'minecraft:material_instances' = (& $mis $f) } } }
    Write-BlockJson -Id $id -Menu ([ordered]@{ category = 'construction' }) -States ([ordered]@{ 'lepton:facing' = @('north', 'south', 'west', 'east', 'down') }) `
        -Components $comp -Permutations $perms
    Register-BlockClient $id 'bamboo' @{ en = $m[1]; zh = $m[2] }
}
Write-ShapedRecipe 'bamboo_mat' @('SBS', 'BBB', 'SBS') @{ S = 'minecraft:stick'; B = 'minecraft:bamboo' } 'lepton:bamboo_mat' 2
Write-ShapedRecipe 'bamboo_mat_carpet' @('MM') @{ M = 'lepton:bamboo_mat' } 'lepton:bamboo_mat_carpet' 4
Set-Part 'BambooMats'

# ---- NetherBrickFenceGate ------------------------------------------------------------------------
$null = New-ShapeSet -Material 'nether_brick' -Shapes fence_gate -BaseBlock 'minecraft:nether_brick' -FenceStick 'minecraft:netherbrick' `
    -Textures 'textures/blocks/nether_brick' -Name @{ en = 'Nether Brick'; zh = '下界砖' } -Names @{ fence_gate = @{ en = 'Nether Brick Fence Gate'; zh = '下界砖栅栏门' } } `
    -Sound nether_brick -Hardness 2 -Resistance 6 -MapColor '#700200' -Tool pickaxe
Set-Part 'NetherBrickFenceGate'

# ---- Thatch --------------------------------------------------------------------------------------
New-FullBlock -Id 'lepton:thatch' -Textures @{ side = (T 'thatch_side'); top = (T 'thatch_top') } -Name @{ en = 'Thatch'; zh = '茅草' } `
    -Sound grass -Hardness 0.5 -Resistance 0.5 -MapColor '#e5e533' -Tool hoe -Flammable -Tags 'lepton:thatch'
Set-Flammable 'lepton:thatch' 5 300
$null = New-ShapeSet -Material 'thatch' -BaseBlock 'lepton:thatch' -Shapes stairs, slab, vertical_slab -Textures @{ side = (T 'thatch_side'); top = (T 'thatch_top') } `
    -Name @{ en = 'Thatch'; zh = '茅草' } -Names @{ vertical_slab = @{ en = 'Thatch Vertical Slab'; zh = '茅草竖直台阶' } } `
    -Sound grass -Hardness 0.5 -Resistance 0.5 -MapColor '#e5e533' -Tool hoe -Flammable
foreach ($s in 'stairs', 'slab', 'vertical_slab') { Set-Flammable "lepton:thatch_$s" 5 300 }
Write-ShapedRecipe 'thatch' @('##', '##') @{ '#' = 'minecraft:wheat' } 'lepton:thatch' 4
Write-ShapelessRecipe 'thatch_revert' @('lepton:thatch') 'minecraft:wheat' 1
Set-Part 'Thatch'

# ---- Grate ---------------------------------------------------------------------------------------
$grateGeo = Write-PaletteGeometry 'palette_grate' ([ordered]@{ plate = @(@{ Box = (New-Box 0 15 0 16 16 16) }) })
$comp = Get-BaseComponents @{ Hardness = 5; Resistance = 10; MapColor = '#a7a7a7'; Tool = 'pickaxe'; Tags = @('lepton:grate') }
$comp['minecraft:light_dampening'] = 0
$comp['minecraft:geometry'] = $grateGeo
$gmi = New-Mi (T 'grate') 'alpha_test'
$comp['minecraft:material_instances'] = [ordered]@{ '*' = $gmi; up = $gmi; down = $gmi; north = $gmi; south = $gmi; west = $gmi; east = $gmi }
$comp['minecraft:collision_box'] = ConvertTo-BoxJson (New-Box 0 15 0 16 16 16)
$comp['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 15 0 16 16 16)
$comp['minecraft:liquid_detection'] = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
$comp['minecraft:loot'] = Write-LootTable 'grate' 'lepton:grate'
Write-BlockJson -Id 'lepton:grate' -Menu ([ordered]@{ category = 'items' }) -Components $comp
Register-BlockClient 'lepton:grate' 'metal' @{ en = 'Iron Grate'; zh = '铁丝网' }
Write-ShapedRecipe 'grate' @('##', '##') @{ '#' = 'minecraft:iron_bars' } 'lepton:grate' 1
Set-Part 'Grate'

# ---- id table for the native side ----------------------------------------------------------------
$lines = foreach ($kv in $script:PartOf.GetEnumerator()) { if ($kv.Value -ne 'None') { "{`"$($kv.Key)`", Part::$($kv.Value)}," } }
$inc = "// Generated by tools/pack/palette.ps1: every block / recipe id of U5 palette and its config part.`n" + ($lines -join "`n")
Write-PackText -Path (Join-Path $script:RepoRoot 'src/feature/palette/PaletteIds.inc') -Text $inc

Complete-PackUnit
