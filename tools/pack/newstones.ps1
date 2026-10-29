# U3 newstones: NewStoneTypes (+ the MoreStoneVariants blocks of the new stones), Permafrost
# (docs/spec/newstones.md).
#
#   pwsh -NoProfile -File tools/pack/newstones.ps1     # regenerates and overwrites the committed output
#
# Besides the pack files it writes src/feature/newstones/Content.inc: every block and recipe id of
# the unit with its gate (the config keys that must be on) and the myalite-family (tinted) blocks,
# read by src/feature/newstones/NewStones.cpp.
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/newstones.ps1"

Start-PackUnit 'newstones'

$texDir = Join-Path $script:RP 'textures/blocks/newstones'
foreach ($p in (Write-NewStonesTextures $texDir)) { $null = Add-PackFile $p }
function T([string]$Name) { return "textures/blocks/newstones/$Name" }
# Myalite family: the default (item) tint step and the texture of a step.
$script:DefaultTint = 8
function TM([string]$Name, [int]$Tint = $script:DefaultTint) { return "textures/blocks/newstones/myalite/${Name}_t$Tint" }

# ---- gates --------------------------------------------------------------------------------------
# Gate names (C++ G_ bits): limestone jasper shale myalite permafrost msv bricks chiseled pillar vslab
$script:BlockGates = [System.Collections.Generic.SortedDictionary[string, string[]]]::new([StringComparer]::Ordinal)
$script:Tinted = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)   # id -> texture name ('pillar' = side/top)
function Set-Gate([string]$Id, [string[]]$Gates) { $script:BlockGates[$Id] = $Gates }
function Set-SetGates($Ids, [string[]]$Gates) {
    foreach ($kv in $Ids.GetEnumerator()) {
        if ($kv.Value -notlike 'lepton:*' -or -not (Test-Path (Join-Path $script:BP "blocks/$(Get-Path $kv.Value).json"))) { continue }
        $g = @($Gates); if ($kv.Key -eq 'vertical_slab') { $g += 'vslab' }
        Set-Gate $kv.Value $g
    }
}
function Set-Tinted($Ids, [string]$Texture) {
    foreach ($kv in $Ids.GetEnumerator()) {
        if ($kv.Value -notlike 'lepton:*' -or -not (Test-Path (Join-Path $script:BP "blocks/$(Get-Path $kv.Value).json"))) { continue }
        $script:Tinted[$kv.Value] = $Texture
    }
}

$Pick = @{ Tool = 'pickaxe' }
$zhShape = @{ stairs = '楼梯'; slab = '台阶'; wall = '墙'; vertical_slab = '竖直台阶' }
# Shape names: "<Prefix> Stairs" etc. (Quark: "Limestone Brick Stairs" for limestone_bricks_stairs).
function Get-ShapeNames([string]$En, [string]$Zh) {
    $n = @{}
    foreach ($s in 'stairs', 'slab', 'wall', 'vertical_slab') {
        $en2 = switch ($s) { 'stairs' { 'Stairs' } 'slab' { 'Slab' } 'wall' { 'Wall' } 'vertical_slab' { 'Vertical Slab' } }
        $n[$s] = @{ en = "$En $en2"; zh = "$Zh$($zhShape[$s])" }
    }
    return $n
}
function Write-Cut([string]$From, [string]$To, [int]$Count) {
    Write-ShapelessRecipe "$(Get-Path $To)_from_$(Get-Path $From)_stonecutting" @($From) $To $Count 'stonecutter'
}

$stoneGroup = 'minecraft:itemGroup.name.stone'
$brickGroup = 'minecraft:itemGroup.name.stoneBrick'

# ---- NewStoneTypes + their MoreStoneVariants blocks ------------------------------------------------
$stones = [ordered]@{
    limestone = @{ en = 'Limestone'; zh = '石灰石'; color = '#707070' }
    jasper    = @{ en = 'Jasper'; zh = '碧玉石'; color = '#8e3c2e' }
    shale     = @{ en = 'Shale'; zh = '页岩'; color = '#a0a0ff' }
    myalite   = @{ en = 'Myalite'; zh = '幻境石'; color = '#7f3fb2' }
}
foreach ($s in $stones.Keys) {
    $v = $stones[$s]
    $my = $s -eq 'myalite'
    $tex = { param($name) if ($my) { TM $name } else { T $name } }
    $common = @{ Sound = 'stone'; Hardness = 1.5; Resistance = 6; MapColor = $v.color }
    $raw = "lepton:$s"; $pol = "lepton:polished_$s"
    $polEn = "Polished $($v.en)"; $polZh = "磨制$($v.zh)"

    # raw stone + stairs, slab, wall, vertical slab
    New-FullBlock -Id $raw -Textures (& $tex $s) -Name @{ en = $v.en; zh = $v.zh } @common @Pick -Group $stoneGroup
    Set-Gate $raw @($s)
    $ids = New-ShapeSet -Material $s -BaseBlock $raw -Shapes stairs, slab, wall, vertical_slab -Textures (& $tex $s) `
        -Name @{ en = $v.en; zh = $v.zh } -Names (Get-ShapeNames $v.en $v.zh) @common @Pick -Stonecutter
    Set-SetGates $ids @($s)
    if ($my) { Set-Tinted $ids $s; $script:Tinted[$raw] = $s }

    # polished stone + stairs, slab, vertical slab
    New-FullBlock -Id $pol -Textures (& $tex "polished_$s") -Name @{ en = $polEn; zh = $polZh } @common @Pick -Group $stoneGroup
    Set-Gate $pol @($s)
    $ids = New-ShapeSet -Material "polished_$s" -BaseBlock $pol -Shapes stairs, slab, vertical_slab -Textures (& $tex "polished_$s") `
        -Name @{ en = $polEn; zh = $polZh } -Names (Get-ShapeNames $polEn $polZh) @common @Pick -Stonecutter
    Set-SetGates $ids @($s)
    if ($my) { Set-Tinted $ids "polished_$s"; $script:Tinted[$pol] = "polished_$s" }
    Write-ShapedRecipe "polished_$s" @('##', '##') @{ '#' = $raw } $pol 4
    Write-Cut $raw $pol 1
    if (-not $my) {
        # Quark has no raw myalite -> polished myalite shape recipes
        Write-Cut $raw "lepton:polished_${s}_stairs" 1
        Write-Cut $raw "lepton:polished_${s}_slab" 2
        Write-Cut $raw "lepton:polished_${s}_vertical_slab" 2
    }

    # bricks (+ shapes), chiseled bricks, pillar (Quark MoreStoneVariants)
    $bricks = "lepton:${s}_bricks"
    $brEn = "$($v.en) Bricks"; $brZh = "$($v.zh)砖"
    New-FullBlock -Id $bricks -Textures (& $tex "${s}_bricks") -Name @{ en = $brEn; zh = $brZh } @common @Pick -Group $brickGroup
    Set-Gate $bricks @($s, 'msv', 'bricks')
    $ids = New-ShapeSet -Material "${s}_bricks" -BaseBlock $bricks -Shapes stairs, slab, wall, vertical_slab -Textures (& $tex "${s}_bricks") `
        -Name @{ en = $brEn; zh = $brZh } -Names (Get-ShapeNames "$($v.en) Brick" "$($v.zh)砖") @common @Pick -Stonecutter
    Set-SetGates $ids @($s, 'msv', 'bricks')
    if ($my) { Set-Tinted $ids "${s}_bricks"; $script:Tinted[$bricks] = "${s}_bricks" }
    $chiseled = "lepton:chiseled_${s}_bricks"
    New-FullBlock -Id $chiseled -Textures (& $tex "chiseled_${s}_bricks") -Name @{ en = "Chiseled $brEn"; zh = "雕纹$brZh" } @common @Pick -Group $brickGroup
    Set-Gate $chiseled @($s, 'msv', 'bricks', 'chiseled')
    if ($my) { $script:Tinted[$chiseled] = "chiseled_${s}_bricks" }
    $pillar = "lepton:${s}_pillar"
    New-FullBlock -Id $pillar -Textures @{ side = (& $tex "${s}_pillar"); top = (& $tex "${s}_pillar_top") } `
        -Name @{ en = "$($v.en) Pillar"; zh = "$($v.zh)柱" } @common @Pick -Group $brickGroup -Axis
    Set-Gate $pillar @($s, 'msv', 'pillar')
    if ($my) { $script:Tinted[$pillar] = 'pillar' }

    Write-ShapedRecipe "${s}_bricks" @('##', '##') @{ '#' = $pol } $bricks 4
    Write-ShapedRecipe "chiseled_${s}_bricks" @('#', '#') @{ '#' = "${bricks}_slab" } $chiseled 1
    Write-ShapedRecipe "${s}_pillar" @('#', '#') @{ '#' = "lepton:polished_${s}_slab" } $pillar 1
    $cut = @(@($bricks, 1), @("${bricks}_stairs", 1), @("${bricks}_slab", 2), @("${bricks}_wall", 1), @("${bricks}_vertical_slab", 2))
    foreach ($c in $cut) {
        Write-Cut $raw $c[0] $c[1]
        Write-Cut $pol $c[0] $c[1]
    }
    Write-Cut $raw $chiseled 1
    Write-Cut $raw $pillar 1
}

# ---- Permafrost ---------------------------------------------------------------------------------
$pf = @{ Sound = 'stone'; Hardness = 1.5; Resistance = 10; MapColor = '#6699d8' }
New-FullBlock -Id 'lepton:permafrost' -Textures (T 'permafrost') -Name @{ en = 'Permafrost'; zh = '永冻石' } @pf @Pick -Group $stoneGroup
Set-Gate 'lepton:permafrost' @('permafrost')
$ids = New-ShapeSet -Material 'permafrost' -BaseBlock 'lepton:permafrost' -Shapes stairs, slab, wall, vertical_slab -Textures (T 'permafrost') `
    -Name @{ en = 'Permafrost'; zh = '永冻石' } -Names (Get-ShapeNames 'Permafrost' '永冻石') @pf @Pick -Stonecutter
Set-SetGates $ids @('permafrost')
New-FullBlock -Id 'lepton:permafrost_bricks' -Textures (T 'permafrost_bricks') -Name @{ en = 'Permafrost Bricks'; zh = '永冻石砖' } @pf @Pick -Group $brickGroup
Set-Gate 'lepton:permafrost_bricks' @('permafrost')
$ids = New-ShapeSet -Material 'permafrost_bricks' -BaseBlock 'lepton:permafrost_bricks' -Shapes stairs, slab, wall, vertical_slab -Textures (T 'permafrost_bricks') `
    -Name @{ en = 'Permafrost Bricks'; zh = '永冻石砖' } -Names (Get-ShapeNames 'Permafrost Brick' '永冻石砖') @pf @Pick -Stonecutter
Set-SetGates $ids @('permafrost')
Write-ShapedRecipe 'permafrost_bricks' @('##', '##') @{ '#' = 'lepton:permafrost' } 'lepton:permafrost_bricks' 4
foreach ($c in @(@('lepton:permafrost_bricks', 1), @('lepton:permafrost_bricks_stairs', 1), @('lepton:permafrost_bricks_slab', 2), @('lepton:permafrost_bricks_wall', 1), @('lepton:permafrost_bricks_vertical_slab', 2))) {
    Write-Cut 'lepton:permafrost' $c[0] $c[1]
}

# ---- post-processing of the generated block JSON / loot tables ----------------------------------------
function Edit-PackJson([string]$Path, [scriptblock]$Edit) {
    $j = Get-Content $Path -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    & $Edit $j
    Write-PackJson $Path $j
}
function Get-BlockPath([string]$Id) { Join-Path $script:BP "blocks/$(Get-Path $Id).json" }

# Myalite family: lepton:tint (0..15, default 8) selects the texture tinted with that step's colour;
# NewStones.cpp sets it from the block position (Quark's MyaliteColorLogic).
foreach ($id in $script:Tinted.Keys) {
    $texName = $script:Tinted[$id]
    Edit-PackJson (Get-BlockPath $id) {
        param($j)
        $b = $j['minecraft:block']
        if (-not $b.description.states) { $b.description.states = [ordered]@{} }
        $b.description.states['lepton:tint'] = @(@($script:DefaultTint) + @(0..($script:MyaliteTints - 1) | Where-Object { $_ -ne $script:DefaultTint }))
        $b.components['tag:lepton:myalite_tinted'] = [ordered]@{}
        $perms = [System.Collections.Generic.List[object]]::new()
        if ($b.permutations) { foreach ($p in $b.permutations) { $perms.Add($p) } }
        for ($i = 0; $i -lt $script:MyaliteTints; $i++) {
            $t = if ($texName -eq 'pillar') { @{ side = (TM 'myalite_pillar' $i); top = (TM 'myalite_pillar_top' $i) } } else { TM $texName $i }
            $perms.Add([ordered]@{ condition = "q.block_state('lepton:tint') == $i"; components = [ordered]@{ 'minecraft:material_instances' = (Get-MaterialInstances $t) } })
        }
        $b.permutations = @($perms)
    }
}

# Drops need a pickaxe (Quark requiresCorrectToolForDrops).
foreach ($id in $script:BlockGates.Keys) {
    $cond = [ordered]@{ condition = 'match_tool'; 'minecraft:match_tool_filter_all' = @('minecraft:is_pickaxe') }
    foreach ($suffix in '', '_double') {
        $p = Join-Path $script:BP "loot_tables/blocks/$(Get-Path $id)$suffix.json"
        if (-not (Test-Path $p)) { continue }
        Edit-PackJson $p { param($j) foreach ($pool in $j.pools) { $pool.conditions = @($cond) } }
    }
}

Complete-PackUnit

# ---- Content.inc: block and recipe gates, tinted blocks for NewStones.cpp ------------------------------
$repo = $script:RepoRoot
$recipeGates = [System.Collections.Generic.SortedDictionary[string, string[]]]::new([StringComparer]::Ordinal)
foreach ($rel in Get-Content (Join-Path $PSScriptRoot 'generated/newstones.txt')) {
    if ($rel -notmatch '^assets/pack/bp/recipes/') { continue }
    $j = Get-Content (Join-Path $repo $rel) -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $kind = @($j.Keys | Where-Object { $_ -like 'minecraft:recipe_*' })[0]
    $r = $j[$kind]
    $items = @($r.result.item)
    if ($r.key) { $items += @($r.key.Values | ForEach-Object { $_.item }) }
    if ($r.ingredients) { $items += @($r.ingredients | ForEach-Object { $_.item }) }
    $g = @()
    foreach ($it in $items) { if ($script:BlockGates.ContainsKey($it)) { $g += $script:BlockGates[$it] } }
    if (-not $g.Count) { throw "newstones: recipe $rel has no gated item" }
    $recipeGates[$r.description.identifier] = @($g | Sort-Object -Unique)
}
$fmt = { param($ids) foreach ($kv in $ids.GetEnumerator()) { '    {"' + $kv.Key + '", ' + (($kv.Value | Sort-Object -Unique | ForEach-Object { "G_$_" }) -join ' | ') + '},' } }
$inc = @(
    '// Generated by tools/pack/newstones.ps1 - do not edit. Every block / recipe id of U3 with its gate,'
    '// and the myalite-family blocks that carry lepton:tint.'
    '// clang-format off'
    'inline constexpr GateEntry kBlockGates[] = {'
) + @(& $fmt $script:BlockGates) + @('};', 'inline constexpr GateEntry kRecipeGates[] = {') + @(& $fmt $recipeGates) + @(
    '};', 'inline constexpr std::string_view kTintedBlocks[] = {'
) + @($script:Tinted.Keys | ForEach-Object { '    "' + $_ + '",' }) + @(
    '};', "inline constexpr int kTintSteps   = $($script:MyaliteTints);", "inline constexpr int kDefaultTint = $($script:DefaultTint);", '// clang-format on')
Write-PackText -Path (Join-Path $repo 'src/feature/newstones/Content.inc') -Text ($inc -join "`n") -Shared
Write-Host "[pack] newstones: $($script:BlockGates.Count) blocks ($($script:Tinted.Count) tinted), $($recipeGates.Count) recipes gated"
