# U11 logistics pack content: chute, feeding trough, storage crate, copper pipe, encased copper
# pipe (blocks, geometry, recipes, loot), the pipe item display entity, names and pipe sounds.
#
#   pwsh -NoProfile -File tools/pack/logistics.ps1     # regenerates the committed output
. "$PSScriptRoot/lib.ps1"

Start-PackUnit 'logistics'
& "$PSScriptRoot/../textures/logistics.ps1"
foreach ($png in Get-ChildItem (Join-Path $script:RP 'textures/blocks/logistics') -Filter *.png) { $null = Add-PackFile $png.FullName }

$T = 'textures/blocks/logistics'

# ---- helpers ---------------------------------------------------------------------------------------

# A geometry cube with the given material on every face; -Omit drops faces (open pipe ends).
function New-Cube($Box, [string]$Material = '*', [string[]]$Omit = @(), [hashtable]$Faces = @{}) {
    $c = New-GeoCube $Box
    foreach ($f in @($c.uv.Keys)) {
        if ($Omit -contains $f) { $c.uv.Remove($f); continue }
        $c.uv[$f].material_instance = if ($Faces[$f]) { $Faces[$f] } else { $Material }
    }
    return $c
}

function Write-Geo([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) { [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($kv.Value) } }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{ identifier = "geometry.lepton.$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0) }
                bones       = @($bonesJson)
            })
    }
    Write-PackJson -Path (Join-Path $script:RP "models/blocks/lepton_$Name.geo.json") -Object $geo
    return "geometry.lepton.$Name"
}

function Mat([string]$Texture, [string]$Render = 'alpha_test') { return [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = $Render } }

function Write-Recipe([string]$Name, $Body, [string]$Kind = 'minecraft:recipe_shaped') {
    $body.description = [ordered]@{ identifier = "lepton:$Name" }
    $ordered = [ordered]@{ description = $body.description }
    foreach ($k in $Body.Keys) { if ($k -ne 'description') { $ordered[$k] = $Body[$k] } }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{ format_version = $script:RecipeFormat; $Kind = $ordered })
}

$planks = [ordered]@{ tag = 'minecraft:planks' }

# ---- chute -----------------------------------------------------------------------------------------
$chuteBoxes = @((New-Box 0 10 0 16 16 16), (New-Box 4 4 4 12 10 12), (New-Box 6 0 6 10 4 10))
$faces = @{ up = 'top'; down = 'bottom' }
$chuteGeo = Write-Geo 'logistics_chute' ([ordered]@{ chute = @($chuteBoxes | ForEach-Object { New-Cube $_ 'side' -Faces $faces }) })
$c = Get-BaseComponents @{ Hardness = 2.5; Resistance = 2.5; MapColor = '#8f7748'; Flammable = $true; Tool = 'axe' }
$c['minecraft:geometry'] = $chuteGeo
$c['minecraft:material_instances'] = [ordered]@{
    '*' = (Mat "$T/chute_side"); side = (Mat "$T/chute_side"); top = (Mat "$T/chute_top"); bottom = (Mat "$T/chute_bottom")
}
$c['minecraft:collision_box'] = Get-CollisionJson $chuteBoxes
$c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 16 16)
$c['minecraft:light_dampening'] = 0
$c['minecraft:loot'] = Write-LootTable 'chute' 'lepton:chute'
$c['tag:lepton:logistics'] = [ordered]@{}
Write-BlockJson -Id 'lepton:chute' -Menu ([ordered]@{ category = 'items'; group = 'minecraft:itemGroup.name.redstone' }) `
    -States ([ordered]@{ 'lepton:enabled' = @($true, $false) }) -Components $c
Register-BlockClient 'lepton:chute' 'wood' @{ en = 'Chute'; zh = '溜槽' }
Write-Recipe 'chute' ([ordered]@{
        tags = @('crafting_table'); pattern = @('WWW', 'SWS', ' S ')
        key = [ordered]@{ S = [ordered]@{ item = 'minecraft:stick' }; W = $planks }
        unlock = @([ordered]@{ item = 'minecraft:hopper' }, [ordered]@{ item = 'minecraft:stick' })
        result = [ordered]@{ item = 'lepton:chute'; count = 1 }
    })

# ---- feeding trough --------------------------------------------------------------------------------
$walls = @((New-Box 0 0 0 2 8 16), (New-Box 14 0 0 16 8 16), (New-Box 2 0 0 14 8 2), (New-Box 2 0 14 14 8 16))
$wallFaces = @{ up = 'top'; down = 'bottom' }
$inner = @(
    (New-Cube $walls[0] 'side' -Faces @{ up = 'top'; down = 'bottom'; east = 'inner' }),
    (New-Cube $walls[1] 'side' -Faces @{ up = 'top'; down = 'bottom'; west = 'inner' }),
    (New-Cube $walls[2] 'side' -Faces @{ up = 'top'; down = 'bottom'; south = 'inner' }),
    (New-Cube $walls[3] 'side' -Faces @{ up = 'top'; down = 'bottom'; north = 'inner' })
)
$troughGeo = Write-Geo 'logistics_feeding_trough' ([ordered]@{
        walls       = $inner
        floor_empty = @(New-Cube (New-Box 2 0 2 14 2 14) 'bottom' -Faces @{ up = 'floor' })
        floor_full  = @(New-Cube (New-Box 2 0 2 14 6 14) 'bottom' -Faces @{ up = 'hay'; north = 'hay_side'; south = 'hay_side'; east = 'hay_side'; west = 'hay_side' })
    })
$c = Get-BaseComponents @{ Hardness = 0.6; Resistance = 0.6; MapColor = '#8f7748'; Tool = 'axe' }
$c['minecraft:geometry'] = [ordered]@{
    identifier      = $troughGeo
    bone_visibility = [ordered]@{ floor_empty = "!q.block_state('lepton:full')"; floor_full = "q.block_state('lepton:full')" }
}
$c['minecraft:material_instances'] = [ordered]@{
    '*' = (Mat "$T/trough_side"); side = (Mat "$T/trough_side"); top = (Mat "$T/trough_top"); bottom = (Mat "$T/trough_bottom")
    inner = (Mat "$T/trough_side_inner"); floor = (Mat "$T/trough_bottom"); hay = (Mat 'textures/blocks/hay_block_top'); hay_side = (Mat 'textures/blocks/hay_block_side')
}
$c['minecraft:collision_box'] = Get-CollisionJson (@($walls) + @(New-Box 2 0 2 14 2 14))
$c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 8 16)
$c['minecraft:light_dampening'] = 0
$c['minecraft:loot'] = Write-LootTable 'feeding_trough' 'lepton:feeding_trough'
$c['tag:lepton:logistics'] = [ordered]@{}
$troughPerms = @([ordered]@{
        condition  = "q.block_state('lepton:full')"
        components = [ordered]@{ 'minecraft:collision_box' = Get-CollisionJson (@($walls) + @(New-Box 2 0 2 14 6 14)) }
    })
Write-BlockJson -Id 'lepton:feeding_trough' -Menu ([ordered]@{ category = 'items' }) `
    -States ([ordered]@{ 'lepton:full' = @($false, $true) }) -Components $c -Permutations $troughPerms
Register-BlockClient 'lepton:feeding_trough' 'wood' @{ en = 'Feeding Trough'; zh = '饲料槽' }
Write-Recipe 'feeding_trough' ([ordered]@{
        tags = @('crafting_table'); pattern = @('#W#', '###')
        key = [ordered]@{ '#' = $planks; W = [ordered]@{ item = 'minecraft:wheat' } }
        unlock = @([ordered]@{ item = 'minecraft:wheat' })
        result = [ordered]@{ item = 'lepton:feeding_trough'; count = 1 }
    })

# ---- storage crate ---------------------------------------------------------------------------------
$c = Get-BaseComponents @{ Hardness = 2.5; Resistance = 2.5; MapColor = '#8f7748'; Tool = 'axe' }
$c['minecraft:geometry'] = 'minecraft:geometry.full_block'
$c['minecraft:material_instances'] = [ordered]@{ '*' = (Mat "$T/crate" 'opaque') }
$c['minecraft:loot'] = Write-LootTable 'crate' 'lepton:crate'
$c['tag:lepton:logistics'] = [ordered]@{}
$cratePerms = @([ordered]@{
        condition  = "q.block_state('lepton:open')"
        components = [ordered]@{ 'minecraft:material_instances' = [ordered]@{ '*' = (Mat "$T/crate" 'opaque'); up = (Mat "$T/crate_open" 'opaque') } }
    })
Write-BlockJson -Id 'lepton:crate' -Menu ([ordered]@{ category = 'items' }) `
    -States ([ordered]@{ 'lepton:open' = @($false, $true) }) -Components $c -Permutations $cratePerms
Register-BlockClient 'lepton:crate' 'wood' @{ en = 'Storage Crate'; zh = '板条箱' }
Write-Recipe 'crate' ([ordered]@{
        tags = @('crafting_table'); pattern = @('IWI', 'WCW', 'IWI')
        key = [ordered]@{ C = [ordered]@{ item = 'minecraft:chest' }; I = [ordered]@{ item = 'minecraft:iron_ingot' }; W = $planks }
        unlock = @([ordered]@{ item = 'minecraft:chest' })
        result = [ordered]@{ item = 'lepton:crate'; count = 1 }
    })

# ---- copper pipes ----------------------------------------------------------------------------------
# One int state per face: 0 none, 1 pipe, 2 opening, 3 terminal (Quark's PipeBlockEntity.ConnectionType;
# TERMINAL_OFFSET is drawn as TERMINAL: 6 faces x 5 kinds would exceed the 16 state bits of a block).
$dirs = 'down', 'up', 'north', 'south', 'west', 'east'
$states = [ordered]@{}
foreach ($d in $dirs) { $states["lepton:conn_$d"] = @(0, 1, 2, 3) }

# Boxes in "facing" space: depth d (0 = at the block face) .. along the face's axis.
function Get-FaceBox([string]$Dir, [double]$D0, [double]$D1, [double]$A0, [double]$A1, [double]$B0, [double]$B1) {
    switch ($Dir) {
        'down' { return New-Box $A0 $D0 $B0 $A1 $D1 $B1 }
        'up' { return New-Box $A0 (16 - $D1) $B0 $A1 (16 - $D0) $B1 }
        'north' { return New-Box $A0 $B0 $D0 $A1 $B1 $D1 }
        'south' { return New-Box $A0 $B0 (16 - $D1) $A1 $B1 (16 - $D0) }
        'west' { return New-Box $D0 $B0 $A0 $D1 $B1 $A1 }
        'east' { return New-Box (16 - $D1) $B0 $A0 (16 - $D0) $B1 $A1 }
    }
}
function Get-AxisFaces([string]$Dir) {
    switch ($Dir) { { $_ -in 'down', 'up' } { return @('up', 'down') } { $_ -in 'north', 'south' } { return @('north', 'south') } default { return @('east', 'west') } }
}
function Get-Ring([string]$Dir, [double]$D0, [double]$D1) {
    return @(
        (Get-FaceBox $Dir $D0 $D1 4 12 4 6), (Get-FaceBox $Dir $D0 $D1 4 12 10 12),
        (Get-FaceBox $Dir $D0 $D1 4 6 6 10), (Get-FaceBox $Dir $D0 $D1 10 12 6 10)
    )
}

$center = New-Box 5 5 5 11 11 11
$bones = [ordered]@{ center = @(New-Cube $center 'pipe') }
foreach ($d in $dirs) {
    $bones["arm_$d"] = @(New-Cube (Get-FaceBox $d 0 5 5 11 5 11) 'pipe' -Omit (Get-AxisFaces $d))
    $bones["flare_in_$d"] = @(Get-Ring $d 4 6 | ForEach-Object { New-Cube $_ 'flare' })
    $bones["flare_edge_$d"] = @(Get-Ring $d 0 2 | ForEach-Object { New-Cube $_ 'flare' })
}
$pipeGeo = Write-Geo 'logistics_pipe' $bones
$encBones = [ordered]@{ glass = @(New-Cube (New-Box 0 0 0 16 16 16) 'glass') }
foreach ($kv in $bones.GetEnumerator()) { $encBones[$kv.Key] = $kv.Value }
$encasedGeo = Write-Geo 'logistics_encased_pipe' $encBones

function Get-PipeVisibility([bool]$Encased) {
    $v = [ordered]@{ center = 'true' }
    foreach ($d in $dirs) {
        $s = "q.block_state('lepton:conn_$d')"
        $v["arm_$d"] = if ($Encased) { "$s != 0" } else { "($s == 1 || $s == 3)" }
        $v["flare_in_$d"] = if ($Encased) { 'false' } else { "$s == 2" }
        $v["flare_edge_$d"] = if ($Encased) { "($s == 2 || $s == 3)" } else { "$s == 3" }
    }
    if ($Encased) { $v.glass = 'true' }
    return $v
}

$pipeMats = [ordered]@{ '*' = (Mat "$T/pipe_structure"); pipe = (Mat "$T/pipe_structure"); flare = (Mat "$T/pipe_connectors"); glass = (Mat 'textures/blocks/glass') }
$liquid = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }

# copper pipe: collision follows the solid arms (64 permutations)
$c = Get-BaseComponents @{ Hardness = 3; Resistance = 10; MapColor = '#c0714f'; Tool = 'pickaxe' }
$c['minecraft:geometry'] = [ordered]@{ identifier = $pipeGeo; bone_visibility = (Get-PipeVisibility $false) }
$c['minecraft:material_instances'] = $pipeMats
$c['minecraft:collision_box'] = ConvertTo-BoxJson $center
$c['minecraft:selection_box'] = ConvertTo-BoxJson $center
$c['minecraft:liquid_detection'] = $liquid
$c['minecraft:light_dampening'] = 0
$c['minecraft:loot'] = Write-LootTable 'pipe' 'lepton:pipe'
$c['tag:lepton:pipes'] = [ordered]@{}
$c['tag:lepton:logistics'] = [ordered]@{}
$perms = for ($mask = 1; $mask -lt 64; $mask++) {
    $conds = @(); $boxes = @($center)
    for ($i = 0; $i -lt 6; $i++) {
        $s = "q.block_state('lepton:conn_$($dirs[$i])')"
        if ($mask -band (1 -shl $i)) { $conds += "($s == 1 || $s == 3)"; $boxes += Get-FaceBox $dirs[$i] 0 5 5 11 5 11 }
        else { $conds += "($s == 0 || $s == 2)" }
    }
    [ordered]@{
        condition  = ($conds -join ' && ')
        components = [ordered]@{
            'minecraft:collision_box' = Get-CollisionJson $boxes
            'minecraft:selection_box' = ConvertTo-BoxJson (Get-BoundingBox $boxes)
        }
    }
}
Write-BlockJson -Id 'lepton:pipe' -Menu ([ordered]@{ category = 'items'; group = 'minecraft:itemGroup.name.redstone' }) -States $states -Components $c -Permutations $perms
Register-BlockClient 'lepton:pipe' 'copper' @{ en = 'Copper Pipe'; zh = '铜管道' }

$c = Get-BaseComponents @{ Hardness = 3; Resistance = 10; MapColor = '#c0714f'; Tool = 'pickaxe' }
$c['minecraft:geometry'] = [ordered]@{ identifier = $encasedGeo; bone_visibility = (Get-PipeVisibility $true) }
$c['minecraft:material_instances'] = $pipeMats
$c['minecraft:light_dampening'] = 0
$c['minecraft:loot'] = Write-LootTable 'encased_pipe' 'lepton:encased_pipe'
$c['tag:lepton:pipes'] = [ordered]@{}
$c['tag:lepton:logistics'] = [ordered]@{}
Write-BlockJson -Id 'lepton:encased_pipe' -Menu ([ordered]@{ category = 'items'; group = 'minecraft:itemGroup.name.redstone' }) -States $states -Components $c
Register-BlockClient 'lepton:encased_pipe' 'glass' @{ en = 'Encased Copper Pipe'; zh = '铜管道玻璃箱' }

Write-Recipe 'pipe' ([ordered]@{
        tags = @('crafting_table'); pattern = @('I', 'G', 'I')
        key = [ordered]@{ G = [ordered]@{ item = 'minecraft:glass' }; I = [ordered]@{ item = 'minecraft:copper_ingot' } }
        unlock = @([ordered]@{ item = 'minecraft:copper_ingot' })
        result = [ordered]@{ item = 'lepton:pipe'; count = 6 }
    })
Write-Recipe 'encased_pipe' ([ordered]@{
        tags = @('crafting_table'); ingredients = @([ordered]@{ item = 'lepton:pipe' }, [ordered]@{ item = 'minecraft:glass' })
        unlock = @([ordered]@{ item = 'lepton:pipe' })
        result = [ordered]@{ item = 'lepton:encased_pipe'; count = 1 }
    }) 'minecraft:recipe_shapeless'
Write-Recipe 'encased_pipe_revert' ([ordered]@{
        tags = @('crafting_table'); ingredients = @([ordered]@{ item = 'lepton:encased_pipe' })
        unlock = @([ordered]@{ item = 'lepton:encased_pipe' })
        result = [ordered]@{ item = 'lepton:pipe'; count = 1 }
    }) 'minecraft:recipe_shapeless'

# ---- pipe item display entity ------------------------------------------------------------------------
Write-PackJson (Join-Path $script:BP 'entities/pipe_item.json') ([ordered]@{
        format_version    = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:pipe_item'; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
            components  = [ordered]@{
                'minecraft:type_family'          = [ordered]@{ family = @('lepton_pipe_item', 'inanimate') }
                'minecraft:collision_box'        = [ordered]@{ width = 0.01; height = 0.01 }
                'minecraft:physics'              = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'             = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:damage_sensor'        = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
                'minecraft:health'               = [ordered]@{ value = 1; max = 1 }
                'minecraft:fire_immune'          = [ordered]@{}
                'minecraft:knockback_resistance' = [ordered]@{ value = 1.0 }
                'minecraft:scale'                = [ordered]@{ value = 0.8 }
                'minecraft:nameable'             = [ordered]@{ always_show = $false; allow_name_tag_renaming = $false }
                'minecraft:conditional_bandwidth_optimization' = [ordered]@{ default_values = [ordered]@{ max_optimized_distance = 0.0; max_dropped_ticks = 0; use_motion_prediction_hints = $false } }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'entity/pipe_item.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:pipe_item'
                materials          = [ordered]@{ default = 'entity_alphatest' }
                textures           = [ordered]@{ default = 'textures/blocks/glass' }
                geometry           = [ordered]@{ default = 'geometry.lepton.pipe_item' }
                render_controllers = @('controller.render.default')
                enable_attachables = $true
            }
        }
    })
# The held item is drawn at the rightItem bone; the arm points it forward so it looks like an item
# floating in the pipe.
Write-PackJson (Join-Path $script:RP 'models/entity/lepton_pipe_item.geo.json') ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{ identifier = 'geometry.lepton.pipe_item'; texture_width = 16; texture_height = 16
                    visible_bounds_width = 1; visible_bounds_height = 1; visible_bounds_offset = @(0, 0.25, 0) }
                bones       = @(
                    [ordered]@{ name = 'root'; pivot = @(0, 0, 0) },
                    [ordered]@{ name = 'rightArm'; parent = 'root'; pivot = @(0, -3, 0); rotation = @(-90, 0, 0) },
                    [ordered]@{ name = 'rightItem'; parent = 'rightArm'; pivot = @(0, -3, 0) }
                )
            })
    })

# ---- sounds (original, synthesised) -----------------------------------------------------------------
function Write-Wav([string]$Path, [double]$Seconds, [scriptblock]$Sample) {
    $rate = 22050; $n = [int]($rate * $Seconds)
    $ms = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($ms)
    $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $n * 2)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
    $w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1)
    $w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
    $w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]($n * 2))
    $rng = [Random]::new(1100); $lp = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $n
        $v = & $Sample $t ($rng.NextDouble() * 2 - 1) ([ref]$lp)
        $w.Write([int16][Math]::Max(-32767, [Math]::Min(32767, [int]($v * 32767))))
    }
    $w.Flush()
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllBytes($Path, $ms.ToArray())
    $null = Add-PackFile $Path
}
$snd = Join-Path $script:RP 'sounds/lepton/logistics'
# shoot: a short air burst whose filter opens and closes (whoosh)
Write-Wav (Join-Path $snd 'pipe_shoot.wav') 0.32 {
    param($t, $noise, [ref]$lp)
    $cut = 0.05 + 0.5 * [Math]::Sin([Math]::PI * [Math]::Min(1, $t * 1.6))
    $lp.Value += ($noise - $lp.Value) * $cut
    $env = [Math]::Pow(1 - $t, 1.5) * [Math]::Min(1, $t * 40)
    return $lp.Value * $env * 0.9
}
# pickup: a soft rising suction pop
Write-Wav (Join-Path $snd 'pipe_pickup.wav') 0.22 {
    param($t, $noise, [ref]$lp)
    $cut = 0.03 + 0.35 * $t
    $lp.Value += ($noise - $lp.Value) * $cut
    $env = [Math]::Sin([Math]::PI * $t)
    $tone = [Math]::Sin(2 * [Math]::PI * (180 + 420 * $t) * $t * 0.22) * 0.25
    return ($lp.Value * 0.8 + $tone) * $env
}
Write-PackJson (Join-Path $script:RP 'sounds.d/logistics.json') ([ordered]@{
        'lepton.pipe.shoot'  = [ordered]@{ category = 'block'; sounds = @([ordered]@{ name = 'sounds/lepton/logistics/pipe_shoot'; volume = 0.8 }) }
        'lepton.pipe.pickup' = [ordered]@{ category = 'block'; sounds = @([ordered]@{ name = 'sounds/lepton/logistics/pipe_pickup'; volume = 0.8 }) }
    })

Add-PackText 'entity.lepton:pipe_item.name' @{ en = 'Pipe Item'; zh = '管道物品' }
Complete-PackUnit
