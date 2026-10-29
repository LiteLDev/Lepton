# U8 storage: CompressedBlocks, Stools, Rope, ShearVines, MorePottedPlants, GlassShard, UtilityRecipes
# (docs/spec/storage.md).
#
#   pwsh -NoProfile -File tools/pack/storage.ps1     # regenerates and overwrites the committed output
#
# Besides the pack files it writes src/feature/storage/Content.inc: every block, item and recipe id
# of the unit with the config keys that must all be on for it (read by src/feature/storage/).
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/storage.ps1"

Start-PackUnit 'storage'

$blockTex = Join-Path $script:RP 'textures/blocks/storage'
$itemTex = Join-Path $script:RP 'textures/items/storage'
foreach ($p in (Write-StorageTextures -BlockDir $blockTex -ItemDir $itemTex)) { $null = Add-PackFile $p }
function T([string]$Name) { return "textures/blocks/storage/$Name" }

# ---- gates ----------------------------------------------------------------------------------------
$script:Gates = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
$script:RecipeGates = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
function Set-Gate([string]$Id, [string[]]$Keys) { $script:Gates[$Id] = ($Keys -join ',') }

# ---- recipe writers (ingredients: 'minecraft:x' or '#tag') ----------------------------------------
function Get-Ingredient([string]$I) {
    if ($I.StartsWith('#')) { return [ordered]@{ tag = $I.Substring(1) } }
    return [ordered]@{ item = $I }
}
function Get-Unlock([string[]]$Items) {
    $u = @($Items | Where-Object { -not $_.StartsWith('#') } | Sort-Object -Unique | ForEach-Object { [ordered]@{ item = $_ } })
    if (-not $u.Count) { $u = @([ordered]@{ context = 'AlwaysUnlocked' }) }
    return $u
}
function Add-Shaped([string]$Name, [string[]]$Pattern, [hashtable]$Key, [string]$Result, [int]$Count, [string[]]$Gate, [int]$Priority = 0) {
    $k = [ordered]@{}
    foreach ($ch in ($Key.Keys | Sort-Object)) { $k[$ch] = Get-Ingredient $Key[$ch] }
    $r = [ordered]@{
        description = [ordered]@{ identifier = "lepton:$Name" }
        tags        = @('crafting_table')
        pattern     = @($Pattern)
        key         = $k
        unlock      = @(Get-Unlock @($Key.Values))
        result      = [ordered]@{ item = $Result; count = $Count }
    }
    if ($Priority) { $r.priority = $Priority }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{ format_version = $script:RecipeFormat; 'minecraft:recipe_shaped' = $r })
    $script:RecipeGates["lepton:$Name"] = ($Gate -join ',')
}
function Add-Shapeless([string]$Name, [string[]]$Ingredients, [string]$Result, [int]$Count, [string[]]$Gate) {
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version               = $script:RecipeFormat
            'minecraft:recipe_shapeless' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @('crafting_table')
                ingredients = @($Ingredients | ForEach-Object { Get-Ingredient $_ })
                unlock      = @(Get-Unlock $Ingredients)
                result      = [ordered]@{ item = $Result; count = $Count }
            }
        })
    $script:RecipeGates["lepton:$Name"] = ($Gate -join ',')
}
function Add-Smelting([string]$Name, [string]$InputItem, [string]$Output, [int]$Count, [string[]]$Tags, [string[]]$Gate) {
    $out = if ($Count -eq 1) { $Output } else { [ordered]@{ item = $Output; count = $Count } }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version            = $script:RecipeFormat
            'minecraft:recipe_furnace' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @($Tags)
                input       = $InputItem
                output      = $out
            }
        })
    $script:RecipeGates["lepton:$Name"] = ($Gate -join ',')
}

# ---- geometry (unit-owned files: models/blocks/storage_<name>.geo.json) ---------------------------
# Cube in block pixels with explicit per-face UVs: $Uv = @{ north = @(u, v, w, h); ... } (missing
# faces are not drawn), one material instance for the whole cube. Model x is mirrored (see lib.ps1).
function New-UvCube($Box, [hashtable]$Uv, [string]$Material = '*', $Rotation = $null, $Pivot = $null) {
    $faces = [ordered]@{}
    foreach ($f in 'north', 'south', 'east', 'west', 'up', 'down') {
        if (-not $Uv.ContainsKey($f)) { continue }
        $u = $Uv[$f]
        # the model's east face is the world's west face
        $mf = if ($script:GeoMirrorX -and $f -eq 'east') { 'west' } elseif ($script:GeoMirrorX -and $f -eq 'west') { 'east' } else { $f }
        $faces[$mf] = [ordered]@{ uv = @((Get-Num $u[0]), (Get-Num $u[1])); uv_size = @((Get-Num $u[2]), (Get-Num $u[3])); material_instance = $Material }
    }
    $ox = if ($script:GeoMirrorX) { 8 - $Box.X1 } else { $Box.X0 - 8 }
    $cube = [ordered]@{
        origin = @((Get-Num $ox), (Get-Num $Box.Y0), (Get-Num ($Box.Z0 - 8)))
        size   = @((Get-Num ($Box.X1 - $Box.X0)), (Get-Num ($Box.Y1 - $Box.Y0)), (Get-Num ($Box.Z1 - $Box.Z0)))
        uv     = $faces
    }
    if ($null -ne $Rotation) { $cube.rotation = @($Rotation); $cube.pivot = @($Pivot) }
    return $cube
}
# World-aligned ("uv locked") UVs of a box, like lib.ps1's New-GeoCube.
function Get-LockedUv($b) {
    return @{
        north = @((16 - $b.X1), (16 - $b.Y1), ($b.X1 - $b.X0), ($b.Y1 - $b.Y0)); south = @($b.X0, (16 - $b.Y1), ($b.X1 - $b.X0), ($b.Y1 - $b.Y0))
        east = @((16 - $b.Z1), (16 - $b.Y1), ($b.Z1 - $b.Z0), ($b.Y1 - $b.Y0)); west = @($b.Z0, (16 - $b.Y1), ($b.Z1 - $b.Z0), ($b.Y1 - $b.Y0))
        up = @($b.X0, $b.Z0, ($b.X1 - $b.X0), ($b.Z1 - $b.Z0)); down = @($b.X0, (16 - $b.Z1), ($b.X1 - $b.X0), ($b.Z1 - $b.Z0))
    }
}
function Write-StorageGeometry([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) { [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($kv.Value) } }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = "geometry.lepton.storage_$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($bonesJson)
            })
    }
    Write-PackJson -Path (Join-Path $script:RP "models/blocks/storage_$Name.geo.json") -Object $geo
    return "geometry.lepton.storage_$Name"
}

$script:WaterlogRule = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }

# A block with Lepton's base components plus Quark's specifics. -Flame @(catch, destroy) (Java fire
# spread speed, flammability).
function Add-StorageBlock {
    param(
        [Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][hashtable]$Name, $Textures, [string]$Sound = 'stone',
        [double]$Hardness = 1, [double]$Resistance = -1, [string]$MapColor, [string]$Tool, [int[]]$Flame, [int]$Light = 0,
        [string]$RenderMethod = 'opaque', [switch]$Axis, [string]$Category = 'construction', [string]$Group, [string[]]$Tags = @(),
        [string[]]$Gate, [switch]$Hidden, [string]$Loot, $Geometry, $Material, $Collision, $Selection, $States, $Permutations,
        [switch]$PassLight, [switch]$Waterlog
    )
    if ($Resistance -lt 0) { $Resistance = $Hardness }
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $c = Get-BaseComponents $spec
    if ($Flame) { $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = $Flame[0]; destroy_chance_modifier = $Flame[1] } }
    if ($Light) { $c['minecraft:light_emission'] = $Light }
    if ($PassLight) { $c['minecraft:light_dampening'] = 0 }
    if ($Waterlog) { $c['minecraft:liquid_detection'] = $script:WaterlogRule }
    $c['minecraft:geometry'] = if ($Geometry) { $Geometry } else { 'minecraft:geometry.full_block' }
    $c['minecraft:material_instances'] = if ($Material) { $Material } else { Get-MaterialInstances $Textures $RenderMethod }
    if ($null -ne $Collision) { $c['minecraft:collision_box'] = $Collision }
    if ($null -ne $Selection) { $c['minecraft:selection_box'] = $Selection }
    $c['minecraft:loot'] = if ($Loot) { $Loot } else { Write-LootTable (Get-Path $Id) $Id }
    $traits = $null; $perms = @($Permutations | Where-Object { $_ })
    if ($Axis) {
        $c['tag:lepton:pillar'] = [ordered]@{}
        $traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
        $perms += [ordered]@{ condition = "q.block_state('minecraft:block_face') == 'north' || q.block_state('minecraft:block_face') == 'south'"
            components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(90, 0, 0) } } }
        $perms += [ordered]@{ condition = "q.block_state('minecraft:block_face') == 'east' || q.block_state('minecraft:block_face') == 'west'"
            components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, 0, 90) } } }
    }
    $menu = if ($Hidden) { [ordered]@{ category = 'none' } } else { Get-MenuCategory $spec $Group }
    Write-BlockJson -Id $Id -Menu $menu -States $States -Traits $traits -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
    if ($Gate) { Set-Gate $Id $Gate }
}

# An item with an icon (shards; block items of blocks with a flat icon).
function Add-StorageItem([string]$Id, [hashtable]$Name, [string]$Icon, [string]$Category = 'items', [string]$Group, [string]$Block, [string[]]$ItemTags, [string[]]$Gate) {
    $menu = [ordered]@{ category = $Category }
    if ($Group) { $menu.group = $Group }
    $comp = [ordered]@{ 'minecraft:icon' = (Add-ItemTexture $Icon) }
    if ($Block) { $comp['minecraft:block_placer'] = [ordered]@{ block = $Block; replace_block_item = $true } }
    Add-PackText "item.$Id.name" $Name
    if ($ItemTags) { $comp['minecraft:tags'] = [ordered]@{ tags = @($ItemTags) } }
    $comp['minecraft:max_stack_size'] = 64
    Write-PackJson (Join-Path $script:BP "items/$(Get-Path $Id).json") ([ordered]@{
            format_version   = $script:ItemFormat
            'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = $Id; menu_category = $menu }; components = $comp }
        })
    if ($Gate) { Set-Gate $Id $Gate }
}

# ==================================================================================================
# CompressedBlocks
# ==================================================================================================
$CB = 'enableCompressedBlocks'
$cubeGroup = $null

# Block of Charcoal (pickaxe; the "requires correct tool" rule is set natively)
Add-StorageBlock -Id 'lepton:charcoal_block' -Name @{ en = 'Block of Charcoal'; zh = '木炭块' } -Textures (T 'charcoal_block') `
    -Hardness 5 -Resistance 10 -MapColor '#191919' -Tool pickaxe -Gate $CB, 'enableCharcoalBlock' -Tags 'lepton:infiniburn'
Add-Shaped 'storage_charcoal_block' @('###', '###', '###') @{ '#' = 'minecraft:charcoal' } 'lepton:charcoal_block' 1 @($CB, 'enableCharcoalBlock')
Add-Shapeless 'storage_charcoal_block_uncompress' @('lepton:charcoal_block') 'minecraft:charcoal' 9 @($CB, 'enableCharcoalBlock')

# pillars: name, en, zh, map colour, flammability, item
$pillars = @(
    @('sugar_cane', 'Sugar Cane Bundle', '甘蔗捆', '#7fcc19', 200, 'minecraft:sugar_cane', 'enableSugarCaneBlock'),
    @('cactus', 'Cactus Batch', '仙人掌块', '#667f33', 50, 'minecraft:cactus', 'enableCactusBlock'),
    @('chorus_fruit', 'Chorus Fruit Batch', '紫颂果块', '#7f3fb2', 10, 'minecraft:chorus_fruit', 'enableChorusFruitBlock'),
    @('stick', 'Stick Stack', '木棍堆', '#8f7748', 300, 'minecraft:stick', 'enableStickBlock')
)
foreach ($p in $pillars) {
    $id = "lepton:$($p[0])_block"
    Add-StorageBlock -Id $id -Name @{ en = $p[1]; zh = $p[2] } -Textures @{ side = (T "$($p[0])_block"); top = (T "$($p[0])_block_top") } `
        -Hardness 0.5 -Sound wood -MapColor $p[3] -Tool axe -Flame 0, $p[4] -Axis -Gate $CB, $p[6]
    Add-Shaped "storage_$($p[0])_block" @('###', '###', '###') @{ '#' = $p[5] } $id 1 @($CB, $p[6])
    Add-Shapeless "storage_$($p[0])_block_uncompress" @($id) $p[5] 9 @($CB, $p[6])
}

# crates: name, en, zh, map colour, item, flag
$crates = @(
    @('apple', 'Apple Crate', '箱装苹果', '#993333', 'minecraft:apple', 'enableAppleCrate'),
    @('golden_apple', 'Golden Apple Crate', '箱装金苹果', '#faee4d', 'minecraft:golden_apple', 'enableGoldenAppleCrate'),
    @('potato', 'Potato Crate', '箱装马铃薯', '#d87f33', 'minecraft:potato', 'enablePotatoCrate'),
    @('carrot', 'Carrot Crate', '箱装胡萝卜', '#9f5224', 'minecraft:carrot', 'enableCarrotCrate'),
    @('golden_carrot', 'Golden Carrot Crate', '箱装金胡萝卜', '#faee4d', 'minecraft:golden_carrot', 'enableGoldenCarrotCrate'),
    @('beetroot', 'Beetroot Crate', '箱装甜菜根', '#993333', 'minecraft:beetroot', 'enableBeetrootCrate')
)
foreach ($p in $crates) {
    $id = "lepton:$($p[0])_crate"
    $tags = @('lepton:storage_wooden'); if ($p[0] -eq 'golden_apple') { $tags += 'lepton:beacon_base' }
    Add-StorageBlock -Id $id -Name @{ en = $p[1]; zh = $p[2] } -Textures @{ side = (T "$($p[0])_crate"); top = (T "$($p[0])_crate_top"); bottom = (T "$($p[0])_crate_bottom") } `
        -Hardness 1.5 -Sound wood -MapColor $p[3] -Tool axe -Flame 0, 150 -Tags $tags -Gate $CB, $p[5]
    Add-Shaped "storage_$($p[0])_crate" @('###', '###', '###') @{ '#' = $p[4] } $id 1 @($CB, $p[5])
    Add-Shapeless "storage_$($p[0])_crate_uncompress" @($id) $p[4] 9 @($CB, $p[5])
}

# sacks: name, en, zh, map colour, item, flag, light
$sacks = @(
    @('cocoa_beans', 'Cocoa Bean Sack', '袋装可可豆', '#664c33', 'minecraft:cocoa_beans', 'enableCocoaBeanSack', 0),
    @('nether_wart', 'Nether Wart Sack', '袋装下界疣', '#993333', 'minecraft:nether_wart', 'enableNetherWartSack', 0),
    @('gunpowder', 'Gunpowder Sack', '袋装火药', '#4c4c4c', 'minecraft:gunpowder', 'enableGunpowderSack', 0),
    @('berry', 'Sweet Berry Sack', '袋装甜浆果', '#993333', 'minecraft:sweet_berries', 'enableBerrySack', 0),
    @('glowberry', 'Glow Berry Sack', '袋装发光浆果', '#e5e533', 'minecraft:glow_berries', 'enableGlowBerrySack', 14)
)
foreach ($p in $sacks) {
    $id = "lepton:$($p[0])_sack"
    Add-StorageBlock -Id $id -Name @{ en = $p[1]; zh = $p[2] } -Textures @{ side = (T "$($p[0])_sack"); top = (T "$($p[0])_sack_top"); bottom = (T "$($p[0])_sack_bottom") } `
        -Hardness 0.5 -Sound cloth -MapColor $p[3] -Flame 0, 150 -Light $p[6] -Gate $CB, $p[5]
    Add-Shaped "storage_$($p[0])_sack" @('###', '###', '###') @{ '#' = $p[4] } $id 1 @($CB, $p[5])
    Add-Shapeless "storage_$($p[0])_sack_uncompress" @($id) $p[4] 9 @($CB, $p[5])
}
# the 3x3 nether wart pattern now makes the sack: 4 nether wart make a nether wart block
Add-Shapeless 'storage_nether_wart_block_4x4' @('minecraft:nether_wart', 'minecraft:nether_wart', 'minecraft:nether_wart', 'minecraft:nether_wart') `
    'minecraft:nether_wart_block' 1 @($CB, 'enableNetherWartSack')

Add-StorageBlock -Id 'lepton:blaze_lantern' -Name @{ en = 'Blaze Lantern'; zh = '烈焰灯' } -Textures (T 'blaze_lantern') -Hardness 0.3 `
    -Sound glass -MapColor '#e5e533' -Light 15 -Tags 'lepton:infiniburn' -Gate $CB, 'enableBlazeLantern'
Add-Shaped 'storage_blaze_lantern' @('BPB', 'PPP', 'BPB') @{ B = 'minecraft:blaze_rod'; P = 'minecraft:blaze_powder' } 'lepton:blaze_lantern' 1 @($CB, 'enableBlazeLantern')

foreach ($p in @(
        @('bonded_leather', 'Bonded Leather', '打包皮革', '#d87f33', 'minecraft:leather', 'enableBondedLeather'),
        @('bonded_rabbit_hide', 'Bonded Rabbit Hide', '打包兔子皮', '#d1b1a1', 'minecraft:rabbit_hide', 'enableBondedRabbitHide'))) {
    $id = "lepton:$($p[0])"
    Add-StorageBlock -Id $id -Name @{ en = $p[1]; zh = $p[2] } -Textures (T $p[0]) -Hardness 0.4 -Sound cloth -MapColor $p[3] -Gate $CB, $p[5]
    Add-Shaped "storage_$($p[0])" @('###', '###', '###') @{ '#' = $p[4] } $id 1 @($CB, $p[5])
    Add-Shapeless "storage_$($p[0])_uncompress" @($id) $p[4] 9 @($CB, $p[5])
}

# ==================================================================================================
# Stools
# ==================================================================================================
$stoolGeo = Write-StorageGeometry 'stool' ([ordered]@{
        small = @(
            (New-UvCube (New-Box 0 1 0 16 9 16) @{ north = @(0, 0, 8, 4); south = @(0, 0, 8, 4); east = @(0, 0, 8, 4); west = @(0, 0, 8, 4); up = @(8, 0, 8, 8); down = @(8, 8, 8, 8) })
            foreach ($l in @(@(0, 0), @(12, 0), @(0, 12), @(12, 12))) {
                New-UvCube (New-Box $l[0] 0 $l[1] ($l[0] + 4) 1 ($l[1] + 4)) @{ north = @(0, 15, 4, 1); south = @(0, 15, 4, 1); east = @(0, 15, 4, 1); west = @(0, 15, 4, 1); down = @(4, 8, 4, 4) }
            })
        big   = @(
            (New-UvCube (New-Box 0 8 0 16 16 16) @{ north = @(0, 0, 8, 4); south = @(0, 0, 8, 4); east = @(0, 0, 8, 4); west = @(0, 0, 8, 4); up = @(8, 0, 8, 8); down = @(8, 8, 8, 8) })
            foreach ($l in @(@(0, 0), @(12, 0), @(0, 12), @(12, 12))) {
                New-UvCube (New-Box $l[0] 0 $l[1] ($l[0] + 4) 8 ($l[1] + 4)) @{ north = @(0, 8, 4, 8); south = @(0, 8, 4, 8); east = @(0, 8, 4, 8); west = @(0, 8, 4, 8); down = @(4, 8, 4, 4) }
            })
    })
$mapColors = @{ white = '#ffffff'; light_gray = '#999999'; gray = '#4c4c4c'; black = '#191919'; brown = '#664c33'; red = '#993333'; orange = '#d87f33'
    yellow = '#e5e533'; lime = '#7fcc19'; green = '#667f33'; cyan = '#4c7f99'; light_blue = '#6699d8'; blue = '#334cb2'; purple = '#7f3fb2'; magenta = '#b24cd8'; pink = '#f27fa5' }
$colorNames = [ordered]@{
    white = @('White', '白色'); orange = @('Orange', '橙色'); magenta = @('Magenta', '品红色'); light_blue = @('Light Blue', '淡蓝色')
    yellow = @('Yellow', '黄色'); lime = @('Lime', '黄绿色'); pink = @('Pink', '粉红色'); gray = @('Gray', '灰色'); light_gray = @('Light Gray', '淡灰色')
    cyan = @('Cyan', '青色'); purple = @('Purple', '紫色'); blue = @('Blue', '蓝色'); brown = @('Brown', '棕色'); green = @('Green', '绿色')
    red = @('Red', '红色'); black = @('Black', '黑色')
}
$smallBox = New-Box 0 0 0 16 9 16; $bigBox = New-Box 0 0 0 16 16 16
foreach ($kv in $colorNames.GetEnumerator()) {
    $col = $kv.Key
    $id = "lepton:${col}_stool"
    $perms = @(
        [ordered]@{ condition = (Get-StateCond 'lepton:big' $false); components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $smallBox; 'minecraft:selection_box' = ConvertTo-BoxJson $smallBox } },
        [ordered]@{ condition = (Get-StateCond 'lepton:big' $true); components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $bigBox; 'minecraft:selection_box' = ConvertTo-BoxJson $bigBox } }
    )
    Add-StorageBlock -Id $id -Name @{ en = "$($kv.Value[0]) Stool"; zh = "$($kv.Value[1])凳子" } -Hardness 0.2 -Sound wood -MapColor $mapColors[$col] `
        -Category items -Group 'minecraft:itemGroup.name.bed' -Tags 'lepton:stool', 'lepton:storage_wooden' -Gate 'enableStools' -PassLight -Waterlog `
        -Geometry ([ordered]@{ identifier = $stoolGeo; bone_visibility = [ordered]@{ small = (Get-StateCond 'lepton:big' $false); big = (Get-StateCond 'lepton:big' $true) } }) `
        -Material ([ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture (T "${col}_stool")); render_method = 'alpha_test' } }) `
        -States ([ordered]@{ 'lepton:big' = @($false, $true); 'lepton:sat_in' = @($false, $true) }) -Permutations $perms
    Add-Shaped "storage_${col}_stool" @('###', 'WWW') @{ '#' = "minecraft:${col}_wool"; W = '#minecraft:wooden_slabs' } $id 4 @('enableStools')
}
Add-PackText 'entity.lepton:stool.name' @{ en = 'Stool'; zh = '凳子' }

# ==================================================================================================
# Rope
# ==================================================================================================
$ropeGeo = Write-StorageGeometry 'rope' ([ordered]@{
        rope = @(New-UvCube (New-Box 6 0 6 10 16 10) @{ north = @(6, 0, 4, 16); south = @(6, 0, 4, 16); east = @(6, 0, 4, 16); west = @(6, 0, 4, 16); up = @(0, 0, 4, 4); down = @(0, 0, 4, 4) })
    })
Add-StorageBlock -Id 'lepton:rope' -Name @{ en = 'Rope Coil'; zh = '绳卷' } -Hardness 0.5 -Sound cloth -MapColor '#664c33' -Flame 60, 30 `
    -Tags 'lepton:rope' -Gate 'enableRope' -PassLight -Waterlog -Hidden -Geometry $ropeGeo `
    -Material ([ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture (T 'rope')); render_method = 'alpha_test' } }) `
    -Collision $false -Selection (ConvertTo-BoxJson (New-Box 6 0 6 10 16 10))
Add-StorageItem 'lepton:rope' @{ en = 'Rope Coil'; zh = '绳卷' } 'textures/items/storage/rope' -Category items -Block 'lepton:rope' -Gate 'enableRope'
Add-Shaped 'storage_rope' @('##', '##', '##') @{ '#' = 'minecraft:string' } 'lepton:rope' 3 @('enableRope')

# ==================================================================================================
# ShearVines: cut vine (vanilla vine texture, foliage tint)
# ==================================================================================================
$vineBoxes = [ordered]@{
    north = New-Box 0 0 0.8 16 16 0.8; south = New-Box 0 0 15.2 16 16 15.2; west = New-Box 0.8 0 0 0.8 16 16; east = New-Box 15.2 0 0 15.2 16 16
    up = New-Box 0 15.2 0 16 15.2 16
}
$vineBones = [ordered]@{}
foreach ($kv in $vineBoxes.GetEnumerator()) {
    $b = $kv.Value; $full = @(0, 0, 16, 16)
    $uv = if ($kv.Key -in 'north', 'south') { @{ north = $full; south = $full } } elseif ($kv.Key -eq 'up') { @{ up = $full; down = $full } } else { @{ east = $full; west = $full } }
    $vineBones[$kv.Key] = @(New-UvCube $b $uv)
}
$vineGeo = Write-StorageGeometry 'cut_vine' $vineBones
$vineVis = [ordered]@{}; foreach ($d in $vineBoxes.Keys) { $vineVis[$d] = Get-StateCond "lepton:$d" $true }
$vinePerms = @()
$dirs5 = @('north', 'south', 'west', 'east', 'up')
for ($mask = 0; $mask -lt 32; $mask++) {
    $on = @(for ($i = 0; $i -lt 5; $i++) { if ($mask -band (1 -shl $i)) { $dirs5[$i] } })
    $cond = Join-And @(for ($i = 0; $i -lt 5; $i++) { Get-StateCond "lepton:$($dirs5[$i])" ([bool]($mask -band (1 -shl $i))) })
    $boxes = @(foreach ($d in $on) { $b = $vineBoxes[$d]
            New-Box ([Math]::Max(0, [Math]::Min($b.X0, 15))) ([Math]::Max(0, [Math]::Min($b.Y0, 15))) ([Math]::Max(0, [Math]::Min($b.Z0, 15))) `
            ([Math]::Min(16, [Math]::Max($b.X1, 1))) ([Math]::Min(16, [Math]::Max($b.Y1, 1))) ([Math]::Min(16, [Math]::Max($b.Z1, 1))) })
    $sel = if ($boxes.Count) { Get-BoundingBox $boxes } else { New-Box 0 0 0 16 16 16 }
    $vinePerms += [ordered]@{ condition = $cond; components = [ordered]@{ 'minecraft:selection_box' = ConvertTo-BoxJson $sel } }
}
$vineStates = [ordered]@{}; foreach ($d in $dirs5) { $vineStates["lepton:$d"] = @($false, $true) }
Write-PackJson (Join-Path $script:BP 'loot_tables/blocks/cut_vine.json') ([ordered]@{ pools = @() })
$cutVineLoot = 'loot_tables/blocks/cut_vine.json'
$vineMat = [ordered]@{ texture = (Add-TerrainTexture 'textures/blocks/vine'); render_method = 'alpha_test'; tint_method = 'default_foliage'; ambient_occlusion = $false }
Add-StorageBlock -Id 'lepton:cut_vine' -Name @{ en = 'Cut Vines'; zh = '已剪切的藤蔓' } -Hardness 0.2 -Sound grass -MapColor '#007c00' -Flame 0, 300 `
    -Tags 'lepton:cut_vine' -Gate 'enableShearVines' -PassLight -Hidden -Loot $cutVineLoot `
    -Geometry ([ordered]@{ identifier = $vineGeo; bone_visibility = $vineVis }) -Material ([ordered]@{ '*' = $vineMat }) -Collision $false `
    -States $vineStates -Permutations $vinePerms

# ==================================================================================================
# MorePottedPlants
# ==================================================================================================
$potBoxes = @((New-Box 5 0 5 6 6 11), (New-Box 10 0 5 11 6 11), (New-Box 6 0 5 10 6 6), (New-Box 6 0 10 10 6 11))
$potCubes = @(foreach ($b in $potBoxes) { New-UvCube $b (Get-LockedUv $b) '*' })
$dirtBox = New-Box 6 0 6 10 4 10
$potCubes += New-UvCube $dirtBox @{ up = @(6, 6, 4, 4) } 'dirt'
$crossUv = @{ north = @(0, 0, 16, 16); south = @(0, 0, 16, 16) }
$cross = @(
    (New-UvCube (New-Box 2.6 4 8 13.4 16 8) $crossUv 'plant' @(0, 45, 0) @(0, 8, 0)),
    (New-UvCube (New-Box 2.6 4 8 13.4 16 8) $crossUv 'plant' @(0, -45, 0) @(0, 8, 0))
)
$potGeo = Write-StorageGeometry 'potted' ([ordered]@{ pot = $potCubes; plant = $cross })
$potBox = New-Box 5 0 5 11 6 11
# block, plant item, en, zh, texture, tint, light
$potted = @(
    @('beetroot', 'minecraft:beetroot_seeds', 'Potted Beetroots', '甜菜根盆栽', 'textures/blocks/beetroots_stage_3', $null, 0),
    @('berries', 'minecraft:sweet_berries', 'Potted Sweet Berry Bush', '甜浆果盆栽', 'textures/blocks/sweet_berry_bush_stage3', $null, 0),
    @('carrot', 'minecraft:carrot', 'Potted Carrots', '胡萝卜盆栽', 'textures/blocks/carrots_stage_3', $null, 0),
    @('chorus', 'minecraft:chorus_flower', 'Potted Chorus Tree', '紫颂花盆栽', (T 'potted_chorus'), $null, 0),
    @('cocoa_bean', 'minecraft:cocoa_beans', 'Potted Cocoa', '可可豆盆栽', (T 'potted_cocoa_bean'), $null, 0),
    @('short_grass', 'minecraft:short_grass', 'Potted Grass', '草盆栽', 'textures/blocks/tallgrass', 'grass', 0),
    @('peony', 'minecraft:peony', 'Potted Peony', '牡丹盆栽', 'textures/blocks/double_plant_paeonia_top', $null, 0),
    @('large_fern', 'minecraft:large_fern', 'Potted Large Fern', '大型蕨盆栽', 'textures/blocks/double_plant_fern_top', 'grass', 0),
    @('lilac', 'minecraft:lilac', 'Potted Lilac', '丁香盆栽', 'textures/blocks/double_plant_syringa_top', $null, 0),
    @('melon', 'minecraft:melon_seeds', 'Potted Melon', '西瓜盆栽', (T 'potted_melon'), $null, 0),
    @('nether_sprouts', 'minecraft:nether_sprouts', 'Potted Nether Sprouts', '下界苗盆栽', (T 'potted_nether_sprouts'), $null, 0),
    @('nether_wart', 'minecraft:nether_wart', 'Potted Nether Wart', '下界疣盆栽', 'textures/blocks/nether_wart_stage_2', $null, 0),
    @('potato', 'minecraft:potato', 'Potted Potatoes', '马铃薯盆栽', 'textures/blocks/potatoes_stage_3', $null, 0),
    @('pumpkin', 'minecraft:pumpkin_seeds', 'Potted Pumpkin', '南瓜盆栽', (T 'potted_pumpkin'), $null, 0),
    @('rose', 'minecraft:rose_bush', 'Potted Rose', '玫瑰盆栽', 'textures/blocks/double_plant_rose_top', $null, 0),
    @('sea_pickle', 'minecraft:sea_pickle', 'Potted Sea Pickle', '海泡菜盆栽', (T 'potted_sea_pickle'), $null, 3),
    @('sugar_cane', 'minecraft:sugar_cane', 'Potted Sugar Cane', '甘蔗盆栽', (T 'potted_sugar_cane'), $null, 0),
    @('sunflower', 'minecraft:sunflower', 'Potted Sunflower', '向日葵盆栽', (T 'potted_sunflower'), $null, 0),
    @('tall_grass', 'minecraft:tall_grass', 'Potted Tall Grass', '高草丛盆栽', 'textures/blocks/double_plant_grass_top', 'grass', 0),
    @('twisting_vines', 'minecraft:twisting_vines', 'Potted Twisting Vines', '缠怨藤盆栽', (T 'potted_twisting_vines'), $null, 0),
    @('vine', 'minecraft:vine', 'Potted Vines', '藤蔓盆栽', (T 'potted_vine'), $null, 0),
    @('weeping_vines', 'minecraft:weeping_vines', 'Potted Weeping Vines', '垂泪藤盆栽', (T 'potted_weeping_vines'), $null, 0),
    @('wheat', 'minecraft:wheat_seeds', 'Potted Wheat', '小麦盆栽', 'textures/blocks/wheat_stage_7', $null, 0),
    @('cave_vines', 'minecraft:glow_berries', 'Potted Cave Vines', '洞穴藤蔓盆栽', (T 'potted_cave_vines'), $null, 14),
    @('pitcher_plant', 'minecraft:pitcher_plant', 'Potted Pitcher Plant', '瓶子草盆栽', (T 'potted_pitcher_plant'), $null, 0)
)
$potTex = Add-TerrainTexture 'textures/blocks/flower_pot'
$dirtTex = Add-TerrainTexture 'textures/blocks/dirt'
$pottedList = @()
foreach ($p in $potted) {
    $id = "lepton:potted_$($p[0])"
    $plant = [ordered]@{ texture = (Add-TerrainTexture $p[4]); render_method = 'alpha_test'; face_dimming = $false; ambient_occlusion = $false }
    if ($p[5]) { $plant.tint_method = $p[5] }
    $mat = [ordered]@{ '*' = [ordered]@{ texture = $potTex; render_method = 'alpha_test' }; dirt = [ordered]@{ texture = $dirtTex; render_method = 'alpha_test' }; plant = $plant }
    $loot = "loot_tables/blocks/potted_$($p[0]).json"
    Write-PackJson (Join-Path $script:BP $loot) ([ordered]@{ pools = @(
                [ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = 'minecraft:flower_pot'; weight = 1 }) },
                [ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = $p[1]; weight = 1 }) }) })
    Add-StorageBlock -Id $id -Name @{ en = $p[2]; zh = $p[3] } -Hardness 0 -Resistance 0 -Sound stone -Light $p[6] -Tags 'lepton:potted' `
        -Gate 'enableMorePottedPlants' -PassLight -Hidden -Loot $loot -Geometry $potGeo -Material $mat `
        -Collision (ConvertTo-BoxJson $potBox) -Selection (ConvertTo-BoxJson $potBox)
    $pottedList += "    {`"$id`", `"$($p[1])`"},"
}

# ==================================================================================================
# GlassShard
# ==================================================================================================
$GS = 'enableGlassShard'
Add-StorageItem 'lepton:clear_shard' @{ en = 'Glass Shard'; zh = '玻璃碎片' } 'textures/items/storage/clear_shard' -ItemTags 'lepton:shards' -Gate $GS
Add-StorageItem 'lepton:dirty_shard' @{ en = 'Dirty Glass Shard'; zh = '脏玻璃碎片' } 'textures/items/storage/dirty_shard' -ItemTags 'lepton:shards' -Gate $GS
foreach ($kv in $colorNames.GetEnumerator()) {
    Add-StorageItem "lepton:$($kv.Key)_shard" @{ en = "$($kv.Value[0]) Glass Shard"; zh = "$($kv.Value[1])玻璃碎片" } "textures/items/storage/$($kv.Key)_shard" -ItemTags 'lepton:shards' -Gate $GS
}
Add-StorageBlock -Id 'lepton:dirty_glass' -Name @{ en = 'Dirty Glass'; zh = '脏玻璃' } -Textures (T 'dirty_glass') -Hardness 0.3 -Sound glass `
    -MapColor '#664c33' -RenderMethod blend -PassLight -Category construction -Group 'minecraft:itemGroup.name.glass' -Tags 'lepton:dirty_glass' -Gate $GS
$null = New-ShapeSet -Material 'dirty_glass' -BaseBlock 'lepton:dirty_glass' -Shapes pane -Textures (T 'dirty_glass') -Sound glass -Hardness 0.3 `
    -Resistance 0.3 -MapColor '#664c33' -RenderMethod blend -Name @{ en = 'Dirty Glass'; zh = '脏玻璃' } -NoCrafting
Set-Gate 'lepton:dirty_glass_pane' $GS
Add-Shaped 'storage_dirty_glass_pane' @('###', '###') @{ '#' = 'lepton:dirty_glass' } 'lepton:dirty_glass_pane' 16 @($GS)
Add-Shaped 'storage_clear_glass' @('##', '##') @{ '#' = 'lepton:clear_shard' } 'minecraft:glass' 1 @($GS)
Add-Shaped 'storage_dirty_glass' @('##', '##') @{ '#' = 'lepton:dirty_shard' } 'lepton:dirty_glass' 1 @($GS)
foreach ($kv in $colorNames.GetEnumerator()) {
    Add-Shaped "storage_$($kv.Key)_glass" @('##', '##') @{ '#' = "lepton:$($kv.Key)_shard" } "minecraft:$($kv.Key)_stained_glass" 1 @($GS)
}
# mixed shards: any four -> dirty glass (after the single-colour recipes)
Add-Shaped 'storage_mixed_dirty_glass' @('##', '##') @{ '#' = '#lepton:shards' } 'lepton:dirty_glass' 1 @($GS) -Priority 1
Add-Smelting 'storage_clean_glass' 'lepton:dirty_glass' 'minecraft:glass' 1 @('furnace') @($GS)

# ==================================================================================================
# UtilityRecipes
# ==================================================================================================
$UR = 'enableUtilityRecipes'
# better stone tools (Bedrock's own stone tools already take cobblestone, cobbled deepslate and blackstone)
$stoneMats = [ordered]@{
    stone = 'minecraft:stone'; granite = 'minecraft:granite'; diorite = 'minecraft:diorite'; andesite = 'minecraft:andesite'; deepslate = 'minecraft:deepslate'
    tuff = 'minecraft:tuff'; mossy_cobblestone = 'minecraft:mossy_cobblestone'; flint = 'minecraft:flint'; sandstone = 'minecraft:sandstone'; calcite = 'minecraft:calcite'
    limestone = 'lepton:limestone'; jasper = 'lepton:jasper'; shale = 'lepton:shale'; permafrost = 'lepton:permafrost'
}
$tools = [ordered]@{
    sword = @(@('#', '#', 'S'), 'minecraft:stone_sword'); shovel = @(@('#', 'S', 'S'), 'minecraft:stone_shovel')
    pickaxe = @(@('###', ' S ', ' S '), 'minecraft:stone_pickaxe'); axe = @(@('##', '#S', ' S'), 'minecraft:stone_axe'); hoe = @(@('##', ' S', ' S'), 'minecraft:stone_hoe')
}
foreach ($m in $stoneMats.GetEnumerator()) { foreach ($t in $tools.GetEnumerator()) {
        Add-Shaped "storage_stone_$($t.Key)_from_$($m.Key)" $t.Value[0] @{ '#' = $m.Value; S = 'minecraft:stick' } $t.Value[1] 1 @($UR, 'betterStoneToolCrafting') } }

Add-Shapeless 'storage_dispenser_bow' @('minecraft:dropper', 'minecraft:bow') 'minecraft:dispenser' 1 @($UR, 'enableDispenser')
Add-Shaped 'storage_dispenser_no_bow' @(' #X', '#DX', ' #X') @{ '#' = 'minecraft:stick'; X = 'minecraft:string'; D = 'minecraft:dropper' } 'minecraft:dispenser' 1 @($UR, 'enableDispenser')
Add-Shaped 'storage_repeater' @('X X', '#X#', 'III') @{ X = 'minecraft:redstone'; '#' = 'minecraft:stick'; I = 'minecraft:stone' } 'minecraft:repeater' 1 @($UR, 'enableRepeater')
Add-Shaped 'storage_chest_minecart' @('#C#', '###') @{ '#' = 'minecraft:iron_ingot'; C = 'minecraft:chest' } 'minecraft:chest_minecart' 1 @($UR, 'enableMinecarts')
Add-Shaped 'storage_chest_minecart_trapped' @('#C#', '###') @{ '#' = 'minecraft:iron_ingot'; C = 'minecraft:trapped_chest' } 'minecraft:chest_minecart' 1 @($UR, 'enableMinecarts')
Add-Shaped 'storage_hopper_minecart' @('#X#', '###') @{ '#' = 'minecraft:iron_ingot'; X = 'minecraft:hopper' } 'minecraft:hopper_minecart' 1 @($UR, 'enableMinecarts')
Add-Shaped 'storage_tnt_minecart' @('#X#', '###') @{ '#' = 'minecraft:iron_ingot'; X = 'minecraft:tnt' } 'minecraft:tnt_minecart' 1 @($UR, 'enableMinecarts')
foreach ($w in 'oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'mangrove', 'cherry', 'pale_oak') {
    Add-Shaped "storage_direct_${w}_chest_boat" @('#X#', '###') @{ '#' = "minecraft:${w}_planks"; X = 'minecraft:chest' } "minecraft:${w}_chest_boat" 1 @($UR, 'enableChestBoats')
}
Add-Shaped 'storage_direct_bamboo_chest_raft' @('#X#', '###') @{ '#' = 'minecraft:bamboo_planks'; X = 'minecraft:chest' } 'minecraft:bamboo_chest_raft' 1 @($UR, 'enableChestBoats')
Add-Shaped 'storage_chests_from_logs' @('###', '# #', '###') @{ '#' = '#minecraft:logs' } 'minecraft:chest' 4 @($UR, 'logsToChests') -Priority 1
foreach ($p in @(@('brain', 'pink'), @('bubble', 'magenta'), @('fire', 'red'), @('horn', 'yellow'), @('tube', 'blue'))) {
    Add-Shapeless "storage_$($p[0])_coral_to_$($p[1])_dye" @("minecraft:$($p[0])_coral") "minecraft:$($p[1])_dye" 1 @($UR, 'coralToDye')
    Add-Shapeless "storage_$($p[0])_coral_fan_to_$($p[1])_dye" @("minecraft:$($p[0])_coral_fan") "minecraft:$($p[1])_dye" 1 @($UR, 'coralToDye')
}
Add-Shaped 'storage_bent_bread' @('##', '# ') @{ '#' = 'minecraft:wheat' } 'minecraft:bread' 1 @($UR, 'bentRecipes')
Add-Shaped 'storage_bent_cookie' @('X#', '# ') @{ '#' = 'minecraft:wheat'; X = 'minecraft:cocoa_beans' } 'minecraft:cookie' 8 @($UR, 'bentRecipes')
Add-Shaped 'storage_bent_paper' @('##', '# ') @{ '#' = 'minecraft:sugar_cane' } 'minecraft:paper' 3 @($UR, 'bentRecipes')
Add-Smelting 'storage_bone_meal_utility' 'minecraft:bone' 'minecraft:bone_meal' 3 @('furnace') @($UR, 'boneMealUtility')
Add-Shapeless 'storage_charcoal_to_black_dye' @('minecraft:charcoal') 'minecraft:black_dye' 1 @($UR, 'charcoalToBlackDye')
Add-Shaped 'storage_easy_hopper' @('IWI', 'IWI', ' I ') @{ I = 'minecraft:iron_ingot'; W = '#minecraft:logs' } 'minecraft:hopper' 1 @($UR, 'easyHopper')
Add-Shaped 'storage_easy_sticks' @('#', '#') @{ '#' = '#minecraft:logs' } 'minecraft:stick' 16 @($UR, 'easySticks')
Add-Shaped 'storage_easy_sticks_bamboo' @('#', '#') @{ '#' = 'minecraft:bamboo_block' } 'minecraft:stick' 8 @($UR, 'easySticks')
Add-Shaped 'storage_easy_sticks_stripped_bamboo' @('#', '#') @{ '#' = 'minecraft:stripped_bamboo_block' } 'minecraft:stick' 8 @($UR, 'easySticks')
foreach ($m in 'iron', 'copper', 'gold') {
    Add-Smelting "storage_raw_${m}_block_smelt" "minecraft:raw_${m}_block" "minecraft:${m}_block" 1 @('furnace') @($UR, 'smeltRawOreBlocks')
    Add-Smelting "storage_raw_${m}_block_blast" "minecraft:raw_${m}_block" "minecraft:${m}_block" 1 @('blast_furnace') @($UR, 'smeltRawOreBlocks')
}

Complete-PackUnit

# ---- Content.inc --------------------------------------------------------------------------------
$lines = @(
    '// Generated by tools/pack/storage.ps1 - do not edit.',
    '// Every block/item and recipe id of U8 with the config keys that must all be on for it.',
    'inline constexpr GateEntry kContentGates[] = {'
)
foreach ($kv in $script:Gates.GetEnumerator()) { $lines += "    {`"$($kv.Key)`", `"$($kv.Value)`"}," }
$lines += '};'
$lines += 'inline constexpr GateEntry kRecipeGates[] = {'
foreach ($kv in $script:RecipeGates.GetEnumerator()) { $lines += "    {`"$($kv.Key)`", `"$($kv.Value)`"}," }
$lines += '};'
$lines += '// Potted plants: block id, plant item.'
$lines += 'inline constexpr PottedEntry kPotted[] = {'
$lines += $pottedList
$lines += '};'
$inc = Join-Path $script:RepoRoot 'src/feature/storage/Content.inc'
New-Item -ItemType Directory -Force (Split-Path $inc) | Out-Null
[IO.File]::WriteAllText($inc, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
Write-Host "[pack] storage: $($script:Gates.Count) gated ids, $($script:RecipeGates.Count) recipes"
