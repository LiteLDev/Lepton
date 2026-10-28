# Pack files of U22 throwables (behaviour + resource pack JSON). Run: pwsh tools/pack/throwables.ps1
# The output is committed; textures come from tools/textures/throwables.ps1.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot/../.."
$bp = Join-Path $root 'assets/pack/bp'
$rp = Join-Path $root 'assets/pack/rp'

function Write-Json([string]$Path, $Value) {
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force $dir | Out-Null
    $text = ($Value | ConvertTo-Json -Depth 32) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($Path, $text + "`n", [Text.UTF8Encoding]::new($false))
}
function Write-Text([string]$Path, [string[]]$Lines) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}

$eggs = [ordered]@{
    red_blue    = @{ en = 'Red Parrot Egg'; zh = '红色鹦鹉蛋' }
    blue        = @{ en = 'Blue Parrot Egg'; zh = '蓝色鹦鹉蛋' }
    green       = @{ en = 'Green Parrot Egg'; zh = '绿色鹦鹉蛋' }
    yellow_blue = @{ en = 'Cyan Parrot Egg'; zh = '青色鹦鹉蛋' }
    gray        = @{ en = 'Gray Parrot Egg'; zh = '灰色鹦鹉蛋' }
}

# ---- items --------------------------------------------------------------------------------------
function New-LpItem([string]$Id, [hashtable]$Components, [string]$Category = 'items', [string]$Group = $null, [bool]$Hidden = $false) {
    $menu = [ordered]@{ category = $Category }
    if ($Group) { $menu.group = $Group }
    if ($Hidden) { $menu.category = 'none'; $menu.is_hidden_in_commands = $false }
    $c = [ordered]@{ 'minecraft:icon' = "lepton_throwables_$Id" }
    foreach ($k in ($Components.Keys | Sort-Object)) { $c[$k] = $Components[$k] }
    Write-Json "$bp/items/$Id.json" ([ordered]@{
            format_version  = '1.21.60'
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Id"; menu_category = $menu }
                components  = $c
            }
        })
}

function Rang-Components([int]$Durability, [int]$Damage, [int]$Enchant, [string]$Repair, [bool]$Fire) {
    $c = @{
        'minecraft:max_stack_size' = 1
        'minecraft:hand_equipped'  = $true
        'minecraft:durability'     = [ordered]@{ max_durability = $Durability }
        'minecraft:damage'         = $Damage
        'minecraft:enchantable'    = [ordered]@{ slot = 'pickaxe'; value = $Enchant }
        'minecraft:cooldown'       = [ordered]@{ category = 'lepton_pickarang'; duration = 0.5 }
        'minecraft:repairable'     = [ordered]@{
            repair_items = @(
                [ordered]@{ items = @($Repair); repair_amount = 'query.max_durability * 0.25' }
            )
        }
        'minecraft:tags'           = [ordered]@{ tags = @('lepton:pickarangs') }
    }
    return $c
}
New-LpItem 'pickarang' (Rang-Components 800 2 10 'minecraft:diamond' $false) 'equipment' 'minecraft:itemGroup.name.pickaxe'
New-LpItem 'flamerang' (Rang-Components 1040 3 15 'minecraft:netherite_ingot' $true) 'equipment' 'minecraft:itemGroup.name.pickaxe'

New-LpItem 'torch_arrow' @{
    'minecraft:max_stack_size' = 64
    'minecraft:projectile'     = [ordered]@{ projectile_entity = 'lepton:torch_arrow'; minimum_critical_power = 1.25 }
    'minecraft:tags'           = [ordered]@{ tags = @('minecraft:arrow') }
} 'equipment' 'minecraft:itemGroup.name.arrow'

foreach ($v in $eggs.Keys) {
    New-LpItem "egg_parrot_$v" @{
        'minecraft:max_stack_size' = 16
        'minecraft:throwable'      = [ordered]@{ do_swing_animation = $true; launch_power_scale = 1.0; max_launch_power = 1.0 }
        'minecraft:projectile'     = [ordered]@{ projectile_entity = "lepton:parrot_egg_$v"; minimum_critical_power = 1.0 }
    } 'items' 'minecraft:itemGroup.name.egg'
}

New-LpItem 'bottled_cloud' @{ 'minecraft:max_stack_size' = 64 } 'items'
New-LpItem 'slime_in_a_bucket' @{ 'minecraft:max_stack_size' = 1 } 'items' 'minecraft:itemGroup.name.bucket'
New-LpItem 'slime_in_a_bucket_excited' @{ 'minecraft:max_stack_size' = 1 } 'items' $null $true

# ---- the cloud block ----------------------------------------------------------------------------
$sizes = 0..9
$vis = [ordered]@{}
foreach ($s in $sizes) { $vis["s$s"] = "q.block_state('lepton:size') == $s" }
Write-Json "$bp/blocks/cloud.json" ([ordered]@{
        format_version    = '1.26.0'
        'minecraft:block' = [ordered]@{
            description = [ordered]@{
                identifier    = 'lepton:cloud'
                menu_category = [ordered]@{ category = 'none'; is_hidden_in_commands = $false }
                states        = [ordered]@{ 'lepton:size' = @($sizes) }
            }
            components  = [ordered]@{
                'minecraft:collision_box'              = $false
                'minecraft:selection_box'              = $true
                'minecraft:destructible_by_mining'     = [ordered]@{ seconds_to_destroy = 0 }
                'minecraft:destructible_by_explosion'  = [ordered]@{ explosion_resistance = 0 }
                'minecraft:light_dampening'            = 0
                'minecraft:map_color'                  = '#a4a8b8'
                'minecraft:movable'                    = [ordered]@{ movement_type = 'immovable' }
                'minecraft:geometry'                   = [ordered]@{ identifier = 'geometry.lepton.cloud'; bone_visibility = $vis }
                'minecraft:material_instances'         = [ordered]@{ '*' = [ordered]@{ texture = 'lepton_throwables_cloud'; render_method = 'alpha_test'; ambient_occlusion = $false } }
                'minecraft:loot'                       = 'loot_tables/blocks/cloud.json'
            }
        }
    })
Write-Json "$bp/loot_tables/blocks/cloud.json" ([ordered]@{ pools = @() })

$faceUv = [ordered]@{}
foreach ($f in @('north', 'south', 'east', 'west', 'up', 'down')) { $faceUv[$f] = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) } }
$bones = foreach ($s in $sizes) {
    $px = [Math]::Round(9.6 * (10 - $s) / 10, 2)
    [ordered]@{
        name  = "s$s"
        pivot = @(0, 8, 0)
        cubes = @([ordered]@{ origin = @((-$px / 2), (8 - $px / 2), (-$px / 2)); size = @($px, $px, $px); uv = $faceUv })
    }
}
Write-Json "$rp/models/blocks/lepton_cloud.geo.json" ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{ identifier = 'geometry.lepton.cloud'; texture_width = 16; texture_height = 16 }
                bones       = @($bones)
            })
    })

# ---- recipes ------------------------------------------------------------------------------------
function Write-Shaped([string]$Name, [string[]]$Pattern, $Key, [string]$Result) {
    Write-Json "$bp/recipes/$Name.json" ([ordered]@{
            format_version            = '1.20.10'
            'minecraft:recipe_shaped' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @('crafting_table')
                pattern     = $Pattern
                key         = $Key
                unlock      = @([ordered]@{ item = 'minecraft:diamond' })
                result      = [ordered]@{ item = $Result; count = 1 }
            }
        })
}
Write-Shaped 'pickarang_heart' @('DWH', '  W', '  D') ([ordered]@{
        D = [ordered]@{ item = 'minecraft:diamond' }; W = [ordered]@{ tag = 'minecraft:planks' }; H = [ordered]@{ item = 'lepton:diamond_heart' }
    }) 'lepton:pickarang'
Write-Shaped 'pickarang_no_heart' @('DWD', '  W', '  D') ([ordered]@{
        D = [ordered]@{ item = 'minecraft:diamond' }; W = [ordered]@{ tag = 'minecraft:planks' }
    }) 'lepton:pickarang'
Write-Json "$bp/recipes/flamerang_smithing.json" ([ordered]@{
        format_version                       = '1.20.10'
        'minecraft:recipe_smithing_transform' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:flamerang_smithing' }
            tags        = @('smithing_table')
            template    = 'minecraft:netherite_upgrade_smithing_template'
            base        = 'lepton:pickarang'
            addition    = 'minecraft:netherite_ingot'
            result      = 'lepton:flamerang'
        }
    })
Write-Json "$bp/recipes/torch_arrow.json" ([ordered]@{
        format_version               = '1.20.10'
        'minecraft:recipe_shapeless' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:torch_arrow' }
            tags        = @('crafting_table')
            ingredients = @([ordered]@{ item = 'minecraft:torch' }, [ordered]@{ item = 'minecraft:arrow' })
            unlock      = @([ordered]@{ item = 'minecraft:arrow' })
            result      = [ordered]@{ item = 'lepton:torch_arrow'; count = 1 }
        }
    })

# ---- entities (behaviour) -----------------------------------------------------------------------
function Rang-Entity([string]$Id, [bool]$Fire) {
    $c = [ordered]@{
        'minecraft:type_family'   = [ordered]@{ family = @('lepton_pickarang', 'inanimate') }
        'minecraft:collision_box' = [ordered]@{ width = 0.4; height = 0.4 }
        'minecraft:physics'       = [ordered]@{ has_gravity = $false; has_collision = $false }
        'minecraft:pushable'      = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
        'minecraft:damage_sensor' = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
        'minecraft:health'        = [ordered]@{ value = 1; max = 1 }
        'minecraft:knockback_resistance' = [ordered]@{ value = 1.0 }
    }
    if ($Fire) { $c['minecraft:fire_immune'] = @{} }
    Write-Json "$bp/entities/$Id.json" ([ordered]@{
            format_version     = '1.21.0'
            'minecraft:entity' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Id"; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
                components  = $c
            }
        })
}
Rang-Entity 'pickarang' $false
Rang-Entity 'flamerang' $true

Write-Json "$bp/entities/torch_arrow.json" ([ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:torch_arrow'; runtime_identifier = 'minecraft:arrow'; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
            components  = [ordered]@{
                'minecraft:type_family'   = [ordered]@{ family = @('arrow', 'lepton_torch_arrow') }
                'minecraft:collision_box' = [ordered]@{ width = 0.25; height = 0.25 }
                'minecraft:projectile'    = [ordered]@{
                    on_hit           = [ordered]@{
                        impact_damage   = [ordered]@{ damage = @(1, 4); knockback = $true; semi_random_diff_damage = $false; destroy_on_hit = $true }
                        stick_in_ground = [ordered]@{ shake_time = 0.35 }
                        arrow_effect    = @{}
                    }
                    hit_sound        = 'bow.hit'
                    power            = 1.6
                    gravity          = 0.05
                    uncertainty_base = 16
                    uncertainty_multiplier = 4
                    anchor           = 1
                    should_bounce    = $true
                    offset           = @(0, -0.1, 0)
                }
                'minecraft:physics'       = @{}
                'minecraft:pushable'      = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $true }
                'minecraft:conditional_bandwidth_optimization' = [ordered]@{
                    default_values = [ordered]@{ max_optimized_distance = 80.0; max_dropped_ticks = 7; use_motion_prediction_hints = $true }
                }
            }
        }
    })

foreach ($v in $eggs.Keys) {
    Write-Json "$bp/entities/parrot_egg_$v.json" ([ordered]@{
            format_version     = '1.21.0'
            'minecraft:entity' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:parrot_egg_$v"; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
                components  = [ordered]@{
                    'minecraft:type_family'   = [ordered]@{ family = @('lepton_parrot_egg', 'inanimate') }
                    'minecraft:collision_box' = [ordered]@{ width = 0.25; height = 0.25 }
                    'minecraft:projectile'    = [ordered]@{
                        on_hit           = [ordered]@{
                            impact_damage    = [ordered]@{ damage = 0; knockback = $true; destroy_on_hit = $false }
                            remove_on_hit    = @{}
                            particle_on_hit  = [ordered]@{ particle_type = 'iconcrack'; num_particles = 8; on_entity_hit = $true; on_other_hit = $true }
                        }
                        power            = 1.5
                        gravity          = 0.03
                        angle_offset     = 0.0
                        uncertainty_base = 1.0
                        shoot_sound      = 'bow'
                    }
                    'minecraft:physics'       = @{}
                    'minecraft:pushable'      = [ordered]@{ is_pushable = $true; is_pushable_by_piston = $true }
                }
            }
        })
}

# ---- entities (client) --------------------------------------------------------------------------
function Rang-Client([string]$Id) {
    Write-Json "$rp/entity/$Id.entity.json" ([ordered]@{
            format_version            = '1.10.0'
            'minecraft:client_entity' = [ordered]@{
                description = [ordered]@{
                    identifier         = "lepton:$Id"
                    materials          = [ordered]@{ default = 'entity_alphatest' }
                    textures           = [ordered]@{ default = "textures/items/$Id" }
                    geometry           = [ordered]@{ default = 'geometry.lepton.flat_item' }
                    animations         = [ordered]@{ spin = 'animation.lepton.pickarang.spin' }
                    scripts            = [ordered]@{ animate = @('spin') }
                    render_controllers = @('controller.render.default')
                }
            }
        })
}
Rang-Client 'pickarang'
Rang-Client 'flamerang'

Write-Json "$rp/entity/torch_arrow.entity.json" ([ordered]@{
        format_version            = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:torch_arrow'
                materials          = [ordered]@{ default = 'arrow' }
                textures           = [ordered]@{ default = 'textures/entity/lepton/torch_arrow' }
                geometry           = [ordered]@{ default = 'geometry.lepton.torch_arrow' }
                animations         = [ordered]@{ move = 'animation.arrow.move' }
                scripts            = [ordered]@{
                    pre_animation = @(
                        'variable.shake = query.shake_time - query.frame_alpha;',
                        'variable.shake_power = variable.shake > 0.0 ? -Math.sin(variable.shake * 200.0) * variable.shake : 0.0;'
                    )
                    animate       = @('move')
                }
                render_controllers = @('controller.render.arrow')
            }
        }
    })

foreach ($v in $eggs.Keys) {
    Write-Json "$rp/entity/parrot_egg_$v.entity.json" ([ordered]@{
            format_version            = '1.10.0'
            'minecraft:client_entity' = [ordered]@{
                description = [ordered]@{
                    identifier         = "lepton:parrot_egg_$v"
                    materials          = [ordered]@{ default = 'egg' }
                    textures           = [ordered]@{ default = "textures/items/egg_parrot_$v" }
                    geometry           = [ordered]@{ default = 'geometry.item_sprite' }
                    render_controllers = @('controller.render.item_sprite')
                    animations         = [ordered]@{ flying = 'animation.actor.billboard' }
                    scripts            = [ordered]@{ animate = @('flying') }
                }
            }
        })
}

# The vanilla parrot, plus the kotobirb look (grey parrots the server marks with mark variant 1).
Write-Json "$rp/entity/parrot.entity.json" ([ordered]@{
        format_version            = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier            = 'minecraft:parrot'
                materials             = [ordered]@{ default = 'parrot' }
                textures              = [ordered]@{
                    blue        = 'textures/entity/parrot/parrot_blue'
                    green       = 'textures/entity/parrot/parrot_green'
                    red_blue    = 'textures/entity/parrot/parrot_red_blue'
                    yellow_blue = 'textures/entity/parrot/parrot_yellow_blue'
                    grey        = 'textures/entity/parrot/parrot_grey'
                }
                geometry              = [ordered]@{ default = 'geometry.parrot' }
                scripts               = [ordered]@{
                    pre_animation = @(
                        'variable.state = query.is_dancing ? 3 : (query.is_sitting ? 2 : (!query.is_on_ground && !query.is_jumping && !query.is_riding ? 0 : 1));',
                        'variable.dance.x = Math.cos(query.life_time * 57.3 * 20.0);',
                        'variable.dance.y = -Math.sin(query.life_time * 57.3 * 20.0);',
                        'variable.wing_flap = ((math.sin(query.wing_flap_position * 57.3) + 1) * query.wing_flap_speed);'
                    )
                }
                animations            = [ordered]@{
                    moving         = 'animation.parrot.moving'
                    base           = 'animation.parrot.base'
                    dance          = 'animation.parrot.dance'
                    sitting        = 'animation.parrot.sitting'
                    flying         = 'animation.parrot.flying'
                    standing       = 'animation.parrot.standing'
                    look_at_target = 'animation.common.look_at_target'
                }
                animation_controllers = @([ordered]@{ setup = 'controller.animation.parrot.setup' }, [ordered]@{ move = 'controller.animation.parrot.move' })
                render_controllers    = @('controller.render.lepton_parrot')
                spawn_egg             = [ordered]@{ texture = 'spawn_egg_parrot' }
            }
        }
    })
Write-Json "$rp/render_controllers/lepton_parrot.render_controllers.json" ([ordered]@{
        format_version     = '1.8.0'
        render_controllers = [ordered]@{
            'controller.render.lepton_parrot' = [ordered]@{
                arrays        = [ordered]@{ textures = [ordered]@{ 'Array.skins' = @('Texture.red_blue', 'Texture.blue', 'Texture.green', 'Texture.yellow_blue', 'Texture.grey') } }
                geometry      = 'Geometry.default'
                materials     = @([ordered]@{ '*' = 'Material.default' })
                textures      = @('Array.skins[query.variant]')
                overlay_color = [ordered]@{ r = 1.0; g = 0.55; b = 0.8; a = "(query.variant == 4 && query.mark_variant == 1) ? 0.45 : 0.0" }
            }
        }
    })

# ---- geometry and animations --------------------------------------------------------------------
Write-Json "$rp/models/entity/lepton_throwables.geo.json" ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @(
            [ordered]@{
                description = [ordered]@{ identifier = 'geometry.lepton.flat_item'; texture_width = 16; texture_height = 16; visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
                bones       = @([ordered]@{
                        name  = 'body'
                        pivot = @(0, 3.2, 0)
                        cubes = @([ordered]@{
                                origin = @(-8, 3.2, -8); size = @(16, 0, 16)
                                uv     = [ordered]@{ up = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; down = [ordered]@{ uv = @(0, 16); uv_size = @(16, -16) } }
                            })
                    })
            },
            [ordered]@{
                description = [ordered]@{ identifier = 'geometry.lepton.torch_arrow'; texture_width = 32; texture_height = 32; visible_bounds_width = 1; visible_bounds_height = 1; visible_bounds_offset = @(0, 0, 0) }
                bones       = @([ordered]@{
                        name  = 'body'
                        pivot = @(0, 0, 0)
                        cubes = @(
                            [ordered]@{ origin = @(0, -2.5, -8); size = @(0, 5, 16); uv = [ordered]@{ east = [ordered]@{ uv = @(0, 0); uv_size = @(16, 5) }; west = [ordered]@{ uv = @(16, 0); uv_size = @(-16, 5) } } },
                            [ordered]@{ origin = @(-2.5, 0, -8); size = @(5, 0, 16); uv = [ordered]@{ up = [ordered]@{ uv = @(16, 0); uv_size = @(-16, 5) }; down = [ordered]@{ uv = @(0, 0); uv_size = @(16, 5) } } }
                        )
                    })
            }
        )
    })
Write-Json "$rp/animations/lepton_throwables.animation.json" ([ordered]@{
        format_version = '1.8.0'
        animations     = [ordered]@{
            'animation.lepton.pickarang.spin' = [ordered]@{
                loop  = $true
                bones = [ordered]@{ body = [ordered]@{ rotation = @(0, 'query.life_time * 400.0', 0) } }
            }
        }
    })

# ---- particles ----------------------------------------------------------------------------------
function Write-Dot([string]$Id, [string]$Color, [double]$Size, [string]$Life, [string]$Gravity, [int]$Count, [string]$Speed) {
    $name = $Id.Split(':')[1]
    Write-Json "$rp/particles/$name.json" ([ordered]@{
            format_version  = '1.10.0'
            particle_effect = [ordered]@{
                description = [ordered]@{ identifier = $Id; basic_render_parameters = [ordered]@{ material = 'particles_alpha'; texture = 'textures/particle/particles' } }
                components  = [ordered]@{
                    'minecraft:emitter_rate_instant'          = [ordered]@{ num_particles = $Count }
                    'minecraft:emitter_lifetime_once'         = [ordered]@{ active_time = 0 }
                    'minecraft:emitter_shape_box'             = [ordered]@{ half_dimensions = @(0.25, 0.25, 0.25); direction = 'outwards' }
                    'minecraft:particle_lifetime_expression'  = [ordered]@{ max_lifetime = $Life }
                    'minecraft:particle_initial_speed'        = $Speed
                    'minecraft:particle_motion_dynamic'       = [ordered]@{ linear_acceleration = @(0, $Gravity, 0); linear_drag_coefficient = 1.5 }
                    'minecraft:particle_appearance_billboard' = [ordered]@{
                        size = @($Size, $Size); facing_camera_mode = 'rotate_xyz'
                        uv   = [ordered]@{ texture_width = 128; texture_height = 128; uv = @(8, 0); uv_size = @(8, 8) }
                    }
                    'minecraft:particle_appearance_tinting'   = [ordered]@{ color = $Color }
                    'minecraft:particle_appearance_lighting'  = @{}
                }
            }
        })
}
Write-Dot 'lepton:pike_ash' '#ff6b6560' 0.05 'math.random(1.5, 3.0)' '-0.3' 1 '0.05'
Write-Dot 'lepton:pike_spore' '#ff2fc6c0' 0.06 'math.random(2.0, 4.0)' '0.05' 1 '0.05'
Write-Dot 'lepton:cloud_puff' '#ffffffff' 0.25 'math.random(0.6, 1.4)' '0.05' 1 '0.1'

# ---- texture atlases, block sounds, names, sounds -----------------------------------------------
$itemTex = [ordered]@{}
foreach ($i in @('pickarang', 'flamerang', 'torch_arrow', 'bottled_cloud', 'slime_in_a_bucket', 'slime_in_a_bucket_excited') + @($eggs.Keys | ForEach-Object { "egg_parrot_$_" })) {
    $itemTex["lepton_throwables_$i"] = [ordered]@{ textures = "textures/items/$i" }
}
Write-Json "$rp/item_texture.d/throwables.json" $itemTex
Write-Json "$rp/terrain_texture.d/throwables.json" ([ordered]@{ lepton_throwables_cloud = [ordered]@{ textures = 'textures/blocks/cloud' } })
Write-Json "$rp/blocks.d/throwables.json" ([ordered]@{ 'lepton:cloud' = [ordered]@{ sound = 'cloth' } })

$sound = { param($name, $pitch, $volume) [ordered]@{ name = $name; pitch = $pitch; volume = $volume } }
Write-Json "$rp/sounds.d/throwables.json" ([ordered]@{
        'lepton.pickarang.throw'  = [ordered]@{ category = 'neutral'; sounds = @((& $sound 'sounds/random/bow' 1.2 1.0)) }
        'lepton.pickarang.clank'  = [ordered]@{ category = 'player'; sounds = @((& $sound 'sounds/random/anvil_land' 2.0 0.5)) }
        'lepton.pickarang.spark'  = [ordered]@{ category = 'player'; sounds = @((& $sound 'sounds/random/fizz' 0.5 0.5)) }
        'lepton.pickarang.pickup' = [ordered]@{ category = 'player'; sounds = @((& $sound 'sounds/random/pop' 1.125 0.5), (& $sound 'sounds/random/pop' 1.0 0.5), (& $sound 'sounds/random/pop' 0.875 0.5), (& $sound 'sounds/random/pop' 0.75 0.5)) }
        'lepton.parrot.egg'       = [ordered]@{ category = 'neutral'; sounds = @((& $sound 'sounds/mob/chicken/plop' 1.0 1.0)) }
    })

$en = @(
    'item.lepton:pickarang.name=Pickarang', 'item.lepton:flamerang.name=Flamarang', 'item.lepton:torch_arrow.name=Torch Arrow',
    'item.lepton:bottled_cloud.name=Cloud in a Bottle', 'item.lepton:slime_in_a_bucket.name=Bucket of Slime',
    'item.lepton:slime_in_a_bucket_excited.name=Bucket of Slime', 'tile.lepton:cloud.name=Cloud',
    'entity.lepton:pickarang.name=Pickarang', 'entity.lepton:flamerang.name=Flamarang', 'entity.lepton:torch_arrow.name=Torch Arrow'
)
$zh = @(
    'item.lepton:pickarang.name=皮卡镖', 'item.lepton:flamerang.name=界炎镖', 'item.lepton:torch_arrow.name=火把箭',
    'item.lepton:bottled_cloud.name=瓶中云', 'item.lepton:slime_in_a_bucket.name=史莱姆桶',
    'item.lepton:slime_in_a_bucket_excited.name=史莱姆桶', 'tile.lepton:cloud.name=云',
    'entity.lepton:pickarang.name=皮卡镖', 'entity.lepton:flamerang.name=界炎镖', 'entity.lepton:torch_arrow.name=火把箭'
)
foreach ($v in $eggs.Keys) {
    $en += "item.lepton:egg_parrot_$v.name=$($eggs[$v].en)"; $zh += "item.lepton:egg_parrot_$v.name=$($eggs[$v].zh)"
    $en += "entity.lepton:parrot_egg_$v.name=Parrot Egg"; $zh += "entity.lepton:parrot_egg_$v.name=鹦鹉蛋"
}
Write-Text "$rp/texts.d/en_US/throwables.lang" $en
Write-Text "$rp/texts.d/zh_CN/throwables.lang" $zh

Write-Host 'throwables pack written'
