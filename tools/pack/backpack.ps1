# U14 backpack: Backpack, Ravager Hide, Bonded Ravager Hide, Seed Pouch, Totem of Holding entity.
# See docs/spec/backpack.md.
#
#   pwsh -NoProfile -File tools/pack/backpack.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/backpack.ps1"

Start-PackUnit 'backpack'

Write-BackpackTextures -RP $script:RP -Record { param($p) Add-PackFile $p }

function Write-Item([string]$Path, [hashtable]$Name, [string]$Category, $Components, [string]$Group) {
    $menu = [ordered]@{ category = $Category }
    if ($Group) { $menu.group = $Group }
    Write-PackJson (Join-Path $script:BP "items/$Path.json") ([ordered]@{
            format_version  = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Path"; menu_category = $menu }
                components  = $Components
            }
        })
    Add-PackText "item.lepton:$Path.name" $Name
}

# --- items -----------------------------------------------------------------------------------------
Write-Item 'backpack' @{ en = 'Backpack'; zh = '背包' } 'equipment' ([ordered]@{
        'minecraft:icon'           = (Add-ItemTexture 'textures/items/backpack')
        'minecraft:max_stack_size' = 1
        'minecraft:wearable'       = [ordered]@{ slot = 'slot.armor.chest'; protection = 0 }
        'minecraft:dyeable'        = [ordered]@{ default_color = '#A06540' }
        'minecraft:rarity'         = 'rare'
        'minecraft:tags'           = [ordered]@{ tags = @('lepton:backpack_blocked') }
    })
Write-Item 'ravager_hide' @{ en = 'Ravager Hide'; zh = '劫掠兽皮' } 'items' ([ordered]@{
        'minecraft:icon'           = (Add-ItemTexture 'textures/items/ravager_hide')
        'minecraft:max_stack_size' = 64
        'minecraft:rarity'         = 'rare'
    })
Write-Item 'seed_pouch' @{ en = 'Seed Pouch'; zh = '种子袋' } 'equipment' ([ordered]@{
        'minecraft:icon'           = (Add-ItemTexture 'textures/items/seed_pouch')
        'minecraft:max_stack_size' = 1
    })

# --- block -----------------------------------------------------------------------------------------
New-FullBlock -Id 'lepton:bonded_ravager_hide' -Textures 'textures/blocks/bonded_ravager_hide' `
    -Name @{ en = 'Bonded Ravager Hide'; zh = '打包劫掠兽皮' } -Sound 'cloth' -Hardness 1 -Resistance 1 `
    -MapColor '#191919' -Flammable -Category 'construction'

# --- recipes ---------------------------------------------------------------------------------------
foreach ($chest in @(@('', 'minecraft:chest'), @('_trapped_chest', 'minecraft:trapped_chest'))) {
    Write-ShapedRecipe "backpack$($chest[0])" @('LRL', 'LCL', 'LIL') @{
        L = 'minecraft:leather'; R = 'lepton:ravager_hide'; C = $chest[1]; I = 'minecraft:iron_ingot'
    } 'lepton:backpack' 1
    Write-ShapedRecipe "backpack_no_hide$($chest[0])" @('LIL', 'LCL', 'LIL') @{
        L = 'minecraft:leather'; C = $chest[1]; I = 'minecraft:iron_ingot'
    } 'lepton:backpack' 1
}
Write-ShapedRecipe 'bonded_ravager_hide' @('###', '###', '###') @{ '#' = 'lepton:ravager_hide' } 'lepton:bonded_ravager_hide' 1
Write-ShapelessRecipe 'bonded_ravager_hide_uncompress' @('lepton:bonded_ravager_hide') 'lepton:ravager_hide' 9

# Quark's #quark:seed_pouch_holdable, as Bedrock ids (kept in sync with src/feature/backpack/SeedPouch.cpp).
$seeds = @(
    'wheat_seeds', 'beetroot_seeds', 'melon_seeds', 'pumpkin_seeds', 'torchflower_seeds', 'pitcher_pod', 'potato', 'carrot',
    'brown_mushroom', 'red_mushroom', 'warped_fungus', 'crimson_fungus',
    'oak_sapling', 'spruce_sapling', 'birch_sapling', 'jungle_sapling', 'acacia_sapling', 'dark_oak_sapling', 'cherry_sapling',
    'pale_oak_sapling', 'mangrove_propagule', 'azalea', 'flowering_azalea',
    'sweet_berries', 'kelp', 'bamboo', 'sugar_cane', 'nether_wart', 'cocoa_beans')
foreach ($s in $seeds) {
    Write-ShapedRecipe "seed_pouch_$s" @(' S ', 'HXH', ' H ') @{ S = 'minecraft:string'; H = 'minecraft:leather'; X = "minecraft:$s" } 'lepton:seed_pouch' 1
}

# --- entities --------------------------------------------------------------------------------------
$common = [ordered]@{
    'minecraft:physics'               = [ordered]@{ has_gravity = $false; has_collision = $false }
    'minecraft:pushable'              = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
    'minecraft:damage_sensor'         = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
    'minecraft:health'                = [ordered]@{ value = 1; max = 1 }
    'minecraft:fire_immune'           = [ordered]@{}
    'minecraft:knockback_resistance'  = [ordered]@{ value = 1.0 }
}
$totem = [ordered]@{
    'minecraft:type_family'   = [ordered]@{ family = @('lepton_totem', 'inanimate') }
    'minecraft:collision_box' = [ordered]@{ width = 0.5; height = 1 }
    'minecraft:inventory'     = [ordered]@{ container_type = 'container'; inventory_size = 64; can_be_siphoned_from = $false; private = $false }
    'minecraft:nameable'      = [ordered]@{ always_show = $false; allow_name_tag_renaming = $false }
}
foreach ($kv in $common.GetEnumerator()) { $totem[$kv.Key] = $kv.Value }
Write-PackJson (Join-Path $script:BP 'entities/totem.json') ([ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{
                identifier = 'lepton:totem'; is_spawnable = $false; is_summonable = $true; is_experimental = $false
                properties = [ordered]@{ 'lepton:dying' = [ordered]@{ type = 'bool'; default = $false; client_sync = $true } }
            }
            components  = $totem
            events      = [ordered]@{ 'lepton:start_dying' = [ordered]@{ set_property = [ordered]@{ 'lepton:dying' = $true } } }
        }
    })
$storage = [ordered]@{
    'minecraft:type_family'   = [ordered]@{ family = @('lepton_backpack_storage', 'inanimate') }
    'minecraft:collision_box' = [ordered]@{ width = 0.01; height = 0.01 }
    'minecraft:inventory'     = [ordered]@{ container_type = 'container'; inventory_size = 27; can_be_siphoned_from = $false; private = $false }
    'minecraft:nameable'      = [ordered]@{ always_show = $false; allow_name_tag_renaming = $false }
}
foreach ($kv in $common.GetEnumerator()) { $storage[$kv.Key] = $kv.Value }
Write-PackJson (Join-Path $script:BP 'entities/backpack_storage.json') ([ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:backpack_storage'; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
            components  = $storage
        }
    })
Add-PackText 'entity.lepton:totem.name' @{ en = 'Totem of Holding'; zh = '留存图腾' }
Add-PackText 'entity.lepton:backpack_storage.name' @{ en = 'Backpack'; zh = '背包' }

# --- client entities, models, animations -----------------------------------------------------------
function Cube($Origin, $Size, $Uv) { return [ordered]@{ origin = @($Origin); size = @($Size); uv = @($Uv) } }
function Write-Geo([string]$File, [string]$Id, [int]$Tw, [int]$Th, $Bones, [double]$Vb = 2) {
    Write-PackJson (Join-Path $script:RP "models/entity/$File.geo.json") ([ordered]@{
            format_version       = '1.12.0'
            'minecraft:geometry' = @([ordered]@{
                    description = [ordered]@{
                        identifier = $Id; texture_width = $Tw; texture_height = $Th
                        visible_bounds_width = $Vb; visible_bounds_height = $Vb + 1; visible_bounds_offset = @(0, 1, 0)
                    }
                    bones       = @($Bones)
                })
        })
}
Write-Geo 'lepton_totem' 'geometry.lepton.totem' 32 32 @(
    [ordered]@{ name = 'root'; pivot = @(0, 0, 0); locators = [ordered]@{ center = @(0, 8, 0) }; cubes = @(
            (Cube @(-3, 2, -1) @(6, 8, 2) @(0, 0)),
            (Cube @(-2, 10, -1) @(4, 4, 2) @(0, 10)),
            (Cube @(-6, 5, -0.5) @(3, 4, 1) @(16, 0)),
            (Cube @(3, 5, -0.5) @(3, 4, 1) @(16, 6))) })
Write-Geo 'lepton_backpack_worn' 'geometry.lepton.backpack_worn' 32 32 @(
    [ordered]@{ name = 'body'; pivot = @(0, 24, 0); cubes = @(
            (Cube @(-4, 13, 2) @(8, 10, 3) @(0, 0)),
            (Cube @(-3, 14, 5) @(6, 4, 1) @(0, 14))) }) 3

Write-PackJson (Join-Path $script:RP 'entity/totem.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier          = 'lepton:totem'
                materials           = [ordered]@{ default = 'entity_alphatest' }
                textures            = [ordered]@{ default = 'textures/entity/totem_of_holding' }
                geometry            = [ordered]@{ default = 'geometry.lepton.totem' }
                particle_effects    = [ordered]@{ portal = 'minecraft:portal_directional' }
                animations          = [ordered]@{
                    idle       = 'animation.lepton.totem.idle'
                    dying      = 'animation.lepton.totem.dying'
                    controller = 'controller.animation.lepton.totem'
                }
                scripts             = [ordered]@{ animate = @('controller') }
                render_controllers  = @('controller.render.default')
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'animations/lepton_totem.animation.json') ([ordered]@{
        format_version = '1.10.0'
        animations     = [ordered]@{
            # 1 degree per tick, bobbing by sin(ticks * 0.03 rad) * 0.1 block (Molang sin takes degrees)
            'animation.lepton.totem.idle'  = [ordered]@{
                loop = $true; animation_length = 0.1
                bones = [ordered]@{ root = [ordered]@{
                        rotation = @(0, 'q.life_time * 20', 0)
                        position = @(0, 'math.sin(q.life_time * 20 * 1.71887) * 1.6', 0) } }
                particle_effects = [ordered]@{
                    '0.0'  = [ordered]@{ effect = 'portal'; locator = 'center' }
                    '0.05' = [ordered]@{ effect = 'portal'; locator = 'center' } }
            }
            # 40 ticks: shrink to 0, rise 5 blocks, spin 5 degrees more per tick
            'animation.lepton.totem.dying' = [ordered]@{
                loop = 'hold_on_last_frame'; animation_length = 2
                bones = [ordered]@{ root = [ordered]@{
                        rotation = @(0, 'q.life_time * 20 + q.anim_time * 100', 0)
                        position = @(0, 'q.anim_time / 2 * 80', 0)
                        scale    = 'math.max(0, 1 - q.anim_time / 2)' } }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'animation_controllers/lepton_totem.animation_controllers.json') ([ordered]@{
        format_version        = '1.10.0'
        animation_controllers = [ordered]@{
            'controller.animation.lepton.totem' = [ordered]@{
                initial_state = 'idle'
                states        = [ordered]@{
                    idle  = [ordered]@{ animations = @('idle'); transitions = @([ordered]@{ dying = "q.property('lepton:dying')" }) }
                    dying = [ordered]@{ animations = @('dying') }
                }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'entity/backpack_storage.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:backpack_storage'
                materials          = [ordered]@{ default = 'entity_alphatest' }
                textures           = [ordered]@{ default = 'textures/blocks/stone' }
                geometry           = [ordered]@{ default = 'geometry.lepton.empty' }
                render_controllers = @('controller.render.default')
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'attachables/backpack.json') ([ordered]@{
        format_version        = '1.10.0'
        'minecraft:attachable' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:backpack'
                materials          = [ordered]@{ default = 'armor_leather'; enchanted = 'armor_leather_enchanted' }
                textures           = [ordered]@{ default = 'textures/models/armor/backpack'; enchanted = 'textures/misc/enchanted_actor_glint' }
                geometry           = [ordered]@{ default = 'geometry.lepton.backpack_worn' }
                render_controllers = @('controller.render.armor')
            }
        }
    })

Complete-PackUnit
