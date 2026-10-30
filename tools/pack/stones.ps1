# U2 stones: MoreStoneVariants, RawMetalBricks, SturdyStone, CelebratoryLamps (docs/spec/stones.md).
#
#   pwsh -NoProfile -File tools/pack/stones.ps1     # regenerates and overwrites the committed output
#
# Besides the pack files it writes src/feature/stones/Content.inc: every block and recipe id of the
# unit with its gate (the config keys that must be on), read by src/feature/stones/Stones.cpp.
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/stones.ps1"

Start-PackUnit 'stones'

$texDir = Join-Path $script:RP 'textures/blocks/stones'
foreach ($p in (Write-StonesTextures $texDir)) { $null = Add-PackFile $p }
function T([string]$Name) { return "textures/blocks/stones/$Name" }

# ---- gates --------------------------------------------------------------------------------------
# Gate names (C++ Gate bits): msv bricks chiseled pillar vslab rawmetal smelt sturdy lamps
$script:BlockGates = [System.Collections.Generic.SortedDictionary[string, string[]]]::new([StringComparer]::Ordinal)
function Set-Gate([string]$Id, [string[]]$Gates) { $script:BlockGates[$Id] = $Gates }
function Set-SetGates($Ids, [string[]]$Gates) {
    foreach ($kv in $Ids.GetEnumerator()) {
        if ($kv.Value -notlike 'lepton:*' -or -not (Test-Path (Join-Path $script:BP "blocks/$(Get-Path $kv.Value).json"))) { continue }
        $g = @($Gates); if ($kv.Key -eq 'vertical_slab') { $g += 'vslab' }
        Set-Gate $kv.Value $g
    }
}

$Pick = @{ Tool = 'pickaxe' }
$zhShape = @{ stairs = '楼梯'; slab = '台阶'; wall = '墙'; vertical_slab = '竖直台阶' }
# Shape names: "<Prefix> Stairs" etc. (Quark: "Granite Brick Stairs" for granite_bricks_stairs).
function Get-ShapeNames([string]$En, [string]$Zh) {
    $n = @{}
    foreach ($s in 'stairs', 'slab', 'wall', 'vertical_slab') {
        $en2 = switch ($s) { 'stairs' { 'Stairs' } 'slab' { 'Slab' } 'wall' { 'Wall' } 'vertical_slab' { 'Vertical Slab' } }
        $n[$s] = @{ en = "$En $en2"; zh = "$Zh$($zhShape[$s])" }
    }
    return $n
}

# ---- MoreStoneVariants: polished calcite / dripstone and the raw stones' shapes ---------------------
$stoneGroup = 'minecraft:itemGroup.name.stone'
$brickGroup = 'minecraft:itemGroup.name.stoneBrick'

$vanillaShaped = @(
    @{ key = 'calcite'; raw = 'minecraft:calcite'; rawTex = 'textures/blocks/calcite'; sound = 'calcite'; hard = 0.75; res = 0.75
        color = '#d1b1a1'; en = 'Calcite'; zh = '方解石'; pEn = 'Polished Calcite'; pZh = '磨制方解石'; material = 'calcite' }
    @{ key = 'dripstone'; raw = 'minecraft:dripstone_block'; rawTex = 'textures/blocks/dripstone_block'; sound = 'dripstone_block'; hard = 1.5; res = 1
        color = '#4c2e24'; en = 'Dripstone'; zh = '滴水石'; pEn = 'Polished Dripstone'; pZh = '磨制滴水石'; material = 'dripstone_block' }
)
$polished = @{ granite = 'minecraft:polished_granite'; diorite = 'minecraft:polished_diorite'; andesite = 'minecraft:polished_andesite' }
$polishedSlab = @{ granite = 'minecraft:polished_granite_slab'; diorite = 'minecraft:polished_diorite_slab'; andesite = 'minecraft:polished_andesite_slab' }
$rawStone = @{ granite = 'minecraft:granite'; diorite = 'minecraft:diorite'; andesite = 'minecraft:andesite' }

foreach ($v in $vanillaShaped) {
    $pid_ = "lepton:polished_$($v.key)"
    New-FullBlock -Id $pid_ -Textures (T "polished_$($v.key)") -Name @{ en = $v.pEn; zh = $v.pZh } -Sound $v.sound `
        -Hardness $v.hard -Resistance $v.res -MapColor $v.color @Pick -Group $stoneGroup
    Set-Gate $pid_ @('msv')
    $ids = New-ShapeSet -Material "polished_$($v.key)" -BaseBlock $pid_ -Shapes stairs, slab, vertical_slab -Textures (T "polished_$($v.key)") `
        -Name @{ en = $v.pEn; zh = $v.pZh } -Names (Get-ShapeNames $v.pEn $v.pZh) -Sound $v.sound -Hardness $v.hard -Resistance $v.res `
        -MapColor $v.color @Pick -Stonecutter
    Set-SetGates $ids @('msv')
    $ids = New-ShapeSet -Material $v.material -BaseBlock $v.raw -Shapes stairs, slab, wall, vertical_slab -Textures $v.rawTex `
        -Name @{ en = $v.en; zh = $v.zh } -Names (Get-ShapeNames $v.en $v.zh) -Sound $v.sound -Hardness $v.hard -Resistance $v.res `
        -MapColor $v.color @Pick -Stonecutter
    Set-SetGates $ids @('msv')
    # 2x2 raw -> 4 polished; stonecutter raw -> polished and its shapes
    Write-ShapedRecipe "polished_$($v.key)" @('##', '##') @{ '#' = $v.raw } $pid_ 4
    Write-ShapelessRecipe "polished_$($v.key)_from_$($v.material)_stonecutting" @($v.raw) $pid_ 1 'stonecutter'
    Write-ShapelessRecipe "polished_$($v.key)_stairs_from_$($v.material)_stonecutting" @($v.raw) "lepton:polished_$($v.key)_stairs" 1 'stonecutter'
    Write-ShapelessRecipe "polished_$($v.key)_slab_from_$($v.material)_stonecutting" @($v.raw) "lepton:polished_$($v.key)_slab" 2 'stonecutter'
    Write-ShapelessRecipe "polished_$($v.key)_vertical_slab_from_$($v.material)_stonecutting" @($v.raw) "lepton:polished_$($v.key)_vertical_slab" 2 'stonecutter'
    $polished[$v.key] = $pid_
    $polishedSlab[$v.key] = "lepton:polished_$($v.key)_slab"
    $rawStone[$v.key] = $v.raw
}

# ---- MoreStoneVariants: bricks, chiseled bricks, pillars ------------------------------------------
$variants = [ordered]@{
    granite   = @{ en = 'Granite'; zh = '花岗岩'; color = '#976d4d'; sound = 'stone'; raw = 'granite' }
    diorite   = @{ en = 'Diorite'; zh = '闪长岩'; color = '#fcfcfc'; sound = 'stone'; raw = 'diorite' }
    andesite  = @{ en = 'Andesite'; zh = '安山岩'; color = '#707070'; sound = 'stone'; raw = 'andesite' }
    calcite   = @{ en = 'Calcite'; zh = '方解石'; color = '#d1b1a1'; sound = 'calcite'; raw = 'calcite' }
    dripstone = @{ en = 'Dripstone'; zh = '滴水石'; color = '#4c2e24'; sound = 'dripstone_block'; raw = 'dripstone_block' }
}
foreach ($s in $variants.Keys) {
    $v = $variants[$s]
    $bricks = "lepton:${s}_bricks"
    $common = @{ Sound = $v.sound; Hardness = 1.5; Resistance = 6; MapColor = $v.color }
    New-FullBlock -Id $bricks -Textures (T "${s}_bricks") -Name @{ en = "$($v.en) Bricks"; zh = "$($v.zh)砖" } @common @Pick -Group $brickGroup
    Set-Gate $bricks @('msv', 'bricks')
    $ids = New-ShapeSet -Material "${s}_bricks" -BaseBlock $bricks -Shapes stairs, slab, wall, vertical_slab -Textures (T "${s}_bricks") `
        -Name @{ en = "$($v.en) Bricks"; zh = "$($v.zh)砖" } -Names (Get-ShapeNames "$($v.en) Brick" "$($v.zh)砖") @common @Pick -Stonecutter
    Set-SetGates $ids @('msv', 'bricks')
    $chiseled = "lepton:chiseled_${s}_bricks"
    New-FullBlock -Id $chiseled -Textures (T "chiseled_${s}_bricks") -Name @{ en = "Chiseled $($v.en) Bricks"; zh = "雕纹$($v.zh)砖" } @common @Pick -Group $brickGroup
    Set-Gate $chiseled @('msv', 'bricks', 'chiseled')
    $pillar = "lepton:${s}_pillar"
    New-FullBlock -Id $pillar -Textures @{ side = (T "${s}_pillar"); top = (T "${s}_pillar_top") } -Name @{ en = "$($v.en) Pillar"; zh = "$($v.zh)柱" } `
        @common @Pick -Group $brickGroup -Axis
    Set-Gate $pillar @('msv', 'pillar')

    # crafting
    Write-ShapedRecipe "${s}_bricks" @('##', '##') @{ '#' = $polished[$s] } $bricks 4
    Write-ShapedRecipe "chiseled_${s}_bricks" @('#', '#') @{ '#' = "lepton:${s}_bricks_slab" } $chiseled 1
    Write-ShapedRecipe "${s}_pillar" @('#', '#') @{ '#' = $polishedSlab[$s] } $pillar 1
    # stonecutter from the raw stone and from the polished stone
    $raw = $rawStone[$s]; $rawPath = Get-Path $raw; $polPath = Get-Path $polished[$s]
    $cut = @(@($bricks, 1), @("$bricks`_stairs", 1), @("$bricks`_slab", 2), @("$bricks`_vertical_slab", 2), @("$bricks`_wall", 1))
    foreach ($c in $cut) {
        Write-ShapelessRecipe "$(Get-Path $c[0])_from_${rawPath}_stonecutting" @($raw) $c[0] $c[1] 'stonecutter'
        Write-ShapelessRecipe "$(Get-Path $c[0])_from_${polPath}_stonecutting" @($polished[$s]) $c[0] $c[1] 'stonecutter'
    }
    Write-ShapelessRecipe "chiseled_${s}_bricks_from_${rawPath}_stonecutting" @($raw) $chiseled 1 'stonecutter'
    Write-ShapelessRecipe "${s}_pillar_from_${rawPath}_stonecutting" @($raw) $pillar 1 'stonecutter'
}

# ---- RawMetalBricks ---------------------------------------------------------------------------------
$metals = [ordered]@{
    iron   = @{ en = 'Raw Iron'; zh = '粗铁'; color = '#d8af93'; block = 'minecraft:iron_block'; tier = 'stone' }
    gold   = @{ en = 'Raw Gold'; zh = '粗金'; color = '#f9d849'; block = 'minecraft:gold_block'; tier = 'iron' }
    copper = @{ en = 'Raw Copper'; zh = '粗铜'; color = '#d87f33'; block = 'minecraft:copper_block'; tier = 'stone' }
}
$script:ToolTiers = @{}
foreach ($m in $metals.Keys) {
    $v = $metals[$m]
    $id = "lepton:raw_${m}_bricks"
    $common = @{ Sound = 'stone'; Hardness = 5; Resistance = 6; MapColor = $v.color }
    New-FullBlock -Id $id -Textures (T "raw_${m}_bricks") -Name @{ en = "$($v.en) Bricks"; zh = "$($v.zh)砖" } @common @Pick -Group $brickGroup
    Set-Gate $id @('rawmetal')
    $ids = New-ShapeSet -Material "raw_${m}_bricks" -BaseBlock $id -Shapes stairs, slab, wall, vertical_slab -Textures (T "raw_${m}_bricks") `
        -Name @{ en = "$($v.en) Bricks"; zh = "$($v.zh)砖" } -Names (Get-ShapeNames "$($v.en) Brick" "$($v.zh)砖") @common @Pick -Stonecutter
    Set-SetGates $ids @('rawmetal')
    # Quark's needs_stone_tool / needs_iron_tool tags: block, stairs, slab, vertical slab (not the wall)
    foreach ($t in $id, $ids.stairs, $ids.slab, $ids.vertical_slab) { $script:ToolTiers[$t] = $v.tier }
    Write-ShapedRecipe "raw_${m}_bricks" @('II', 'II') @{ 'I' = "minecraft:raw_${m}_block" } $id 4
    Write-ShapelessRecipe "raw_${m}_bricks_revert" @($id) "minecraft:raw_$m" 9
    foreach ($f in @(@('furnace', 'smelt'), @('blast_furnace', 'blast'))) {
        Write-PackJson (Join-Path $script:BP "recipes/raw_${m}_bricks_$($f[1]).json") ([ordered]@{
                format_version          = $script:RecipeFormat
                'minecraft:recipe_furnace' = [ordered]@{
                    description = [ordered]@{ identifier = "lepton:raw_${m}_bricks_$($f[1])" }
                    tags        = @($f[0])
                    input       = $id
                    output      = $v.block
                }
            })
    }
}

# ---- SturdyStone --------------------------------------------------------------------------------------
New-FullBlock -Id 'lepton:sturdy_stone' -Textures (T 'sturdy_stone') -Name @{ en = 'Sturdy Stone'; zh = '固化石头' } -Sound 'stone' `
    -Hardness 4 -Resistance 10 -MapColor '#707070' @Pick -Group $brickGroup
Set-Gate 'lepton:sturdy_stone' @('sturdy')
Write-ShapedRecipe 'sturdy_stone' @('CCC', 'CCC', 'CCC') @{ 'C' = 'minecraft:cobblestone' } 'lepton:sturdy_stone' 1
Write-ShapelessRecipe 'sturdy_stone_uncompress' @('lepton:sturdy_stone') 'minecraft:cobblestone' 9

# ---- CelebratoryLamps ---------------------------------------------------------------------------------
foreach ($l in @(@('stone_lamp', 'Stone Lamp', '石灯', 'minecraft:stone'), @('stone_brick_lamp', 'Stone Brick Lamp', '石砖灯', 'minecraft:stone_bricks'))) {
    $id = "lepton:$($l[0])"
    New-FullBlock -Id $id -Textures (T $l[0]) -Name @{ en = $l[1]; zh = $l[2] } -Sound 'stone' -Hardness 1.5 -Resistance 6 -MapColor '#707070' @Pick
    Set-Gate $id @('lamps')
    Write-ShapedRecipe $l[0] @('SSS', 'GTG', 'SSS') @{ 'S' = $l[3]; 'G' = 'minecraft:glass'; 'T' = 'minecraft:torch' } $id 3
}

# ---- post-processing of the generated block JSON / loot tables ----------------------------------------
function Edit-PackJson([string]$Path, [scriptblock]$Edit) {
    $j = Get-Content $Path -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    & $Edit $j
    Write-PackJson $Path $j
}
function Get-BlockPath([string]$Id) { Join-Path $script:BP "blocks/$(Get-Path $Id).json" }

# Sturdy stone: pistons can neither push nor pull it (Quark PushReaction.BLOCK).
Edit-PackJson (Get-BlockPath 'lepton:sturdy_stone') {
    param($j)
    $j['minecraft:block'].components['minecraft:movable'] = [ordered]@{ movement_type = 'immovable' }
}

# Lamps: lepton:light (default 15) selects the emitted light; Stones.cpp sets it from lightLevel.
foreach ($id in 'lepton:stone_lamp', 'lepton:stone_brick_lamp') {
    Edit-PackJson (Get-BlockPath $id) {
        param($j)
        $b = $j['minecraft:block']
        $b.description.states = [ordered]@{ 'lepton:light' = @(15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14) }
        $b.components['minecraft:light_emission'] = 15
        $b.components['tag:lepton:celebratory_lamp'] = [ordered]@{}
        $b.permutations = @(0..14 | ForEach-Object { [ordered]@{ condition = "q.block_state('lepton:light') == $_"; components = [ordered]@{ 'minecraft:light_emission' = $_ } } })
    }
}

# Drops need the right tool (Quark requiresCorrectToolForDrops): any pickaxe; raw metal bricks a
# stone-tier pickaxe or better (raw gold: iron tier).
$tierTags = @{
    any   = @()
    stone = @('minecraft:stone_tier', 'minecraft:copper_tier', 'minecraft:iron_tier', 'minecraft:diamond_tier', 'minecraft:netherite_tier')
    iron  = @('minecraft:iron_tier', 'minecraft:diamond_tier', 'minecraft:netherite_tier')
}
foreach ($id in $script:BlockGates.Keys) {
    $tier = if ($script:ToolTiers[$id]) { $script:ToolTiers[$id] } else { 'any' }
    $cond = [ordered]@{ condition = 'match_tool'; 'minecraft:match_tool_filter_all' = @('minecraft:is_pickaxe') }
    if ($tierTags[$tier].Count) { $cond['minecraft:match_tool_filter_any'] = $tierTags[$tier] }
    foreach ($suffix in '', '_double') {
        $p = Join-Path $script:BP "loot_tables/blocks/$(Get-Path $id)$suffix.json"
        if (-not (Test-Path $p)) { continue }
        Edit-PackJson $p { param($j) foreach ($pool in $j.pools) { $pool.conditions = @($cond) } }
    }
}

Complete-PackUnit

# ---- Content.inc: block and recipe gates for Stones.cpp ------------------------------------------------
$repo = $script:RepoRoot
$recipeGates = [System.Collections.Generic.SortedDictionary[string, string[]]]::new([StringComparer]::Ordinal)
foreach ($rel in Get-Content (Join-Path $PSScriptRoot 'generated/stones.txt')) {
    if ($rel -notmatch '^assets/pack/bp/recipes/') { continue }
    $j = Get-Content (Join-Path $repo $rel) -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $kind = @($j.Keys | Where-Object { $_ -like 'minecraft:recipe_*' })[0]
    $r = $j[$kind]
    $items = @()
    if ($kind -eq 'minecraft:recipe_furnace') { $items = @($r.input, $r.output) }
    else {
        $items += $r.result.item
        if ($r.key) { $items += @($r.key.Values | ForEach-Object { $_.item }) }
        if ($r.ingredients) { $items += @($r.ingredients | ForEach-Object { $_.item }) }
    }
    $g = @()
    foreach ($it in $items) { if ($script:BlockGates.ContainsKey($it)) { $g += $script:BlockGates[$it] } }
    if ($kind -eq 'minecraft:recipe_furnace') { $g += 'smelt' }
    if (-not $g.Count) { throw "stones: recipe $rel has no gated item" }
    $recipeGates[$r.description.identifier] = @($g | Sort-Object -Unique)
}
$fmt = { param($ids) foreach ($kv in $ids.GetEnumerator()) { '    {"' + $kv.Key + '", ' + (($kv.Value | Sort-Object -Unique | ForEach-Object { "G_$_" }) -join ' | ') + '},' } }
$inc = @(
    '// Generated by tools/pack/stones.ps1 - do not edit. Every block / recipe id of U2 with its gate.'
    '// clang-format off'
    'inline constexpr GateEntry kBlockGates[] = {'
) + @(& $fmt $script:BlockGates) + @('};', 'inline constexpr GateEntry kRecipeGates[] = {') + @(& $fmt $recipeGates) + @('};', '// clang-format on')
Write-PackText -Path (Join-Path $repo 'src/feature/stones/Content.inc') -Text ($inc -join "`n") -Shared
Write-Host "[pack] stones: $($script:BlockGates.Count) blocks, $($recipeGates.Count) recipes gated"
