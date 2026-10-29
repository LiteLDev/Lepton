# Pack files of U24 frames: glass item frame blocks and items, dyed frame items, the glass frame
# display entity and the dyed frame overlay entity (behaviour + client entity, geometry,
# animation, render controller), recipes, loot and names. Run it and commit the output:
#
#   pwsh tools/pack/frames.ps1        (textures: pwsh tools/textures/frames.ps1)
. "$PSScriptRoot/lib.ps1"

Start-PackUnit 'frames'

$BP = Join-Path $script:RepoRoot 'assets/pack/bp'
$RP = Join-Path $script:RepoRoot 'assets/pack/rp'

function Fmt([double]$v) { return Get-Num ([Math]::Round($v, 4)) }

# ------------------------------------------------------------------------------------------------
# Glass item frame blocks
# ------------------------------------------------------------------------------------------------

# Boxes of a 12x12x1 frame on face $Face of its cell (the frame faces $Face, its support is on the
# opposite side). Front view coordinates: u, v in 0..16 (v up), depth d from the support.
function Get-FrameBox([string]$Face, [double]$U0, [double]$V0, [double]$U1, [double]$V1, [double]$D0, [double]$D1) {
    switch ($Face) {
        'south' { New-Box $U0 $V0 $D0 $U1 $V1 $D1 }                     # support north (z 0)
        'north' { New-Box (16 - $U1) $V0 (16 - $D1) (16 - $U0) $V1 (16 - $D0) }
        'east' { New-Box $D0 $V0 (16 - $U1) $D1 $V1 (16 - $U0) }          # support west (x 0)
        'west' { New-Box (16 - $D1) $V0 $U0 (16 - $D0) $V1 $U1 }
        'up' { New-Box $U0 $D0 (16 - $V1) $U1 $D1 (16 - $V0) }           # support below (y 0)
        'down' { New-Box $U0 (16 - $D1) $V0 $U1 (16 - $D0) $V1 }
    }
}

function Get-FrameBoxes([string]$Face) {
    return @(
        (Get-FrameBox $Face 2 2 14 3 0 1),     # bottom bar
        (Get-FrameBox $Face 2 13 14 14 0 1),   # top bar
        (Get-FrameBox $Face 2 3 3 13 0 1),     # left bar
        (Get-FrameBox $Face 13 3 14 13 0 1),   # right bar
        (Get-FrameBox $Face 3 3 13 13 0 0.5)   # glass pane
    )
}

$faces = 'down', 'up', 'north', 'south', 'west', 'east'
$bones = [ordered]@{}
foreach ($f in $faces) { $bones[$f] = Get-FrameBoxes $f }
$glassGeo = Write-Geometry 'glass_item_frame' $bones

$glassTex = Add-TerrainTexture 'textures/blocks/frames/glass_item_frame'
$mi = [ordered]@{}
foreach ($k in '*', 'north', 'south', 'east', 'west', 'up', 'down') {
    $mi[$k] = [ordered]@{ texture = $glassTex; render_method = 'blend'; ambient_occlusion = $false; face_dimming = $false }
}

foreach ($b in @(
        @{ Id = 'lepton:glass_item_frame'; En = 'Glass Item Frame'; Zh = '玻璃物品展示框' },
        @{ Id = 'lepton:glowing_glass_item_frame'; En = 'Glow Glass Item Frame'; Zh = '荧光玻璃物品展示框' })) {
    $path = Get-Path $b.Id
    $vis = [ordered]@{}
    foreach ($f in $faces) { $vis[$f] = "q.block_state('minecraft:block_face') == '$f' && !q.block_state('lepton:filled')" }
    $components = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0 }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
        'minecraft:light_dampening'           = 0
        'minecraft:collision_box'             = $false
        'minecraft:selection_box'             = ConvertTo-BoxJson (Get-BoundingBox (Get-FrameBoxes 'up'))
        'minecraft:material_instances'        = $mi
        'minecraft:geometry'                  = [ordered]@{ identifier = $glassGeo; bone_visibility = $vis }
        'minecraft:loot'                      = (Write-LootTable $path $b.Id)
        'minecraft:map_color'                 = '#c8e4ec'
        'tag:lepton:glass_item_frame'         = [ordered]@{}
    }
    $perms = foreach ($f in $faces) {
        [ordered]@{
            condition  = "q.block_state('minecraft:block_face') == '$f'"
            components = [ordered]@{ 'minecraft:selection_box' = ConvertTo-BoxJson (Get-BoundingBox (Get-FrameBoxes $f)) }
        }
    }
    Write-BlockJson -Id $b.Id -Menu ([ordered]@{ category = 'none'; is_hidden_in_commands = $true }) `
        -States ([ordered]@{ 'lepton:filled' = @($false, $true) }) `
        -Traits ([ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }) `
        -Components $components -Permutations $perms
    Register-BlockClient $b.Id 'itemframe' @{ en = $b.En; zh = $b.Zh }
    Add-PackText "item.$($b.Id).name" @{ en = $b.En; zh = $b.Zh }
    $icon = Add-ItemTexture "textures/items/frames/$path"
    Write-PackJson (Join-Path $BP "items/$path.json") ([ordered]@{
            format_version  = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $b.Id; menu_category = [ordered]@{ category = 'items' } }
                components  = [ordered]@{
                    'minecraft:icon'           = $icon
                    'minecraft:block_placer'   = [ordered]@{ block = $b.Id; replace_block_item = $true }
                    'minecraft:max_stack_size' = 64
                }
            }
        })
}

Write-ShapedRecipe 'glass_item_frame' @('###', '#F#', '###') @{ '#' = 'minecraft:glass_pane'; F = 'minecraft:frame' } 'lepton:glass_item_frame' 2
Write-ShapelessRecipe 'glowing_glass_item_frame' @('minecraft:glow_ink_sac', 'lepton:glass_item_frame') 'lepton:glowing_glass_item_frame' 1

# ------------------------------------------------------------------------------------------------
# Dyed frame items (minecraft:dyeable; the dye colour is in the stack)
# ------------------------------------------------------------------------------------------------

foreach ($d in @(
        @{ Id = 'lepton:dyed_item_frame'; En = 'Item Frame'; Zh = '物品展示框' },
        @{ Id = 'lepton:dyed_glow_item_frame'; En = 'Glow Item Frame'; Zh = '荧光物品展示框' })) {
    $path = Get-Path $d.Id
    $default = Add-ItemTexture "textures/items/frames/$path"
    $dyed = Add-ItemTexture "textures/items/frames/$($path)_dyed"
    Add-PackText "item.$($d.Id).name" @{ en = $d.En; zh = $d.Zh }
    Write-PackJson (Join-Path $BP "items/$path.json") ([ordered]@{
            format_version  = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $d.Id; menu_category = [ordered]@{ category = 'none' } }
                components  = [ordered]@{
                    'minecraft:icon'           = [ordered]@{ textures = [ordered]@{ default = $default; dyed = $dyed } }
                    'minecraft:dyeable'        = [ordered]@{ default_color = '#ffffff' }
                    'minecraft:max_stack_size' = 64
                }
            }
        })
}

# ------------------------------------------------------------------------------------------------
# Entities
# ------------------------------------------------------------------------------------------------

function New-DisplayEntity([string]$Id, $Properties) {
    return [ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{
                identifier       = $Id
                is_spawnable     = $false
                is_summonable    = $true
                is_experimental  = $false
                properties       = $Properties
            }
            components  = [ordered]@{
                'minecraft:type_family'                         = [ordered]@{ family = @('lepton_frame_display', 'inanimate') }
                'minecraft:collision_box'                       = [ordered]@{ width = 0.01; height = 0.01 }
                'minecraft:physics'                             = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'                            = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:damage_sensor'                       = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
                'minecraft:health'                              = [ordered]@{ value = 1; max = 1 }
                'minecraft:fire_immune'                         = [ordered]@{}
                'minecraft:knockback_resistance'                = [ordered]@{ value = 1.0 }
                'minecraft:body_rotation_blocked'               = [ordered]@{}
                'minecraft:nameable'                            = [ordered]@{ always_show = $false; allow_name_tag_renaming = $false }
                'minecraft:conditional_bandwidth_optimization'  = [ordered]@{
                    default_values = [ordered]@{ max_optimized_distance = 0.0; max_dropped_ticks = 0; use_motion_prediction_hints = $false }
                }
            }
        }
    }
}

function New-IntProp([int]$Lo, [int]$Hi, [int]$Default) { return [ordered]@{ type = 'int'; range = @($Lo, $Hi); default = $Default; client_sync = $true } }
function New-BoolProp { return [ordered]@{ type = 'bool'; default = $false; client_sync = $true } }

Write-PackJson (Join-Path $BP 'entities/glass_frame.json') (New-DisplayEntity 'lepton:glass_frame' ([ordered]@{
            'lepton:facing'        = New-IntProp 0 5 3
            'lepton:rotation'      = New-IntProp 0 7 0
            'lepton:kind'          = New-IntProp 0 5 0
            'lepton:sign'          = New-IntProp 0 6 0
            'lepton:sign_rotation' = New-IntProp 0 15 0
            'lepton:scale'         = [ordered]@{ type = 'float'; range = @(0.0, 16.0); default = 1.5; client_sync = $true }
            'lepton:shiny'         = New-BoolProp
        }))
Write-PackJson (Join-Path $BP 'entities/dyed_item_frame.json') (New-DisplayEntity 'lepton:dyed_item_frame' ([ordered]@{
            'lepton:facing' = New-IntProp 0 5 3
            'lepton:color'  = New-IntProp 0 16777215 16777215
            'lepton:map'    = New-BoolProp
            'lepton:glow'   = New-BoolProp
        }))
Add-PackText 'entity.lepton:glass_frame.name' @{ en = 'Glass Item Frame'; zh = '玻璃物品展示框' }
Add-PackText 'entity.lepton:dyed_item_frame.name' @{ en = 'Dyed Item Frame'; zh = '染色物品展示框' }

# ---- glass frame display: no cubes, an item bone posed by the animation --------------------------
# Model space: the entity faces its yaw; the support is behind it at z = +8 (Bedrock models face
# -z). Floor and ceiling frames turn the whole pose with the `face` bone.
$displayGeo = [ordered]@{
    format_version       = '1.12.0'
    'minecraft:geometry' = @([ordered]@{
            description = [ordered]@{
                identifier = 'geometry.lepton.glass_frame'; texture_width = 16; texture_height = 16
                visible_bounds_width = 4; visible_bounds_height = 4; visible_bounds_offset = @(0, 0.5, 0)
            }
            bones       = @(
                [ordered]@{ name = 'root'; pivot = @(0, 0, 0) },
                [ordered]@{ name = 'face'; parent = 'root'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'sign'; parent = 'face'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'sign_turn'; parent = 'sign'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'sign_offset'; parent = 'sign_turn'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'spin'; parent = 'sign_offset'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'holder'; parent = 'spin'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'rightArm'; parent = 'holder'; pivot = @(0, 8, 0) },
                [ordered]@{ name = 'rightItem'; parent = 'rightArm'; pivot = @(0, 8, 0) }
            )
        })
}
Write-PackJson (Join-Path $RP 'models/entity/lepton_glass_frame.geo.json') $displayGeo

# Display pose (Quark's GlassItemFrameRenderer in model units; Java's local +z = towards the
# support = model +z). Kinds: 0 item, 1 tool, 2 block, 3 banner, 4 shield, 5 map.
$P = @{
    facing  = "q.property('lepton:facing')"
    kind    = "q.property('lepton:kind')"
    sign    = "q.property('lepton:sign')"
    rot     = "q.property('lepton:rotation')"
    scale   = "q.property('lepton:scale')"
    signRot = "q.property('lepton:sign_rotation')"
}
# Item depth from the cell centre (Java: 0.475 blocks for items, 0.5 for shields, 0.55 banners).
$depth = "($($P.kind) == 4 ? 8.0 : ($($P.kind) == 3 ? 8.8 : 7.6))"
# Scale: Java renders FIXED items at 0.5 x itemRenderScale (shields x 2.667); banners show their
# flag (0.8 x 0.4 of a banner).
$itemScale = "($($P.kind) == 3 ? 0.5 : ($($P.kind) == 4 ? 0.5 * $($P.scale) * 2.6667 : 0.5 * $($P.scale)))"
# Held-item pose of Bedrock's item bone, per kind (sprites are drawn edge-on to the arm).
$armRot = @(
    "($($P.kind) == 2 ? 0 : 0)",
    "($($P.kind) == 2 ? 0 : 90)",
    "($($P.kind) == 1 ? -45 : 0)"
)
# Sign attachment (Quark: 0.32 scale on the sign face; 1/2 standing in front/behind, 3 wall sign,
# 4 hanging from a wall, 5/6 hanging in front/behind). Positions in px (Java blocks x 16).
$onSign = "($($P.sign) > 0)"
# Quark: angle = -(signRotation - index of the frame's back direction in [N, E, S, W] * 4) * 22.5
$backIndex = "($($P.facing) == 2 ? 2 : ($($P.facing) == 3 ? 0 : ($($P.facing) == 4 ? 1 : 3)))"
$signAngle = "($($P.sign) == 3 || $($P.sign) == 4 ? 0 : -($($P.signRot) - $backIndex * 4) * 22.5 + ($($P.sign) == 2 ? 180 : 0))"
$signY = "($($P.sign) == 3 ? 5.6 - 4.8 : ($($P.sign) == 4 ? 5.6 - 8.32 : 5.6))"
$signZ = "($($P.sign) == 3 ? 15.68 + 7.04 : ($($P.sign) == 4 ? 15.68 - 0.32 : 15.68))"
$hangY = "($($P.sign) == 5 || $($P.sign) == 6 ? -0.52 * 16 / 0.32 : 0)"
$hangZ = "($($P.sign) == 5 ? -0.075 * 16 : ($($P.sign) == 6 ? 0.3 * 16 : 0))"
$spin = "($($P.kind) == 5 ? math.mod($($P.rot), 4) * 2 : $($P.rot)) * 45"

$anim = [ordered]@{
    format_version = '1.8.0'
    animations     = [ordered]@{
        'animation.lepton.glass_frame.pose' = [ordered]@{
            loop  = $true
            bones = [ordered]@{
                face        = [ordered]@{ rotation = @("($($P.facing) == 1 ? -90 : ($($P.facing) == 0 ? 90 : 0))", 0, 0) }
                sign        = [ordered]@{ position = @(0, "($onSign ? $signY : 0)", "($onSign ? $signZ : 0)") }
                sign_turn   = [ordered]@{ rotation = @(0, "($onSign ? $signAngle : 0)", 0); scale = "($onSign ? 0.32 : 1)" }
                sign_offset = [ordered]@{ position = @(0, "($onSign ? $hangY : 0)", "($onSign ? $hangZ - 0.585 * 16 : 0)") }
                spin        = [ordered]@{ rotation = @(0, 0, "-($spin)") }
                holder      = [ordered]@{
                    position = @("($($P.kind) == 4 ? -4 : 0)", 0, $depth)
                    scale    = $itemScale
                }
                rightArm    = [ordered]@{ rotation = $armRot }
            }
        }
    }
}
Write-PackJson (Join-Path $RP 'animations/lepton_frames.animation.json') $anim

Write-PackJson (Join-Path $RP 'entity/glass_frame.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:glass_frame'
                materials          = [ordered]@{ default = 'entity_alphatest' }
                textures           = [ordered]@{ default = 'textures/entity/frames/dyed_item_frame' }
                geometry           = [ordered]@{ default = 'geometry.lepton.glass_frame' }
                animations         = [ordered]@{ pose = 'animation.lepton.glass_frame.pose' }
                scripts            = [ordered]@{ animate = @('pose') }
                render_controllers = @('controller.render.default')
                enable_attachables = $true
            }
        }
    })

# ---- dyed frame overlay: the frame shape (wall / floor / ceiling, item / map) tinted ------------
# Texture 32x16: left = item frame front view (ring 2..13, leather 3..12), right = map frame (ring 0..15).
function New-OverlayCube([string]$Plane, [double]$A0, [double]$B0, [double]$A1, [double]$B1, [double]$D0, [double]$D1, $Uv, [double]$Inflate) {
    # $Plane: wall (front view x, y; depth along z from +8), floor (x, z; depth along y from 0),
    # ceiling (x, z; depth down from 16). $Uv = @(u, v, w, h) of the front face.
    switch ($Plane) {
        'wall' { $origin = @(($A0 - 8), $B0, (8 - $D1)); $size = @(($A1 - $A0), ($B1 - $B0), ($D1 - $D0)) }
        'floor' { $origin = @(($A0 - 8), $D0, ($B0 - 8)); $size = @(($A1 - $A0), ($D1 - $D0), ($B1 - $B0)) }
        'ceiling' { $origin = @(($A0 - 8), (16 - $D1), ($B0 - 8)); $size = @(($A1 - $A0), ($D1 - $D0), ($B1 - $B0)) }
    }
    $front = [ordered]@{ uv = @((Fmt $Uv[0]), (Fmt $Uv[1])); uv_size = @((Fmt $Uv[2]), (Fmt $Uv[3])) }
    $edge = [ordered]@{ uv = @((Fmt $Uv[0]), (Fmt $Uv[1])); uv_size = @(1, 1) }
    $faces = [ordered]@{ north = $edge; south = $edge; east = $edge; west = $edge; up = $edge; down = $edge }
    switch ($Plane) {
        'wall' { $faces.north = $front; $faces.south = $front }
        default { $faces.up = $front; $faces.down = $front }
    }
    $cube = [ordered]@{ origin = @($origin | ForEach-Object { Fmt $_ }); size = @($size | ForEach-Object { Fmt $_ }); uv = $faces }
    if ($Inflate) { $cube.inflate = $Inflate }
    return $cube
}

function Get-OverlayCubes([string]$Plane, [bool]$Map) {
    $e = 0.06
    if ($Map) {
        return @(
            (New-OverlayCube $Plane 0 0 16 1 0 1 @(16, 15, 16, 1) $e),
            (New-OverlayCube $Plane 0 15 16 16 0 1 @(16, 0, 16, 1) $e),
            (New-OverlayCube $Plane 0 1 1 15 0 1 @(16, 1, 1, 14) $e),
            (New-OverlayCube $Plane 15 1 16 15 0 1 @(31, 1, 1, 14) $e)
        )
    }
    return @(
        (New-OverlayCube $Plane 2 2 14 3 0 1 @(2, 13, 12, 1) $e),
        (New-OverlayCube $Plane 2 13 14 14 0 1 @(2, 2, 12, 1) $e),
        (New-OverlayCube $Plane 2 3 3 13 0 1 @(2, 3, 1, 10) $e),
        (New-OverlayCube $Plane 13 3 14 13 0 1 @(13, 3, 1, 10) $e),
        (New-OverlayCube $Plane 3 3 13 13 0.5 0.56 @(3, 3, 10, 10) 0)
    )
}

$overlayBones = foreach ($plane in 'wall', 'floor', 'ceiling') {
    [ordered]@{ name = $plane; pivot = @(0, 8, 0); cubes = @(Get-OverlayCubes $plane $false) }
    [ordered]@{ name = "$($plane)_map"; pivot = @(0, 8, 0); cubes = @(Get-OverlayCubes $plane $true) }
}
Write-PackJson (Join-Path $RP 'models/entity/lepton_dyed_item_frame.geo.json') ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = 'geometry.lepton.dyed_item_frame'; texture_width = 32; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0)
                }
                bones       = @($overlayBones)
            })
    })

$f = "q.property('lepton:facing')"; $m = "q.property('lepton:map')"; $c = "q.property('lepton:color')"
Write-PackJson (Join-Path $RP 'render_controllers/lepton_frames.render_controllers.json') ([ordered]@{
        format_version     = '1.8.0'
        render_controllers = [ordered]@{
            'controller.render.lepton.dyed_item_frame' = [ordered]@{
                geometry        = 'Geometry.default'
                materials       = @([ordered]@{ '*' = 'Material.default' })
                textures        = @('Texture.default')
                color           = [ordered]@{
                    r = "math.mod(math.floor($c / 65536), 256) / 255"
                    g = "math.mod(math.floor($c / 256), 256) / 255"
                    b = "math.mod($c, 256) / 255"
                    a = '1.0'
                }
                part_visibility = @(
                    [ordered]@{ '*' = $false },
                    [ordered]@{ wall = "$f > 1 && !$m" },
                    [ordered]@{ wall_map = "$f > 1 && $m" },
                    [ordered]@{ floor = "$f == 1 && !$m" },
                    [ordered]@{ floor_map = "$f == 1 && $m" },
                    [ordered]@{ ceiling = "$f == 0 && !$m" },
                    [ordered]@{ ceiling_map = "$f == 0 && $m" }
                )
            }
        }
    })

Write-PackJson (Join-Path $RP 'entity/dyed_item_frame.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:dyed_item_frame'
                materials          = [ordered]@{ default = 'entity_alphatest_change_color' }
                textures           = [ordered]@{ default = 'textures/entity/frames/dyed_item_frame' }
                geometry           = [ordered]@{ default = 'geometry.lepton.dyed_item_frame' }
                render_controllers = @('controller.render.lepton.dyed_item_frame')
            }
        }
    })

Complete-PackUnit
