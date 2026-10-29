# U12 crystals: corundum (block, waxed, sheet, cluster), crystal lamps, magnet, magnetized block,
# the beacon beam helper entity and the unit's particles.
#
#   pwsh -NoProfile -File tools/pack/crystals.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/crystals.ps1"

Start-PackUnit 'crystals'

$colors = [ordered]@{
    red    = @{ en = 'Red'; zh = '红色'; map = '#993333'; dye = 'minecraft:red_dye' }
    orange = @{ en = 'Orange'; zh = '橙色'; map = '#d87f33'; dye = 'minecraft:orange_dye' }
    yellow = @{ en = 'Yellow'; zh = '黄色'; map = '#e5e533'; dye = 'minecraft:yellow_dye' }
    green  = @{ en = 'Green'; zh = '绿色'; map = '#667f33'; dye = 'minecraft:green_dye' }
    blue   = @{ en = 'Blue'; zh = '蓝色'; map = '#6699d8'; dye = 'minecraft:light_blue_dye' }
    indigo = @{ en = 'Indigo'; zh = '靛蓝色'; map = '#334cb2'; dye = 'minecraft:blue_dye' }
    violet = @{ en = 'Violet'; zh = '蓝紫色'; map = '#b24cd8'; dye = 'minecraft:pink_dye' }
    white  = @{ en = 'White'; zh = '白色'; map = '#ffffff'; dye = 'minecraft:white_dye' }
    black  = @{ en = 'Black'; zh = '黑色'; map = '#191919'; dye = 'minecraft:black_dye' }
}

$blocks = Join-Path $script:RP 'textures/blocks'
$entityTex = Join-Path $script:RP 'textures/entity'
$particleTex = Join-Path $script:RP 'textures/particle'
function TexB([string]$Name) { Add-PackFile (Join-Path $blocks "$Name.png") }

$pickaxeLoot = {
    param([string]$Name, [string]$Item)
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$Name.json") ([ordered]@{ pools = @([ordered]@{
                    rolls = 1
                    conditions = @([ordered]@{ condition = 'match_tool'; item_tags = [ordered]@{ any_of = @('minecraft:is_pickaxe') } })
                    entries = @([ordered]@{ type = 'item'; name = $Item; weight = 1 })
                }) })
    "loot_tables/blocks/$Name.json"
}
$silkLoot = {
    param([string]$Name, [string]$Item)
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$Name.json") ([ordered]@{ pools = @([ordered]@{
                    rolls = 1
                    conditions = @([ordered]@{ condition = 'match_tool'; enchantments = @([ordered]@{ enchantment = 'silk_touch'; levels = [ordered]@{ range_min = 1 } }) })
                    entries = @([ordered]@{ type = 'item'; name = $Item; weight = 1 })
                }) })
    "loot_tables/blocks/$Name.json"
}
$tagged = { param($c, [string[]]$tags) foreach ($t in $tags) { $c["tag:$t"] = [ordered]@{} } }

# ---------------------------------------------------------------------------------------------
# Corundum: block, waxed block, sheet (pane), cluster
# ---------------------------------------------------------------------------------------------
$clusterGeo = [ordered]@{
    format_version = '1.12.0'
    'minecraft:geometry' = @([ordered]@{
            description = [ordered]@{ identifier = 'geometry.lepton.crystal_cluster'; texture_width = 16; texture_height = 16
                visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
            bones = @(
                [ordered]@{ name = 'cross_a'; pivot = @(0, 0, 0); rotation = @(0, 45, 0); cubes = @([ordered]@{ origin = @(-8, 0, 0); size = @(16, 16, 0)
                            uv = [ordered]@{ north = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) } } }) },
                [ordered]@{ name = 'cross_b'; pivot = @(0, 0, 0); rotation = @(0, -45, 0); cubes = @([ordered]@{ origin = @(-8, 0, 0); size = @(16, 16, 0)
                            uv = [ordered]@{ north = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) } } }) }
            )
        })
}
Write-PackJson (Join-Path $script:RP 'models/blocks/lepton_crystal_cluster.geo.json') $clusterGeo

# cluster: model rotation and box per pointing face (Java CorundumClusterBlock shapes)
$clusterFaces = [ordered]@{
    up    = @{ rot = @(0, 0, 0); box = (New-Box 3 0 3 13 7 13) }
    down  = @{ rot = @(180, 0, 0); box = (New-Box 3 9 3 13 16 13) }
    north = @{ rot = @(-90, 0, 0); box = (New-Box 3 3 9 13 13 16) }
    south = @{ rot = @(90, 0, 0); box = (New-Box 3 3 0 13 13 7) }
    east  = @{ rot = @(0, 0, -90); box = (New-Box 0 3 3 7 13 13) }
    west  = @{ rot = @(0, 0, 90); box = (New-Box 9 3 3 16 13 13) }
}

foreach ($c in $colors.Keys) {
    $info = $colors[$c]
    $id = "lepton:${c}_corundum"
    Save-CorundumTexture $c (TexB "${c}_corundum")
    Save-ClusterTexture $c (TexB "${c}_corundum_cluster")

    foreach ($waxed in $false, $true) {
        $bid = if ($waxed) { "lepton:waxed_${c}_corundum" } else { $id }
        $comp = [ordered]@{
            'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.3 }
            'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
            'minecraft:map_color'                 = $info.map
            'minecraft:light_emission'            = 11
            'minecraft:light_dampening'           = 0
            'minecraft:redstone_conductivity'     = [ordered]@{ redstone_conductor = $false; allows_wire_to_step_down = $true }
            'minecraft:geometry'                  = 'minecraft:geometry.full_block'
            'minecraft:material_instances'        = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture "textures/blocks/${c}_corundum"); render_method = 'blend' } }
            'minecraft:loot'                      = & $pickaxeLoot (Get-Path $bid) $bid
        }
        $tags = @('minecraft:is_pickaxe_item_destructible', 'lepton:corundum_any', "lepton:crystal_color_$c")
        if (-not $waxed) { $tags += 'lepton:corundum' } else { $tags += 'lepton:waxed_corundum' }
        & $tagged $comp $tags
        Write-BlockJson -Id $bid -Menu ([ordered]@{ category = 'construction'; group = 'minecraft:itemGroup.name.glass' }) -Components $comp
        $nm = if ($waxed) { @{ en = "Waxed $($info.en) Corundum"; zh = "涂蜡$($info.zh)刚玉" } } else { @{ en = "$($info.en) Corundum"; zh = "$($info.zh)刚玉" } }
        Register-BlockClient $bid 'amethyst_block' $nm
    }
    Write-ShapelessRecipe "${c}_waxed_corundum" @($id, 'minecraft:honeycomb') "lepton:waxed_${c}_corundum" 1

    # sheet (Quark: ZetaInheritedPaneBlock, full copy of the corundum's properties)
    $pane = New-ShapeSet -Material "${c}_corundum" -BaseBlock $id -Shapes pane -Textures "textures/blocks/${c}_corundum" `
        -Name @{ en = "$($info.en) Corundum"; zh = "$($info.zh)刚玉" } -Names @{ pane = @{ en = "$($info.en) Corundum Sheet"; zh = "$($info.zh)刚玉板" } } `
        -Ids @{ pane = "lepton:${c}_corundum_pane" } -Sound 'amethyst_block' -Hardness 0.3 -Resistance 0 -MapColor $info.map -Tool pickaxe `
        -Tags @('lepton:corundum_pane', "lepton:crystal_color_$c") -RenderMethod blend
    $paneFile = Join-Path $script:BP "blocks/${c}_corundum_pane.json"
    $pj = Get-Content $paneFile -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $pj['minecraft:block']['components']['minecraft:light_emission'] = 11
    $pj['minecraft:block']['components']['minecraft:loot'] = & $silkLoot "${c}_corundum_pane" "lepton:${c}_corundum_pane"
    Write-PackJson $paneFile $pj

    # cluster
    $cid = "lepton:${c}_corundum_cluster"
    $comp = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.3 }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
        'minecraft:map_color'                 = $info.map
        'minecraft:light_emission'            = 11
        'minecraft:light_dampening'           = 0
        'minecraft:redstone_conductivity'     = [ordered]@{ redstone_conductor = $false; allows_wire_to_step_down = $true }
        'minecraft:geometry'                  = 'geometry.lepton.crystal_cluster'
        'minecraft:material_instances'        = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture "textures/blocks/${c}_corundum_cluster"); render_method = 'alpha_test'; face_dimming = $false; ambient_occlusion = $false } }
        'minecraft:liquid_detection'          = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
        'minecraft:loot'                      = & $pickaxeLoot (Get-Path $cid) $cid
        'minecraft:collision_box'             = ConvertTo-BoxJson $clusterFaces.up.box
        'minecraft:selection_box'             = ConvertTo-BoxJson $clusterFaces.up.box
    }
    & $tagged $comp @('minecraft:is_pickaxe_item_destructible', 'lepton:corundum_cluster', "lepton:crystal_color_$c")
    $perms = foreach ($f in $clusterFaces.Keys) {
        [ordered]@{ condition = (Get-StateCond 'minecraft:block_face' $f); components = [ordered]@{
                'minecraft:transformation' = [ordered]@{ rotation = $clusterFaces[$f].rot }
                'minecraft:collision_box'  = ConvertTo-BoxJson $clusterFaces[$f].box
                'minecraft:selection_box'  = ConvertTo-BoxJson $clusterFaces[$f].box } }
    }
    Write-BlockJson -Id $cid -Menu ([ordered]@{ category = 'nature' }) `
        -Traits ([ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }) `
        -Components $comp -Permutations $perms
    Register-BlockClient $cid 'amethyst_cluster' @{ en = "$($info.en) Corundum Cluster"; zh = "$($info.zh)刚玉簇" }
}

# ---------------------------------------------------------------------------------------------
# Crystal lamps (RainbowLamps)
# ---------------------------------------------------------------------------------------------
foreach ($c in $colors.Keys) {
    $info = $colors[$c]
    $lid = "lepton:${c}_crystal_lamp"
    Save-LampTexture $c $false (TexB "${c}_crystal_lamp")
    Save-LampTexture $c $true (TexB "${c}_crystal_lamp_on")
    $lit = Get-StateCond 'lepton:lit' $true
    $comp = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.3 }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
        'minecraft:map_color'                 = $info.map
        'minecraft:light_emission'            = 0
        'minecraft:light_dampening'           = 0
        'minecraft:redstone_conductivity'     = [ordered]@{ redstone_conductor = $false; allows_wire_to_step_down = $true }
        'minecraft:redstone_consumer'         = [ordered]@{ min_power = 1; propagates_power = $false }
        'minecraft:geometry'                  = 'minecraft:geometry.full_block'
        'minecraft:material_instances'        = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture "textures/blocks/${c}_crystal_lamp"); render_method = 'blend' } }
        'minecraft:loot'                      = Write-LootTable (Get-Path $lid) $lid
    }
    & $tagged $comp @('lepton:crystal_lamp', "lepton:crystal_color_$c")
    $perms = @([ordered]@{ condition = $lit; components = [ordered]@{
                'minecraft:light_emission'     = 15
                'minecraft:material_instances' = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture "textures/blocks/${c}_crystal_lamp_on"); render_method = 'blend' } } } })
    Write-BlockJson -Id $lid -Menu ([ordered]@{ category = 'construction'; group = 'minecraft:itemGroup.name.glass' }) `
        -States ([ordered]@{ 'lepton:lit' = @($false, $true) }) -Components $comp -Permutations $perms
    Register-BlockClient $lid 'amethyst_block' @{ en = "$($info.en) Corundum Lamp"; zh = "$($info.zh)刚玉灯" }
    Write-ShapedRecipe "${c}_corundum_lamp" @(' R ', 'RCR', ' R ') @{ 'C' = "lepton:${c}_corundum"; 'R' = 'minecraft:redstone' } $lid 1
    Write-ShapedRecipe "${c}_crystal_lamp" @(' D ', 'RAR', ' G ') @{ 'D' = $info.dye; 'R' = 'minecraft:redstone'; 'A' = 'minecraft:amethyst_block'; 'G' = 'minecraft:glowstone_dust' } $lid 1
}

# ---------------------------------------------------------------------------------------------
# Magnet + magnetized block
# ---------------------------------------------------------------------------------------------
Save-MagnetTextures $blocks
foreach ($f in 'front', 'back', 'side') { foreach ($s in 'off', 'on') { $null = Add-PackFile (Join-Path $blocks "magnet_${f}_$s.png") } }
# model front = north. The placement trait stores the direction the placer looks in
# (minecraft:facing_direction); Quark's FACING (the pushing face) is its opposite, so the front
# faces the placer: looking south -> front north (no rotation).
$magnetFacings = @(
    @{ v = 'up'; rot = @(-90, 0, 0) }, @{ v = 'down'; rot = @(90, 0, 0) }, @{ v = 'south'; rot = @(0, 0, 0) },
    @{ v = 'north'; rot = @(0, 180, 0) }, @{ v = 'east'; rot = @(0, 90, 0) }, @{ v = 'west'; rot = @(0, -90, 0) }
)
$mi = {
    param([string]$s)
    $t = { param($n) [ordered]@{ texture = (Add-TerrainTexture "textures/blocks/magnet_${n}_$s"); render_method = 'opaque' } }
    [ordered]@{ '*' = (& $t 'side'); north = (& $t 'front'); south = (& $t 'back'); east = (& $t 'side'); west = (& $t 'side'); up = (& $t 'side'); down = (& $t 'side') }
}
$comp = [ordered]@{
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 5 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 6 }
    'minecraft:map_color'                 = '#a7a7a7'
    'minecraft:redstone_conductivity'     = [ordered]@{ redstone_conductor = $true; allows_wire_to_step_down = $false }
    'minecraft:redstone_consumer'         = [ordered]@{ min_power = 1; propagates_power = $false }
    'minecraft:geometry'                  = 'minecraft:geometry.full_block'
    'minecraft:material_instances'        = & $mi 'off'
    'minecraft:loot'                      = & $pickaxeLoot 'magnet' 'lepton:magnet'
}
& $tagged $comp @('minecraft:is_pickaxe_item_destructible', 'metal', 'lepton:magnet')
$perms = @(foreach ($f in $magnetFacings) {
        [ordered]@{ condition = (Get-StateCond 'minecraft:facing_direction' $f.v); components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = $f.rot } } }
    }) + @([ordered]@{ condition = (Get-StateCond 'lepton:powered' $true); components = [ordered]@{
            'minecraft:light_emission' = 3; 'minecraft:material_instances' = (& $mi 'on') } })
Write-BlockJson -Id 'lepton:magnet' -Menu ([ordered]@{ category = 'construction'; group = 'minecraft:itemGroup.name.redstone' }) `
    -Traits ([ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:facing_direction') } }) `
    -States ([ordered]@{ 'lepton:powered' = @($false, $true); 'lepton:waxed' = @($false, $true) }) `
    -Components $comp -Permutations $perms
Register-BlockClient 'lepton:magnet' 'metal' @{ en = 'Magnet'; zh = '磁铁' }

Write-PackJson (Join-Path $script:BP 'recipes/magnet.json') ([ordered]@{
        format_version            = $script:RecipeFormat
        'minecraft:recipe_shaped' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:magnet' }
            tags        = @('crafting_table')
            pattern     = @('CIC', 'BFR', 'CIC')
            key         = [ordered]@{ B = [ordered]@{ item = 'minecraft:blue_dye' }; C = [ordered]@{ tag = 'minecraft:stone_crafting_materials' }
                F = [ordered]@{ item = 'minecraft:chorus_fruit' }; I = [ordered]@{ item = 'minecraft:iron_ingot' }; R = [ordered]@{ item = 'minecraft:redstone' } }
            unlock      = @([ordered]@{ item = 'minecraft:iron_ingot' }, [ordered]@{ item = 'minecraft:chorus_fruit' })
            result      = [ordered]@{ item = 'lepton:magnet'; count = 1 }
        }
    })
Write-PackJson (Join-Path $script:BP 'recipes/magnet_pre_end.json') ([ordered]@{
        format_version            = $script:RecipeFormat
        'minecraft:recipe_shaped' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:magnet_pre_end' }
            tags        = @('crafting_table')
            pattern     = @('CIC', 'BPR', 'CIC')
            key         = [ordered]@{ B = [ordered]@{ item = 'minecraft:blue_dye' }; C = [ordered]@{ tag = 'minecraft:stone_crafting_materials' }
                I = [ordered]@{ item = 'minecraft:iron_ingot' }; P = [ordered]@{ item = 'minecraft:copper_ingot' }; R = [ordered]@{ item = 'minecraft:redstone' } }
            unlock      = @([ordered]@{ item = 'minecraft:iron_ingot' }, [ordered]@{ item = 'minecraft:copper_ingot' })
            result      = [ordered]@{ item = 'lepton:magnet'; count = 1 }
        }
    })

# the magnetized block: an invisible, unbreakable placeholder at a moving block's destination
Save-PixelArt -Rows (@('.' * 16) * 16) -Palette @{} -Path (TexB 'magnetized_block')
$comp = [ordered]@{
    'minecraft:destructible_by_mining'    = $false
    'minecraft:destructible_by_explosion' = $false
    'minecraft:light_dampening'           = 0
    'minecraft:collision_box'             = $false
    'minecraft:selection_box'             = $false
    'minecraft:geometry'                  = 'minecraft:geometry.full_block'
    'minecraft:material_instances'        = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture 'textures/blocks/magnetized_block'); render_method = 'alpha_test' } }
    'minecraft:loot'                      = 'loot_tables/blocks/magnetized_block.json'
}
& $tagged $comp @('lepton:magnetized_block')
Write-BlockJson -Id 'lepton:magnetized_block' -Menu ([ordered]@{ category = 'none'; is_hidden_in_commands = $true }) -Components $comp
Register-BlockClient 'lepton:magnetized_block' 'stone' @{ en = 'Magnetized Block'; zh = '磁化块' }

# ---------------------------------------------------------------------------------------------
# Beacon beam helper entity (BeaconRedirection)
# ---------------------------------------------------------------------------------------------
Save-BeamTextures $entityTex
$null = Add-PackFile (Join-Path $entityTex 'beacon_beam_core.png')
foreach ($i in 0..4) { $null = Add-PackFile (Join-Path $entityTex "beacon_beam_glow_$i.png") }

$prop = { param($type, $range, $default) [ordered]@{ type = $type; range = $range; default = $default; client_sync = $true } }
Write-PackJson (Join-Path $script:BP 'entities/beacon_beam.json') ([ordered]@{
        format_version  = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{
                identifier = 'lepton:beacon_beam'; is_spawnable = $false; is_summonable = $true; is_experimental = $false
                properties = [ordered]@{
                    'lepton:dir'  = & $prop 'int' @(0, 5) 1
                    'lepton:len'  = & $prop 'int' @(1, 64) 1
                    'lepton:glow' = & $prop 'int' @(0, 4) 0
                    'lepton:r'    = & $prop 'float' @(0.0, 1.0) 1.0
                    'lepton:g'    = & $prop 'float' @(0.0, 1.0) 1.0
                    'lepton:b'    = & $prop 'float' @(0.0, 1.0) 1.0
                }
            }
            components  = [ordered]@{
                'minecraft:type_family'          = [ordered]@{ family = @('lepton_beacon_beam', 'inanimate') }
                'minecraft:collision_box'        = [ordered]@{ width = 0.01; height = 0.01 }
                'minecraft:physics'              = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'             = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:damage_sensor'        = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
                'minecraft:health'               = [ordered]@{ value = 1; max = 1 }
                'minecraft:fire_immune'          = [ordered]@{}
                'minecraft:knockback_resistance' = [ordered]@{ value = 1.0 }
                'minecraft:nameable'             = [ordered]@{ always_show = $false; allow_name_tag_renaming = $false }
            }
        }
    })

Write-PackJson (Join-Path $script:RP 'models/entity/lepton_beacon_beam.geo.json') ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{ identifier = 'geometry.lepton.beacon_beam'; texture_width = 16; texture_height = 16
                    visible_bounds_width = 132; visible_bounds_height = 132; visible_bounds_offset = @(0, 0, 0) }
                bones       = @(
                    [ordered]@{ name = 'root'; pivot = @(0, 8, 0) },
                    [ordered]@{ name = 'beam'; parent = 'root'; pivot = @(0, 8, 0) },
                    [ordered]@{ name = 'core'; parent = 'beam'; pivot = @(0, 8, 0); cubes = @([ordered]@{ origin = @(-3.2, 8, -3.2); size = @(6.4, 16, 6.4); uv = @(0, 0) }) },
                    [ordered]@{ name = 'glow'; parent = 'beam'; pivot = @(0, 8, 0); cubes = @([ordered]@{ origin = @(-4, 8, -4); size = @(8, 16, 8); uv = @(0, 0) }) }
                )
            })
    })
Write-PackJson (Join-Path $script:RP 'animations/lepton_beacon_beam.animation.json') ([ordered]@{
        format_version = '1.8.0'
        animations     = [ordered]@{
            'animation.lepton.beacon_beam.orient' = [ordered]@{
                loop  = $true
                bones = [ordered]@{
                    # beam axis = +y from the block centre; FacingID 0 down 1 up 2 north 3 south 4 west 5 east
                    root = [ordered]@{ rotation = @(
                            "q.property('lepton:dir') == 0 ? 180 : (q.property('lepton:dir') == 2 ? -90 : (q.property('lepton:dir') == 3 ? 90 : 0))",
                            '0',
                            "q.property('lepton:dir') == 4 ? 90 : (q.property('lepton:dir') == 5 ? -90 : 0)") }
                    beam = [ordered]@{ scale = @(1, "q.property('lepton:len')", 1) }
                    core = [ordered]@{ rotation = @(0, 'q.life_time * 45 - 45', 0) }
                }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'render_controllers/lepton_beacon_beam.render_controllers.json') ([ordered]@{
        format_version     = '1.8.0'
        render_controllers = [ordered]@{
            'controller.render.lepton.beacon_beam_core' = [ordered]@{
                geometry        = 'Geometry.default'
                materials       = @([ordered]@{ '*' = 'Material.core' })
                textures        = @('Texture.core')
                part_visibility = @([ordered]@{ '*' = $true }, [ordered]@{ glow = $false })
                overlay_color   = [ordered]@{ r = "q.property('lepton:r')"; g = "q.property('lepton:g')"; b = "q.property('lepton:b')"; a = '1.0' }
                ignore_lighting = $true
            }
            'controller.render.lepton.beacon_beam_glow' = [ordered]@{
                arrays          = [ordered]@{ textures = [ordered]@{ 'Array.glow' = @('Texture.glow0', 'Texture.glow1', 'Texture.glow2', 'Texture.glow3', 'Texture.glow4') } }
                geometry        = 'Geometry.default'
                materials       = @([ordered]@{ '*' = 'Material.glow' })
                textures        = @("Array.glow[q.property('lepton:glow')]")
                part_visibility = @([ordered]@{ '*' = $true }, [ordered]@{ core = $false })
                overlay_color   = [ordered]@{ r = "q.property('lepton:r')"; g = "q.property('lepton:g')"; b = "q.property('lepton:b')"; a = '1.0' }
                ignore_lighting = $true
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'entity/beacon_beam.entity.json') ([ordered]@{
        format_version           = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier        = 'lepton:beacon_beam'
                materials         = [ordered]@{ core = 'beacon_beam'; glow = 'beacon_beam_transparent' }
                textures          = [ordered]@{
                    core = 'textures/entity/beacon_beam_core'; glow0 = 'textures/entity/beacon_beam_glow_0'; glow1 = 'textures/entity/beacon_beam_glow_1'
                    glow2 = 'textures/entity/beacon_beam_glow_2'; glow3 = 'textures/entity/beacon_beam_glow_3'; glow4 = 'textures/entity/beacon_beam_glow_4'
                }
                geometry          = [ordered]@{ default = 'geometry.lepton.beacon_beam' }
                animations        = [ordered]@{ orient = 'animation.lepton.beacon_beam.orient' }
                scripts           = [ordered]@{ animate = @('orient') }
                render_controllers = @('controller.render.lepton.beacon_beam_core', 'controller.render.lepton.beacon_beam_glow')
            }
        }
    })

# ---------------------------------------------------------------------------------------------
# Particles (corundum dust / sparks / growth, magnet attractor / repulsor)
# ---------------------------------------------------------------------------------------------
Save-ParticleTextures $particleTex
foreach ($n in 'magnet_plus', 'magnet_minus', 'corundum_spark', 'corundum_dust') { $null = Add-PackFile (Join-Path $particleTex "$n.png") }

function Write-Particle([string]$Name, [string]$Texture, $Lifetime, $Size, $Color, $Speed, $Direction, [double[]]$Accel = @(0, 0, 0), [string]$Material = 'particles_alpha') {
    $comp = [ordered]@{
        'minecraft:emitter_rate_instant'          = [ordered]@{ num_particles = 1 }
        'minecraft:emitter_lifetime_once'         = [ordered]@{ active_time = 0 }
        'minecraft:emitter_shape_point'           = [ordered]@{ direction = $Direction }
        'minecraft:particle_lifetime_expression'  = [ordered]@{ max_lifetime = $Lifetime }
        'minecraft:particle_initial_speed'        = $Speed
        'minecraft:particle_motion_dynamic'       = [ordered]@{ linear_acceleration = $Accel }
        'minecraft:particle_appearance_billboard' = [ordered]@{ size = $Size; facing_camera_mode = 'rotate_xyz'
            uv = [ordered]@{ texture_width = 8; texture_height = 8; uv = @(0, 0); uv_size = @(8, 8) } }
        'minecraft:particle_appearance_tinting'   = [ordered]@{ color = $Color }
        'minecraft:particle_appearance_lighting'  = [ordered]@{}
    }
    Write-PackJson (Join-Path $script:RP "particles/lepton_$Name.json") ([ordered]@{
            format_version  = '1.10.0'
            particle_effect = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name"; basic_render_parameters = [ordered]@{ material = $Material; texture = "textures/particle/$Texture" } }
                components  = $comp
            }
        })
}
# Java DustParticle: lifetime 8 / (rand*0.8 + 0.2) ticks, slight upward drift, size from options
Write-Particle 'corundum_dust' 'corundum_dust' '0.4 / math.random(0.2, 1.0)' @('v.size * 0.1', 'v.size * 0.1') @('v.r', 'v.g', 'v.b', 1.0) 0 @(0, 1, 0) @(0, 0.2, 0)
# END_ROD flying towards the corundum (velocity in v.dx/dy/dz blocks per tick)
Write-Particle 'corundum_spark' 'corundum_spark' 'math.random(3, 5)' @(0.05, 0.05) @(1.0, 1.0, 1.0, 1.0) 'math.sqrt(v.dx * v.dx + v.dy * v.dy + v.dz * v.dz) * 20' @('v.dx', 'v.dy', 'v.dz') @(0, 0, 0) 'particles_add'
# ENTITY_EFFECT of a growing crystal
Write-Particle 'corundum_grow' 'corundum_dust' 'math.random(0.4, 1.2)' @(0.08, 0.08) @('v.r', 'v.g', 'v.b', 1.0) 'math.random(0.5, 1.5)' @('v.dx', 'v.dy', 'v.dz') @(0, 0, 0)
# Quark's magnet particles: 33 ticks, no friction, velocity along the flux
Write-Particle 'magnet_repulsor' 'magnet_plus' 1.65 @(0.07, 0.07) @(1.0, 0.35, 0.35, '0.6 * (1 - math.pow(2 * v.particle_age / v.particle_lifetime - 1, 2))') 'math.sqrt(v.dx * v.dx + v.dy * v.dy + v.dz * v.dz) * 20' @('v.dx', 'v.dy', 'v.dz') @(0, 0, 0) 'particles_add'
Write-Particle 'magnet_attractor' 'magnet_minus' 1.65 @(0.07, 0.07) @(0.35, 0.5, 1.0, '0.6 * (1 - math.pow(2 * v.particle_age / v.particle_lifetime - 1, 2))') 'math.sqrt(v.dx * v.dx + v.dy * v.dy + v.dz * v.dz) * 20' @('v.dx', 'v.dy', 'v.dz') @(0, 0, 0) 'particles_add'

# empty loot table for the magnetized block (shared name, owned by this unit)
Write-PackJson (Join-Path $script:BP 'loot_tables/blocks/magnetized_block.json') ([ordered]@{ pools = @() })

Complete-PackUnit
