# U29 worldbeyond pack: chorus weeds, chorus twist (ChorusVegetation), dusky myalite and myalite
# crystal (SpiralSpires). See docs/spec/worldbeyond.md §2.
#
#   pwsh -NoProfile -File tools/pack/worldbeyond.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/worldbeyond.ps1"

Start-PackUnit 'worldbeyond'

foreach ($f in Write-WorldBeyondTextures) { [void](Add-PackFile $f) }

$unit = 'worldbeyond'
function Tex([string]$Name) { return "textures/blocks/$Name" }

# ------------------------------------------------------------------------------------------------
# Dusky myalite: plain stone block (Quark: stone properties, strength 1.5 / 6, pickaxe for drops).
# ------------------------------------------------------------------------------------------------
Write-PackJson (Join-Path $script:BP 'loot_tables/blocks/dusky_myalite.json') ([ordered]@{
        pools = @([ordered]@{
                rolls      = 1
                conditions = @([ordered]@{ condition = 'match_tool'; item_tags_any = @('minecraft:is_pickaxe') })
                entries    = @([ordered]@{ type = 'item'; name = 'lepton:dusky_myalite'; weight = 1 })
            })
    })
New-FullBlock -Id 'lepton:dusky_myalite' -Textures (Tex 'dusky_myalite') -Name @{ en = 'Dusky Myalite'; zh = '黯幻境石' } `
    -Hardness 1.5 -Resistance 6 -MapColor '#7a4958' -Tool pickaxe -Category construction -Group 'minecraft:itemGroup.name.stone' `
    -Loot 'loot_tables/blocks/dusky_myalite.json'

# ------------------------------------------------------------------------------------------------
# Myalite crystal: glass-like, light 14, 16 colour steps in lepton:hue (Quark's position colour).
# ------------------------------------------------------------------------------------------------
# Item colour: Quark tints the item with the colour of position (0, 0, 0); the selftest
# worldbeyond.myalite_color checks this constant against the colour logic.
$itemHue = 6
$hueValues = @($itemHue) + @(0..15 | Where-Object { $_ -ne $itemHue })
$crystalComponents = Get-BaseComponents @{ Hardness = 0.5; Resistance = 1200; MapColor = '#7f3fb2'; Tool = 'pickaxe'; Tags = @('lepton:myalite_crystal') }
$crystalComponents['minecraft:geometry'] = 'minecraft:geometry.full_block'
$crystalComponents['minecraft:light_emission'] = 14
$crystalComponents['minecraft:light_dampening'] = 0
$crystalComponents['minecraft:material_instances'] = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture (Tex "myalite_crystal_$itemHue")); render_method = 'blend' } }
Write-PackJson (Join-Path $script:BP 'loot_tables/blocks/myalite_crystal.json') ([ordered]@{
        pools = @([ordered]@{
                rolls      = 1
                conditions = @([ordered]@{ condition = 'match_tool'; item_tags_any = @('minecraft:is_pickaxe') })
                entries    = @([ordered]@{ type = 'item'; name = 'lepton:myalite_crystal'; weight = 1 })
            })
    })
$crystalComponents['minecraft:loot'] = 'loot_tables/blocks/myalite_crystal.json'
$crystalPerms = foreach ($i in 0..15) {
    [ordered]@{
        condition  = "q.block_state('lepton:hue') == $i"
        components = [ordered]@{
            'minecraft:material_instances' = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture (Tex "myalite_crystal_$i")); render_method = 'blend' } }
        }
    }
}
Write-BlockJson -Id 'lepton:myalite_crystal' -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.stone' }) `
    -States ([ordered]@{ 'lepton:hue' = @($hueValues) }) -Components $crystalComponents -Permutations $crystalPerms
Register-BlockClient 'lepton:myalite_crystal' 'glass' @{ en = 'Myalite Crystal'; zh = '幻境石水晶' }

# ------------------------------------------------------------------------------------------------
# Chorus weeds / chorus twist: cross plants on end stone (Quark's ChorusVegetationBlock).
# ------------------------------------------------------------------------------------------------
$crossGeo = [ordered]@{
    format_version       = '1.21.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{
                identifier = 'geometry.lepton.worldbeyond_cross'; texture_width = 16; texture_height = 16
                visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0)
            }
            bones       = @(
                [ordered]@{
                    name = 'cross'; pivot = @(0, 0, 0)
                    cubes = @(
                        [ordered]@{ origin = @(-8, 0, 0); size = @(16, 16, 0); pivot = @(0, 0, 0); rotation = @(0, 45, 0)
                            uv = [ordered]@{ north = [ordered]@{ uv = @(16, 0); uv_size = @(-16, 16) }; south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) } } },
                        [ordered]@{ origin = @(-8, 0, 0); size = @(16, 16, 0); pivot = @(0, 0, 0); rotation = @(0, -45, 0)
                            uv = [ordered]@{ north = [ordered]@{ uv = @(16, 0); uv_size = @(-16, 16) }; south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) } } }
                    )
                }
            )
        }
    )
}
Write-PackJson (Join-Path $script:RP 'models/blocks/lepton_worldbeyond_cross.geo.json') $crossGeo

function New-ChorusPlant([string]$Path, [hashtable]$Name, [string]$TextureKey, [string]$IconKey) {
    $id = "lepton:$Path"
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$Path.json") ([ordered]@{
            pools = @([ordered]@{
                    rolls      = 1
                    conditions = @([ordered]@{ condition = 'match_tool'; item = 'minecraft:shears' })
                    entries    = @([ordered]@{ type = 'item'; name = $id; weight = 1 })
                })
        })
    $mi = [ordered]@{ '*' = [ordered]@{ texture = $TextureKey; render_method = 'alpha_test'; ambient_occlusion = $false; face_dimming = $false } }
    $c = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0 }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0 }
        'minecraft:map_color'                 = '#007c00'
        'minecraft:flammable'                 = [ordered]@{ catch_chance_modifier = 60; destroy_chance_modifier = 100 }
        'minecraft:geometry'                  = 'geometry.lepton.worldbeyond_cross'
        'minecraft:material_instances'        = $mi
        'minecraft:collision_box'             = $false
        'minecraft:selection_box'             = [ordered]@{ origin = @(-6, 0, -6); size = @(12, 13, 12) }
        'minecraft:light_dampening'           = 0
        'minecraft:replaceable'               = [ordered]@{}
        'minecraft:flower_pottable'           = [ordered]@{}
        'minecraft:embedded_visual'           = [ordered]@{ geometry = 'geometry.lepton.worldbeyond_cross'; material_instances = $mi }
        'minecraft:random_offset'             = [ordered]@{
            x = [ordered]@{ steps = 16; range = [ordered]@{ min = -4; max = 4 } }
            y = [ordered]@{ steps = 0; range = [ordered]@{ min = 0; max = 0 } }
            z = [ordered]@{ steps = 16; range = [ordered]@{ min = -4; max = 4 } }
        }
        'minecraft:placement_filter'          = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up'); block_filter = @('minecraft:end_stone') }) }
        'minecraft:liquid_detection'          = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $false; on_liquid_touches = 'popped' }) }
        'minecraft:movable'                   = [ordered]@{ movement_type = 'popped' }
        'minecraft:loot'                      = "loot_tables/blocks/$Path.json"
        'tag:plant'                           = [ordered]@{}
        'tag:lepton:chorus_vegetation'        = [ordered]@{}
    }
    Write-BlockJson -Id $id -Menu ([ordered]@{ category = 'none' }) -Components $c
    Register-BlockClient $id 'grass' $Name
    # The item: a flat icon (like vanilla plants) that places the block.
    Write-PackJson (Join-Path $script:BP "items/$Path.json") ([ordered]@{
            format_version  = '1.21.60'
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $id; menu_category = [ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.flower' } }
                components  = [ordered]@{
                    'minecraft:icon'         = $IconKey
                    'minecraft:block_placer' = [ordered]@{ block = $id; replace_block_item = $true }
                }
            }
        })
    Add-PackText "item.$id.name" $Name
}

# Weeds: three texture variations chosen per position (Quark's three random models).
$weedsKey = "lepton_$($unit)_chorus_weeds"
$script:Pack.Terrain[$weedsKey] = [ordered]@{
    textures = @([ordered]@{ variations = @(0..2 | ForEach-Object { [ordered]@{ path = (Tex "chorus_weeds_$_"); weight = 1 } }) })
}
$twistKey = Add-TerrainTexture (Tex 'chorus_twist')
$weedsIcon = Add-ItemTexture (Tex 'chorus_weeds_0')
$twistIcon = Add-ItemTexture (Tex 'chorus_twist')
New-ChorusPlant 'chorus_weeds' @{ en = 'Chorus Weeds'; zh = '紫颂杂草' } $weedsKey $weedsIcon
New-ChorusPlant 'chorus_twist' @{ en = 'Chorus Twist'; zh = '紫颂缠草' } $twistKey $twistIcon

Complete-PackUnit
