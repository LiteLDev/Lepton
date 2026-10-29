# U10 redstone pack: iron rod, gravisand (+ its falling entity), metal buttons, obsidian pressure
# plate, redstone randomizer, ender watcher; recipes and loot. Native behaviour: src/feature/redstone.
#
#   pwsh -NoProfile -File tools/pack/redstone.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/lib.ps1"

Start-PackUnit 'redstone'

foreach ($p in (& "$PSScriptRoot/../textures/redstone.ps1")) { $null = Add-PackFile $p }

function Read-Json([string]$Path) { return Get-Content -Raw $Path | ConvertFrom-Json -AsHashtable -Depth 100 }
function Get-BlockPath([string]$Id) { return Join-Path $script:BP "blocks/$(Get-Path $Id).json" }
# Adds components to a block the shared generators wrote (keeps the key order, appends).
function Add-BlockComponents([string]$Id, [hashtable]$Components) {
    $path = Get-BlockPath $Id
    $json = Read-Json $path
    foreach ($k in ($Components.Keys | Sort-Object)) { $json['minecraft:block']['components'][$k] = $Components[$k] }
    Write-PackJson $path ([ordered]@{ format_version = $json.format_version; 'minecraft:block' = $json['minecraft:block'] })
}
$popped = [ordered]@{ movement_type = 'popped' }
function Write-PickaxeLoot([string]$Name, [string]$Item) {
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$Name.json") ([ordered]@{ pools = @([ordered]@{
                rolls      = 1
                conditions = @([ordered]@{ condition = 'match_tool'; item_tags = [ordered]@{ any_of = @('minecraft:is_pickaxe') } })
                entries    = @([ordered]@{ type = 'item'; name = $Item; weight = 1 })
            }) })
    return "loot_tables/blocks/$Name.json"
}
# Unit-owned geometry (models/blocks/lepton_redstone_<name>.geo.json). $Cubes: bone name -> cube list
# (cubes from New-GeoCube, possibly with replaced face UVs / material instances).
function Write-UnitGeometry([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) { [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($kv.Value) } }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{ identifier = "geometry.lepton.redstone_$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0) }
                bones       = @($bonesJson)
            })
    }
    Write-PackJson (Join-Path $script:RP "models/blocks/lepton_redstone_$Name.geo.json") $geo
    return "geometry.lepton.redstone_$Name"
}
# A cube whose every face uses material instance $Mi with fixed UVs (u, v, w, h per face kind).
function New-UvCube($Box, [string]$Mi, $Side, $Top) {
    $c = New-GeoCube $Box
    foreach ($f in 'north', 'south', 'east', 'west') { $c.uv[$f] = [ordered]@{ uv = @($Side[0], $Side[1]); uv_size = @($Side[2], $Side[3]); material_instance = $Mi } }
    foreach ($f in 'up', 'down') { $c.uv[$f] = [ordered]@{ uv = @($Top[0], $Top[1]); uv_size = @($Top[2], $Top[3]); material_instance = $Mi } }
    return $c
}
function Tex([string]$Path) { return [ordered]@{ texture = (Add-TerrainTexture $Path); render_method = 'alpha_test' } }

$menu = [ordered]@{ category = 'items' }

# ------------------------------------------------------------------------------------------------
# Iron rod (end rod shape, rotated by the clicked face)
# ------------------------------------------------------------------------------------------------
$rodGeo = Write-UnitGeometry 'iron_rod' ([ordered]@{ rod = @(New-GeoCube (New-Box 7 0 7 9 16 9)) })
$c = [ordered]@{
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 5 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 10 }
    'minecraft:map_color'                 = '#999999'
    'minecraft:geometry'                  = $rodGeo
    'minecraft:material_instances'        = [ordered]@{ '*' = (Tex 'textures/blocks/iron_rod') }
    'minecraft:light_dampening'           = 0
    'minecraft:collision_box'             = ConvertTo-BoxJson (New-Box 6 0 6 10 16 10)
    'minecraft:selection_box'             = ConvertTo-BoxJson (New-Box 6 0 6 10 16 10)
    'minecraft:loot'                      = Write-LootTable 'iron_rod' 'lepton:iron_rod'
    'tag:lepton:breaks_toretoise_ore'     = [ordered]@{}
    'tag:lepton:collateral_breaker'       = [ordered]@{}
    'tag:lepton:collateral_mover'         = [ordered]@{}
    'tag:lepton:iron_rod'                 = [ordered]@{}
    'tag:metal'                           = [ordered]@{}
    'tag:minecraft:is_pickaxe_item_destructible' = [ordered]@{}
}
$face = { param($f) Get-StateCond 'minecraft:block_face' $f }
$perms = @(
    [ordered]@{ condition = (& $face 'down'); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(180, 0, 0) } } },
    [ordered]@{ condition = (& $face 'north'); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(-90, 0, 0) } } },
    [ordered]@{ condition = (& $face 'south'); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(90, 0, 0) } } },
    [ordered]@{ condition = (& $face 'east'); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, 0, -90) } } },
    [ordered]@{ condition = (& $face 'west'); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, 0, 90) } } }
)
Write-BlockJson -Id 'lepton:iron_rod' -Menu $menu `
    -Traits ([ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }) `
    -Components $c -Permutations $perms
Register-BlockClient 'lepton:iron_rod' 'metal' @{ en = 'Iron Rod'; zh = '铁杆' }
Write-ShapedRecipe 'iron_rod' @('I', 'I', 'R') @{ I = 'minecraft:iron_ingot'; R = 'minecraft:end_rod' } 'lepton:iron_rod' 1
Write-ShapedRecipe 'iron_rod_pre_end' @('I', 'I', 'I') @{ I = 'minecraft:iron_ingot' } 'lepton:iron_rod' 1

# ------------------------------------------------------------------------------------------------
# Gravisand (+ the falling entity lepton:gravisand)
# ------------------------------------------------------------------------------------------------
New-FullBlock -Id 'lepton:gravisand' -Textures 'textures/blocks/gravisand' -Name @{ en = 'Gravisand'; zh = '重力沙子' } `
    -Sound 'sand' -Hardness 0.5 -Resistance 0.5 -MapColor '#d6cc9c' -Tool 'shovel' -Category 'items' -Tags @('lepton:gravisand', 'sand')
Add-BlockComponents 'lepton:gravisand' @{ 'minecraft:redstone_consumer' = [ordered]@{ min_power = 1; propagates_power = $true } }
Write-ShapedRecipe 'gravisand' @('SSS', 'SES', 'SSS') @{ S = 'minecraft:sand'; E = 'minecraft:ender_pearl' } 'lepton:gravisand' 8

Write-PackJson (Join-Path $script:BP 'entities/gravisand.json') ([ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:gravisand'; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
            components  = [ordered]@{
                'minecraft:type_family'          = [ordered]@{ family = @('lepton_gravisand', 'falling_block', 'inanimate') }
                'minecraft:collision_box'        = [ordered]@{ width = 0.98; height = 0.98 }
                'minecraft:physics'              = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'             = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $true }
                'minecraft:damage_sensor'        = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
                'minecraft:health'               = [ordered]@{ value = 1; max = 1 }
                'minecraft:fire_immune'          = [ordered]@{}
                'minecraft:knockback_resistance' = [ordered]@{ value = 1.0 }
                'minecraft:conditional_bandwidth_optimization' = [ordered]@{ default_values = [ordered]@{ max_optimized_distance = 0; max_dropped_ticks = 0; use_motion_prediction_hints = $true } }
            }
        }
    })
$entityGeo = [ordered]@{
    format_version       = '1.12.0'
    'minecraft:geometry' = @([ordered]@{
            description = [ordered]@{ identifier = 'geometry.lepton.redstone_gravisand'; texture_width = 16; texture_height = 16
                visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
            bones       = @([ordered]@{ name = 'block'; pivot = @(0, 0, 0); cubes = @([ordered]@{
                            origin = @(-8, 0, -8); size = @(16, 16, 16)
                            uv     = [ordered]@{
                                north = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; east = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                                south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; west = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                                up = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; down = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                            } }) })
        })
}
Write-PackJson (Join-Path $script:RP 'models/entity/lepton_redstone_gravisand.geo.json') $entityGeo
Write-PackJson (Join-Path $script:RP 'entity/gravisand.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:gravisand'
                materials          = [ordered]@{ default = 'entity' }
                textures           = [ordered]@{ default = 'textures/blocks/gravisand' }
                geometry           = [ordered]@{ default = 'geometry.lepton.redstone_gravisand' }
                render_controllers = @('controller.render.default')
            }
        }
    })
Add-PackText 'entity.lepton:gravisand.name' @{ en = 'Floating Gravisand'; zh = '悬浮的重力沙子' }

# ------------------------------------------------------------------------------------------------
# Metal buttons and the obsidian pressure plate (native behaviour: game/Shapes)
# ------------------------------------------------------------------------------------------------
New-RedstoneInputBlock -Kind button -Id 'lepton:iron_button' -Texture 'textures/blocks/iron_block' -Name @{ en = 'Iron Button'; zh = '铁按钮' } `
    -Sound 'metal' -Hardness 0.5 -Resistance 0.5 -Tool '' -PressTicks 100 -Tags @('metal')
New-RedstoneInputBlock -Kind button -Id 'lepton:gold_button' -Texture 'textures/blocks/gold_block' -Name @{ en = 'Gold Button'; zh = '金按钮' } `
    -Sound 'metal' -Hardness 0.5 -Resistance 0.5 -Tool '' -PressTicks 4 -Tags @('metal')
New-RedstoneInputBlock -Kind pressure_plate -Id 'lepton:obsidian_pressure_plate' -Texture 'textures/blocks/obsidian' `
    -Name @{ en = 'Obsidian Pressure Plate'; zh = '黑曜石压力板' } -Sound 'stone' -Hardness 2 -Resistance 1200 -MapColor '#191919' `
    -Tool 'pickaxe' -PressTicks 20 -Tags @('lepton:players_only')
foreach ($id in 'lepton:iron_button', 'lepton:gold_button') { Add-BlockComponents $id @{ 'minecraft:movable' = $popped } }
Add-BlockComponents 'lepton:obsidian_pressure_plate' @{
    'minecraft:movable' = $popped
    'minecraft:loot'    = (Write-PickaxeLoot 'obsidian_pressure_plate' 'lepton:obsidian_pressure_plate')
}
$woodenButtons = 'wooden', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'mangrove', 'cherry', 'bamboo', 'crimson', 'warped', 'pale_oak'
foreach ($w in $woodenButtons) {
    Write-ShapelessRecipe "iron_button_from_$w" @("minecraft:${w}_button", 'minecraft:iron_ingot') 'lepton:iron_button' 1
    Write-ShapelessRecipe "gold_button_from_$w" @("minecraft:${w}_button", 'minecraft:gold_ingot') 'lepton:gold_button' 1
}
Write-ShapedRecipe 'obsidian_pressure_plate' @('OO') @{ O = 'minecraft:obsidian' } 'lepton:obsidian_pressure_plate' 1
Write-ShapedRecipe 'obsidian_pressure_plate_crying' @('OC') @{ O = 'minecraft:obsidian'; C = 'minecraft:crying_obsidian' } 'lepton:obsidian_pressure_plate' 1
Write-ShapedRecipe 'obsidian_pressure_plate_crying2' @('CC') @{ C = 'minecraft:crying_obsidian' } 'lepton:obsidian_pressure_plate' 1

# ------------------------------------------------------------------------------------------------
# Redstone randomizer: a slab with three torches and a prismarine crystal. Java FACING (the input
# side) = opposite of the placement trait's cardinal direction (the player's look direction).
# Base layout for FACING south: back torch at the south edge, left torch west, right torch east.
# ------------------------------------------------------------------------------------------------
function Rotate-Box($b, [string]$Facing) {
    switch ($Facing) {
        'south' { return $b }
        'north' { return New-Box (16 - $b.X1) $b.Y0 (16 - $b.Z1) (16 - $b.X0) $b.Y1 (16 - $b.Z0) }
        'east' { return New-Box (16 - $b.Z1) $b.Y0 $b.X0 (16 - $b.Z0) $b.Y1 $b.X1 }
        'west' { return New-Box $b.Z0 $b.Y0 (16 - $b.X1) $b.Z1 $b.Y1 (16 - $b.X0) }
    }
}
$litUv = @(7, 5, 2, 6); $litTop = @(7, 5, 2, 2)
$unlitUv = @(7, 6, 2, 5); $unlitTop = @(7, 6, 2, 2)
$torches = [ordered]@{ back = (New-Box 7 2 12 9 7 14); left = (New-Box 2 2 7 4 7 9); right = (New-Box 12 2 7 14 7 9) }
$bones = [ordered]@{
    base    = @(New-GeoCube (New-Box 0 0 0 16 2 16))
    crystal = @(New-UvCube (New-Box 6 2 6 10 4 10) 'up' @(6, 6, 4, 2) @(6, 6, 4, 4))
}
$vis = [ordered]@{}
$javaFacing = @{ north = 'south'; south = 'north'; east = 'west'; west = 'east' }   # trait value -> Java FACING
$powered = { param($v) Get-StateCond 'lepton:powered' $v }
foreach ($trait in 'north', 'south', 'east', 'west') {
    $f = $javaFacing[$trait]
    $fc = Get-StateCond 'minecraft:cardinal_direction' $trait
    foreach ($t in $torches.Keys) {
        $box = Rotate-Box $torches[$t] $f
        $lit = New-Box $box.X0 $box.Y0 $box.Z0 $box.X1 ($box.Y1 + 1) $box.Z1
        $bones["${t}_lit_$trait"] = @(New-UvCube $lit 'torch_on' $litUv $litTop)
        $bones["${t}_unlit_$trait"] = @(New-UvCube $box 'torch_off' $unlitUv $unlitTop)
        $on = switch ($t) { 'back' { Join-Or @((& $powered 'left'), (& $powered 'right')) } 'left' { & $powered 'left' } 'right' { & $powered 'right' } }
        $off = switch ($t) { 'back' { & $powered 'off' } 'left' { Join-Or @((& $powered 'off'), (& $powered 'right')) } 'right' { Join-Or @((& $powered 'off'), (& $powered 'left')) } }
        $vis["${t}_lit_$trait"] = Join-And @($fc, $on)
        $vis["${t}_unlit_$trait"] = Join-And @($fc, $off)
    }
}
$randGeo = Write-UnitGeometry 'redstone_randomizer' $bones
$side = Tex 'textures/blocks/stone_slab_top'
$c = [ordered]@{
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
    'minecraft:geometry'                  = [ordered]@{ identifier = $randGeo; bone_visibility = $vis }
    'minecraft:material_instances'        = [ordered]@{
        '*' = $side; north = $side; south = $side; east = $side; west = $side; down = $side
        up = (Tex 'textures/blocks/redstone_randomizer_off_south')
        torch_on = (Tex 'textures/blocks/redstone_torch_on'); torch_off = (Tex 'textures/blocks/redstone_torch_off')
    }
    'minecraft:light_dampening'           = 0
    'minecraft:collision_box'             = ConvertTo-BoxJson (New-Box 0 0 0 16 2 16)
    'minecraft:selection_box'             = ConvertTo-BoxJson (New-Box 0 0 0 16 2 16)
    'minecraft:movable'                   = $popped
    'minecraft:tick'                      = [ordered]@{ interval_range = @(1, 1); looping = $true }
    'minecraft:loot'                      = Write-LootTable 'redstone_randomizer' 'lepton:redstone_randomizer'
    'tag:lepton:redstone_randomizer'      = [ordered]@{}
    'tag:stone'                           = [ordered]@{}
}
$perms = @()
foreach ($trait in 'north', 'south', 'east', 'west') {
    foreach ($state in 'off', 'left', 'right') {
        $f = $javaFacing[$trait]
        $comp = [ordered]@{ 'minecraft:material_instances' = [ordered]@{
                '*' = $side; north = $side; south = $side; east = $side; west = $side; down = $side
                up = (Tex "textures/blocks/redstone_randomizer_${state}_$f")
                torch_on = (Tex 'textures/blocks/redstone_torch_on'); torch_off = (Tex 'textures/blocks/redstone_torch_off')
            } }
        if ($state -ne 'off') {
            # LEFT powers FACING clockwise (the placer's left), RIGHT counter-clockwise
            $out = if ($state -eq 'left') { switch ($f) { 'north' { 'east' } 'east' { 'south' } 'south' { 'west' } 'west' { 'north' } } } `
                else { switch ($f) { 'north' { 'west' } 'west' { 'south' } 'south' { 'east' } 'east' { 'north' } } }
            $comp['minecraft:redstone_producer'] = [ordered]@{ power = 15; strongly_powered_face = $out; connected_faces = @($out) }
        }
        $perms += [ordered]@{ condition = Join-And @((Get-StateCond 'minecraft:cardinal_direction' $trait), (& $powered $state)); components = $comp }
    }
}
Write-BlockJson -Id 'lepton:redstone_randomizer' -Menu ([ordered]@{ category = 'none' }) `
    -States ([ordered]@{ 'lepton:powered' = @('off', 'left', 'right') }) `
    -Traits ([ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 } }) `
    -Components $c -Permutations $perms
Register-BlockClient 'lepton:redstone_randomizer' 'stone' @{ en = 'Redstone Randomizer'; zh = '红石随机器' }
Add-PackText 'item.lepton:redstone_randomizer.name' @{ en = 'Redstone Randomizer'; zh = '红石随机器' }
$icon = Add-ItemTexture 'textures/items/redstone_randomizer'
Write-PackJson (Join-Path $script:BP 'items/redstone_randomizer.json') ([ordered]@{
        format_version   = $script:ItemFormat
        'minecraft:item' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:redstone_randomizer'; menu_category = $menu }
            components  = [ordered]@{
                'minecraft:icon'           = $icon
                'minecraft:block_placer'   = [ordered]@{ block = 'lepton:redstone_randomizer'; replace_block_item = $true }
                'minecraft:max_stack_size' = 64
            }
        }
    })
Write-ShapedRecipe 'redstone_randomizer' @(' X ', 'XBX', 'III') @{ X = 'minecraft:redstone_torch'; B = 'minecraft:prismarine_crystals'; I = 'minecraft:stone' } 'lepton:redstone_randomizer' 1

# ------------------------------------------------------------------------------------------------
# Ender watcher: lepton:watched (eye open), lepton:power 0..15 (redstone output)
# ------------------------------------------------------------------------------------------------
$off = Tex 'textures/blocks/ender_watcher'; $off.render_method = 'opaque'
$onTex = Tex 'textures/blocks/ender_watcher_on'; $onTex.render_method = 'opaque'
$c = [ordered]@{
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 3 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 10 }
    'minecraft:map_color'                 = '#667f33'
    'minecraft:geometry'                  = 'minecraft:geometry.full_block'
    'minecraft:material_instances'        = [ordered]@{ '*' = $off }
    'minecraft:tick'                      = [ordered]@{ interval_range = @(1, 1); looping = $true }
    'minecraft:loot'                      = Write-LootTable 'ender_watcher' 'lepton:ender_watcher'
    'tag:lepton:ender_watcher'            = [ordered]@{}
    'tag:metal'                           = [ordered]@{}
    'tag:minecraft:is_pickaxe_item_destructible' = [ordered]@{}
}
$perms = @([ordered]@{ condition = (Get-StateCond 'lepton:watched' $true); components = [ordered]@{ 'minecraft:material_instances' = [ordered]@{ '*' = $onTex } } })
for ($p = 1; $p -le 15; $p++) {
    $perms += [ordered]@{ condition = (Get-StateCond 'lepton:power' $p); components = [ordered]@{ 'minecraft:redstone_producer' = [ordered]@{ power = $p } } }
}
Write-BlockJson -Id 'lepton:ender_watcher' -Menu $menu `
    -States ([ordered]@{ 'lepton:watched' = @($false, $true); 'lepton:power' = [ordered]@{ values = [ordered]@{ min = 0; max = 15 } } }) `
    -Components $c -Permutations $perms
Register-BlockClient 'lepton:ender_watcher' 'metal' @{ en = 'Ender Watcher'; zh = '末影观察者' }
Write-ShapedRecipe 'ender_watcher' @('BRB', 'RER', 'BRB') @{ B = 'minecraft:obsidian'; R = 'minecraft:redstone'; E = 'minecraft:ender_eye' } 'lepton:ender_watcher' 1

Complete-PackUnit
