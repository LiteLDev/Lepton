# U7 newwoods: the azalea, trumpet (blossom) and ashen (ancient) wood sets with their Quark variants,
# leaves, saplings, hedges, leaf carpets and the Enchanted Fruit (docs/spec/newwoods.md).
#
#   pwsh -NoProfile -File tools/pack/newwoods.ps1      # regenerates and overwrites the committed output
#
# The variants (vertical planks, bookshelves, ladders, posts, hollow logs, hedges, leaf carpets) come
# from U6's generators (tools/pack/woodvariants.ps1 + tools/textures/woodvariants.ps1), so they get
# U6's block tags and native behaviour. Their shared geometries (models/blocks/lepton_wv_*.geo.json)
# belong to U6 and are not written here.

. "$PSScriptRoot/woodvariants.ps1"        # lib.ps1, New-Wv* generators, U6 texture functions
. "$PSScriptRoot/../textures/newwoods.ps1"

# U6 owns the wv geometries: only reference them.
function Write-WvGeometry([string]$Name, $Bones) { return "geometry.lepton.wv_$Name" }

$script:NwGeoFormat = '1.21.0'

# ------------------------------------------------------------------------------------------------
# Unit-specific blocks
# ------------------------------------------------------------------------------------------------

# Two crossed planes (saplings), 16 px wide, 16 px high.
function Write-NwCrossGeometry {
    $plane = {
        param($rot)
        [ordered]@{
            origin = @(-8, 0, 0); size = @(16, 16, 0); pivot = @(0, 0, 0); rotation = @(0, $rot, 0)
            uv = [ordered]@{
                north = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16); material_instance = 'cross' }
                south = [ordered]@{ uv = @(16, 0); uv_size = @(-16, 16); material_instance = 'cross' }
            }
        }
    }
    $geo = [ordered]@{
        format_version       = $script:NwGeoFormat
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = 'geometry.lepton.nw_cross'; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0)
                }
                bones       = @([ordered]@{ name = 'cross'; pivot = @(0, 0, 0); cubes = @((& $plane 45), (& $plane -45)) })
            })
    }
    Write-PackJson -Path (Join-Path $script:RP 'models/blocks/lepton_nw_cross.geo.json') -Object $geo
    return 'geometry.lepton.nw_cross'
}

# Leaves: full cube, alpha_test, light dampening 1, `lepton:persistent` (default true = player
# placed); drops come from src/feature/newwoods (Java loot with fortune), so the loot table is empty.
function New-NwLeaves {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [Parameter(Mandatory)][string]$ModuleTag, [string]$MapColor)
    $c = Get-BaseComponents @{ Hardness = 0.2; Resistance = 0.2; MapColor = $MapColor; Flammable = $false; Tool = 'hoe'; Tags = @('lepton:nw_leaves', $ModuleTag) }
    # Java leaves: fire spread 30 / 60
    $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = 30; destroy_chance_modifier = 60 }
    $c['minecraft:light_dampening'] = 1
    $c['minecraft:geometry'] = 'minecraft:geometry.full_block'
    $c['minecraft:material_instances'] = [ordered]@{ '*' = [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = 'alpha_test' } }
    $p = Get-Path $Id
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$p.json") ([ordered]@{ pools = @() })
    $c['minecraft:loot'] = "loot_tables/blocks/$p.json"
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.leaves' }) `
        -States ([ordered]@{ 'lepton:persistent' = @($true, $false) }) -Components $c
    Register-BlockClient $Id 'grass' $Name
}

# Sapling: crossed planes, no collision, placed on dirt-like blocks only, flower-pottable, flat item.
$script:SaplingSoil = @(
    'minecraft:grass_block', 'minecraft:dirt', 'minecraft:coarse_dirt', 'minecraft:podzol', 'minecraft:dirt_with_roots',
    'minecraft:moss_block', 'minecraft:mud', 'minecraft:muddy_mangrove_roots', 'minecraft:farmland', 'minecraft:mycelium')
function New-NwSapling {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [Parameter(Mandatory)][string]$ModuleTag, [string]$MapColor)
    $geo = Write-NwCrossGeometry
    $c = Get-BaseComponents @{ Hardness = 0; Resistance = 0; MapColor = $MapColor; Flammable = $false; Tool = ''; Tags = @('lepton:nw_sapling', $ModuleTag) }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:collision_box'] = $false
    $c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 2 0 2 14 12 14)
    $mat = [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = 'alpha_test'; face_dimming = $false; ambient_occlusion = $false }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = $mat; cross = $mat }
    $c['minecraft:geometry'] = $geo
    $c['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up'); block_filter = @($script:SaplingSoil) }) }
    $c['minecraft:flower_pottable'] = [ordered]@{}
    $c['minecraft:embedded_visual'] = [ordered]@{ geometry = $geo; material_instances = [ordered]@{ '*' = $mat; cross = $mat } }
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.sapling' }) `
        -States ([ordered]@{ 'lepton:stage' = @(0, 1) }) -Components $c
    Register-BlockClient $Id 'grass' $Name
    Add-PackText "item.$Id.name" $Name
    $icon = Add-ItemTexture $Texture
    Write-PackJson (Join-Path $script:BP "items/$(Get-Path $Id).json") ([ordered]@{
            format_version   = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $Id; menu_category = [ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.sapling' } }
                components  = [ordered]@{
                    'minecraft:icon'           = $icon
                    'minecraft:block_placer'   = [ordered]@{ block = $Id; replace_block_item = $true }
                    'minecraft:max_stack_size' = 64
                }
            }
        })
}

# Falling leaf particle of a trumpet colour (Java TerrainParticle of the leaves block, spawned with
# velocity (wind x, -1, wind z); docs/spec/newwoods.md §3.9).
function Write-NwLeafParticle([string]$Id, [string]$Texture) {
    # Java: wind from the game time (the world time here): strength 5 + cos(t / 2000) * 2,
    # x = cos(t / 1200) * strength, z = sin(t / 1000) * strength; molang trig functions take degrees
    $init = @(
        't.t = (query.day + query.time_of_day) * 24000;',
        't.s = 5 + math.cos(t.t / 2000 * 57.29578) * 2;',
        't.ex = math.cos(t.t / 1200 * 57.29578) * t.s + variable.emitter_random_1 * 0.8 - 0.4;',
        't.ey = -1 + variable.emitter_random_2 * 0.8 - 0.4;',
        't.ez = math.sin(t.t / 1000 * 57.29578) * t.s + variable.emitter_random_3 * 0.8 - 0.4;',
        't.e = math.sqrt(t.ex * t.ex + t.ey * t.ey + t.ez * t.ez);',
        't.d = (variable.emitter_random_4 + variable.emitter_random_1 + 1) * 0.15 * 0.4;',
        'variable.vx = t.ex / t.e * t.d * 20;',
        'variable.vy = (t.ey / t.e * t.d + 0.1) * 20;',
        'variable.vz = t.ez / t.e * t.d * 20;',
        'variable.spd = math.sqrt(variable.vx * variable.vx + variable.vy * variable.vy + variable.vz * variable.vz);'
    ) -join ' '
    $p = [ordered]@{
        format_version  = '1.10.0'
        particle_effect = [ordered]@{
            description = [ordered]@{
                identifier              = $Id
                basic_render_parameters = [ordered]@{ material = 'particles_alpha'; texture = $Texture }
            }
            components  = [ordered]@{
                'minecraft:emitter_initialization'        = [ordered]@{ creation_expression = $init }
                'minecraft:emitter_rate_instant'          = [ordered]@{ num_particles = 1 }
                'minecraft:emitter_lifetime_once'         = [ordered]@{ active_time = 0 }
                'minecraft:emitter_shape_point'           = [ordered]@{ offset = @(0, 0, 0); direction = @('variable.vx', 'variable.vy', 'variable.vz') }
                'minecraft:particle_initial_speed'        = 'variable.spd'
                'minecraft:particle_lifetime_expression'  = [ordered]@{ max_lifetime = '4 / (variable.particle_random_2 * 0.9 + 0.1) / 20' }
                'minecraft:particle_motion_dynamic'       = [ordered]@{ linear_acceleration = @(0, -16, 0); linear_drag_coefficient = 0.4 }
                'minecraft:particle_motion_collision'     = [ordered]@{ collision_radius = 0.05; coefficient_of_restitution = 0; collision_drag = 10 }
                'minecraft:particle_appearance_billboard' = [ordered]@{
                    size        = @('0.1 * (variable.particle_random_3 * 0.5 + 0.5)', '0.1 * (variable.particle_random_3 * 0.5 + 0.5)')
                    facing_camera_mode = 'lookat_xyz'
                    uv          = [ordered]@{
                        texture_width = 16; texture_height = 16
                        uv = @('math.floor(variable.particle_random_3 * 12)', 'math.floor(variable.particle_random_4 * 12)')
                        uv_size = @(4, 4)
                    }
                }
                'minecraft:particle_appearance_tinting'   = [ordered]@{ color = @(0.6, 0.6, 0.6, 1.0) }
                'minecraft:particle_appearance_lighting'  = [ordered]@{}
            }
        }
    }
    Write-PackJson (Join-Path $script:RP "particles/$(($Id -split ':')[1]).json") $p
}

# The Enchanted Fruit (Quark AncientFruitItem): food 4, saturation 0.3, always edible.
function New-NwFruit {
    $id = 'lepton:ancient_fruit'
    Add-PackText "item.$id.name" @{ en = 'Enchanted Fruit'; zh = '附魔水果' }
    $icon = Add-ItemTexture 'textures/items/newwoods/ancient_fruit'
    Write-PackJson (Join-Path $script:BP 'items/ancient_fruit.json') ([ordered]@{
            format_version   = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $id; menu_category = [ordered]@{ category = 'equipment'; group = 'minecraft:itemGroup.name.miscFood' } }
                components  = [ordered]@{
                    'minecraft:icon'           = $icon
                    'minecraft:max_stack_size' = 64
                    'minecraft:food'           = [ordered]@{ nutrition = 4; saturation_modifier = 0.3; can_always_eat = $true }
                    'minecraft:use_animation'  = 'eat'
                    'minecraft:use_modifiers'  = [ordered]@{ use_duration = 1.6; movement_modifier = 0.35 }
                    'minecraft:tags'           = [ordered]@{ tags = @('minecraft:is_food') }
                }
            }
        })
}

# ------------------------------------------------------------------------------------------------
# The woods
# ------------------------------------------------------------------------------------------------

function Invoke-NewWoodsPack {
    Start-PackUnit 'newwoods'
    $blocksDir = Join-Path $script:RP 'textures/blocks/newwoods'
    $itemsDir = Join-Path $script:RP 'textures/items/newwoods'
    foreach ($p in (Write-NewWoodsTextures $blocksDir $itemsDir)) { $null = Add-PackFile $p }
    $TB = 'textures/blocks/newwoods/'
    $TI = 'textures/items/newwoods/'

    # name roots (Quark en_us / zh_cn), module tag, map colours (planks, bark)
    $woods = [ordered]@{
        azalea  = @{ en = 'Azalea'; zh = '杜鹃'; tag = 'lepton:nw_azalea_wood'; color = '#7fcc19'; bark = '#664c33' }
        blossom = @{ en = 'Trumpet'; zh = '喇叭'; tag = 'lepton:nw_blossom_trees'; color = '#993333'; bark = '#664c33' }
        ancient = @{ en = 'Ashen'; zh = '苍白'; tag = 'lepton:nw_ancient_wood'; color = '#d1b1a1'; bark = '#d1b1a1' }
    }
    foreach ($w in $woods.Keys) {
        $d = $woods[$w]
        $en = $d.en; $zh = $d.zh
        $parts = @('log', 'wood', 'stripped_log', 'stripped_wood', 'planks', 'fence', 'fence_gate', 'door', 'trapdoor', 'button', 'pressure_plate')
        $names = @{
            log = @{ en = "$en Log"; zh = "${zh}原木" }; wood = @{ en = "$en Wood"; zh = "${zh}木" }
            stripped_log = @{ en = "Stripped $en Log"; zh = "去皮${zh}原木" }; stripped_wood = @{ en = "Stripped $en Wood"; zh = "去皮${zh}木" }
            planks = @{ en = "$en Planks"; zh = "${zh}木板" }
            fence = @{ en = "$en Fence"; zh = "${zh}木栅栏" }; fence_gate = @{ en = "$en Fence Gate"; zh = "${zh}木栅栏门" }
            door = @{ en = "$en Door"; zh = "${zh}木门" }; trapdoor = @{ en = "$en Trapdoor"; zh = "${zh}木活板门" }
            button = @{ en = "$en Button"; zh = "${zh}木按钮" }; pressure_plate = @{ en = "$en Pressure Plate"; zh = "${zh}木压力板" }
        }
        $tex = @{
            log_side = "$TB${w}_log"; log_top = "$TB${w}_log_top"; stripped_log_side = "${TB}stripped_${w}_log"; stripped_log_top = "${TB}stripped_${w}_log_top"
            planks = "$TB${w}_planks"; door_top = "$TB${w}_door_top"; door_bottom = "$TB${w}_door_bottom"; door_item = "$TI${w}_door"; trapdoor = "$TB${w}_trapdoor"
        }
        $id = New-WoodSet -Wood $w -Name @{ en = $en; zh = $zh } -Textures $tex -Parts $parts -Names $names -MapColor $d.color -Tags @($d.tag)
        # Quark's planks shapes keep the "_planks_" in their ids
        $null = New-ShapeSet -Material "${w}_planks" -BaseBlock "lepton:${w}_planks" -Shapes stairs, slab, vertical_slab -Textures $tex.planks `
            -Name @{ en = "$en Planks"; zh = "${zh}木板" } -Names @{
                stairs = @{ en = "$en Stairs"; zh = "${zh}木楼梯" }; slab = @{ en = "$en Slab"; zh = "${zh}木台阶" }
                vertical_slab = @{ en = "$en Vertical Slab"; zh = "${zh}木竖直台阶" }
            } -Sound wood -Hardness 2 -Resistance 3 -MapColor $d.color -Flammable -Tool axe -Tags @($d.tag, 'lepton:wooden')

        $planks = "lepton:${w}_planks"
        $logs = @("lepton:${w}_log", "lepton:${w}_wood", "lepton:stripped_${w}_log", "lepton:stripped_${w}_wood")
        $common = @{ MapColor = $d.color }

        # Quark wood set variants (U6 generators)
        New-WvVerticalPlanks -Id "lepton:vertical_${w}_planks" -Texture "${TB}vertical_${w}_planks" -Planks $planks @common `
            -Name @{ en = "Vertical $en Planks"; zh = "竖纹${zh}木板" }
        New-WvBookshelf -Id "lepton:${w}_bookshelf" -SideTexture "$TB${w}_bookshelf" -TopTexture "$TB${w}_planks" -Planks $planks @common `
            -Name @{ en = "$en Bookshelf"; zh = "${zh}木书架" }
        New-WvLadder -Id "lepton:${w}_ladder" -Texture "$TB${w}_ladder" -Planks $planks @common -Name @{ en = "$en Ladder"; zh = "${zh}木梯子" }
        New-WvPost -Id "lepton:${w}_post" -SideTexture "$TB${w}_log" @common -Strippable -Name @{ en = "$en Post"; zh = "${zh}木杆" }
        New-WvPost -Id "lepton:stripped_${w}_post" -SideTexture "${TB}stripped_${w}_log" @common -Name @{ en = "Stripped $en Post"; zh = "去皮${zh}木杆" }
        Write-ShapedRecipe "${w}_post" @('F', 'F', 'F') @{ 'F' = "lepton:${w}_wood" } "lepton:${w}_post" 8
        Write-ShapedRecipe "stripped_${w}_post" @('F', 'F', 'F') @{ 'F' = "lepton:stripped_${w}_wood" } "lepton:stripped_${w}_post" 8
        New-WvHollowLog -Id "lepton:hollow_${w}_log" -SideTexture "$TB${w}_log" -TopTexture "$TB${w}_log_top" -InsideTexture "${TB}stripped_${w}_log" `
            -Log "lepton:${w}_log" @common -Name @{ en = "Hollow $en Log"; zh = "空心${zh}原木" }

        # leaves, saplings, hedges, leaf carpets
        $trees = switch ($w) {
            'azalea' { @() }
            'ancient' { @(@{ id = 'ancient'; en = 'Ashen'; zh = '苍白'; color = '#007c00' }) }
            'blossom' {
                @(
                    @{ id = 'blue_blossom'; en = 'Frosty Trumpet'; zh = '雪霜喇叭'; color = '#6699d8' },
                    @{ id = 'lavender_blossom'; en = 'Serene Trumpet'; zh = '幽静喇叭'; color = '#f27fa5' },
                    @{ id = 'orange_blossom'; en = 'Warm Trumpet'; zh = '温暖喇叭'; color = '#9f5224' },
                    @{ id = 'yellow_blossom'; en = 'Sunny Trumpet'; zh = '阳光喇叭'; color = '#e5e533' },
                    @{ id = 'red_blossom'; en = 'Fiery Trumpet'; zh = '火热喇叭'; color = '#993333' })
            }
        }
        foreach ($t in $trees) {
            $leaves = "lepton:$($t.id)_leaves"
            New-NwLeaves -Id $leaves -Texture "$TB$($t.id)_leaves" -ModuleTag $d.tag -MapColor $t.color -Name @{ en = "$($t.en) Leaves"; zh = "$($t.zh)树叶" }
            New-NwSapling -Id "lepton:$($t.id)_sapling" -Texture "$TB$($t.id)_sapling" -ModuleTag $d.tag -MapColor $t.color `
                -Name @{ en = "$($t.en) Sapling"; zh = "$($t.zh)树苗" }
            New-WvHedge -Id "lepton:$($t.id)_hedge" -LogTexture "$TB${w}_log" -LeafTexture "$TB$($t.id)_leaves" -Tint 'none' -Leaves $leaves -Logs $logs `
                -MapColor $t.color -Name @{ en = "$($t.en) Leaf Hedge"; zh = "$($t.zh)树叶篱" }
            New-WvLeafCarpet -Id "lepton:$($t.id)_leaf_carpet" -LeafTexture "$TB$($t.id)_leaves" -Tint 'none' -Leaves $leaves -MapColor $t.color `
                -Name @{ en = "$($t.en) Leaf Carpet"; zh = "$($t.zh)树叶地毯" }
            if ($w -eq 'blossom') { Write-NwLeafParticle "lepton:$($t.id)_leaf" "$TB$($t.id)_leaves" }
        }
        if ($w -eq 'azalea') {
            # Quark with AzaleaWood: the (flowering) azalea hedges (U6's blocks) take azalea logs
            foreach ($log in $logs) {
                Write-ShapedRecipe "azalea_hedge_from_$(Get-Path $log)" @('L', 'W') @{ 'L' = 'minecraft:azalea_leaves'; 'W' = $log } 'lepton:azalea_hedge' 2
                Write-ShapedRecipe "flowering_azalea_hedge_from_$(Get-Path $log)" @('L', 'W') @{ 'L' = 'minecraft:azalea_leaves_flowered'; 'W' = $log } 'lepton:flowering_azalea_hedge' 2
            }
        }
    }
    New-NwFruit

    Complete-PackUnit
}

if ($MyInvocation.InvocationName -ne '.') { Invoke-NewWoodsPack }
