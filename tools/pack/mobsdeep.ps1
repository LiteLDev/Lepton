# U21 mobsdeep: behaviour + resource pack content of Stonelings, Forgotten, Foxhound, Wraith and the
# soul bead (entities, items, loot tables, spawn rules, client entities, geometry, animations, render
# controllers, attachable, particles, sounds, names) and the textures (tools/textures/mobsdeep.ps1).
#
#   pwsh -NoProfile -File tools/pack/mobsdeep.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
$MobsdeepDefineOnly = $true
. "$PSScriptRoot/../textures/mobsdeep.ps1"

Start-PackUnit 'mobsdeep'

function BPath([string]$Rel) { return (Join-Path $script:BP $Rel) }
function RPath([string]$Rel) { return (Join-Path $script:RP $Rel) }
function O { param([Parameter(ValueFromRemainingArguments)]$Pairs) $h = [ordered]@{}; for ($i = 0; $i -lt $Pairs.Count; $i += 2) { $h[$Pairs[$i]] = $Pairs[$i + 1] }; return $h }

$EntityFormat = '1.21.0'
$Tex = 'textures/entity/mobsdeep'
$Meats = @('minecraft:porkchop', 'minecraft:cooked_porkchop', 'minecraft:beef', 'minecraft:cooked_beef', 'minecraft:chicken',
    'minecraft:cooked_chicken', 'minecraft:mutton', 'minecraft:cooked_mutton', 'minecraft:rabbit', 'minecraft:cooked_rabbit',
    'minecraft:rotten_flesh')
$Heal = @{ 'minecraft:porkchop' = 3; 'minecraft:cooked_porkchop' = 8; 'minecraft:beef' = 3; 'minecraft:cooked_beef' = 8;
    'minecraft:chicken' = 2; 'minecraft:cooked_chicken' = 6; 'minecraft:mutton' = 2; 'minecraft:cooked_mutton' = 6;
    'minecraft:rabbit' = 3; 'minecraft:cooked_rabbit' = 5; 'minecraft:rotten_flesh' = 4 }

function Event-Set([string]$Prop, $Value) { return (O set_property (O $Prop $Value)) }
function Not-Creative { return (O test 'has_ability' subject 'other' value 'instabuild' operator '!=') }
function Is-Family([string]$F, [string]$Subject = 'other') { return (O test 'is_family' subject $Subject value $F) }

# ================================================================================================
# Geometry (from the shared model definitions)
# ================================================================================================
$models = Get-MobsdeepModels
foreach ($name in $models.Keys) {
    $m = $models[$name]
    $bones = foreach ($b in $m.Bones) {
        $j = O name $b.Name
        if ($b.Parent) { $j.parent = $b.Parent }
        $j.pivot = @($b.Pivot)
        if ($b.Rotation) { $j.rotation = @($b.Rotation) }
        if ($b.Cubes.Count) {
            $j.cubes = @($b.Cubes | ForEach-Object {
                    $c = O origin @($_.Origin) size @($_.Size) uv @($_.Uv)
                    if ($_.Inflate) { $c.inflate = $_.Inflate }
                    $c
                })
        }
        $j
    }
    Write-PackJson (RPath "models/entity/lepton_$name.geo.json") (O format_version '1.12.0' 'minecraft:geometry' @(
            (O description (O identifier "geometry.lepton.$name" texture_width $m.Width texture_height $m.Height `
                        visible_bounds_width $m.Bounds[0] visible_bounds_height $m.Bounds[1] visible_bounds_offset @(0, $m.Bounds[2], 0)) `
                bones @($bones))))
}

# ================================================================================================
# Items
# ================================================================================================
function Write-Item([string]$Id, [hashtable]$Name, [hashtable]$Extra) {
    $icon = Add-ItemTexture "textures/items/$Id"
    $comp = O 'minecraft:icon' $icon
    foreach ($k in ($Extra.Keys | Sort-Object)) { $comp[$k] = $Extra[$k] }
    Write-PackJson (BPath "items/$Id.json") (O format_version $script:ItemFormat 'minecraft:item' (O `
                description (O identifier "lepton:$Id" menu_category (O category 'items')) components $comp))
    Add-PackText "item.lepton:$Id.name" $Name
}
Write-Item 'diamond_heart' @{ en = 'Heart of Diamond'; zh = '钻石之心' } @{ 'minecraft:max_stack_size' = 64; 'minecraft:glint' = $true; 'minecraft:rarity' = 'uncommon' }
Write-Item 'forgotten_hat' @{ en = 'Forgotten Hat'; zh = '遗忘者之帽' } @{ 'minecraft:max_stack_size' = 1; 'minecraft:rarity' = 'uncommon'
    'minecraft:wearable' = (O slot 'slot.armor.head' protection 0) }
Write-Item 'soul_bead' @{ en = 'Soul Bead'; zh = '灵魂珠' } @{ 'minecraft:max_stack_size' = 64 }

# ================================================================================================
# Loot tables
# ================================================================================================
function Item([string]$Name, [int]$Weight = 1, $Functions = $null) {
    $e = O type 'item' name $Name weight $Weight
    if ($Functions) { $e.functions = @($Functions) }
    return $e
}
function Count([int]$Min, [int]$Max) { return (O function 'set_count' count (O min $Min max $Max)) }
function Looting([int]$Min, [int]$Max) { return (O function 'looting_enchant' count (O min $Min max $Max)) }
function Write-Loot([string]$Name, $Pools) { Write-PackJson (BPath "loot_tables/entities/$Name.json") (O pools @($Pools)) }

Write-Loot 'stoneling' @((O rolls 1 entries @((Item 'lepton:diamond_heart'))))
Write-Loot 'stoneling_carry' @((O rolls 1 entries @(
            (Item 'minecraft:diamond' 100), (Item 'minecraft:ender_pearl' 200),
            (Item 'minecraft:redstone' 300 (Count 10 15)), (Item 'minecraft:gold_ingot' 300 (Count 2 6)),
            (Item 'minecraft:iron_ingot' 300 (Count 4 7)), (Item 'minecraft:emerald' 200 (Count 1 3)),
            (Item 'minecraft:coal' 300 (Count 18 26)), (Item 'minecraft:zombie_head' 250), (Item 'minecraft:creeper_head' 250),
            (Item 'minecraft:lapis_lazuli' 200 (Count 4 8)), (Item 'minecraft:golden_apple' 50), (Item 'minecraft:saddle' 50),
            (Item 'minecraft:iron_horse_armor' 50), (Item 'minecraft:golden_horse_armor' 40), (Item 'minecraft:diamond_horse_armor' 30),
            (Item 'minecraft:name_tag' 100))))
Write-Loot 'forgotten' @(
    (O rolls 1 entries @((Item 'minecraft:arrow' 1 @((Count 4 8), (Looting 1 2))))),
    (O rolls 1 entries @((Item 'minecraft:bone' 1 @((Count 2 3), (Looting 0 1))))),
    (O rolls 1 conditions @((O condition 'killed_by_player')) entries @((Item 'lepton:forgotten_hat'))))
Write-Loot 'forgotten_gear' @((O rolls 1 entries @((Item 'minecraft:bow'))))
Write-Loot 'foxhound' @(
    (O rolls 1 entries @((Item 'minecraft:leather' 1 @((Count 0 3), (Looting 0 1))))),
    (O rolls 1 entries @((Item 'minecraft:coal' 1 @((Count 1 2), (Looting 0 2))))))
Write-Loot 'wraith' @((O rolls 1 entries @((Item 'lepton:soul_bead' 1 @((Count 1 1), (Looting 0 1))))))

# ================================================================================================
# Spawn rules (Quark's default weights / groups / biomes; the native side narrows them from config)
# ================================================================================================
function Write-SpawnRule([string]$Id, $Conditions) {
    Write-PackJson (BPath "spawn_rules/$Id.json") (O format_version '1.8.0' 'minecraft:spawn_rules' (O `
                description (O identifier "lepton:$Id" population_control 'monster') conditions @($Conditions)))
}
$monsterLight = O min 0 max 7 adjust_for_weather $true
$stoneBlocks = @('minecraft:stone', 'minecraft:deepslate', 'minecraft:tuff', 'minecraft:andesite', 'minecraft:diorite',
    'minecraft:granite', 'minecraft:calcite', 'minecraft:dripstone_block', 'minecraft:cobblestone', 'minecraft:cobbled_deepslate',
    'minecraft:smooth_basalt', 'minecraft:infested_stone', 'minecraft:infested_deepslate')
Write-SpawnRule 'stoneling' @((O 'minecraft:spawns_underground' (O) 'minecraft:spawns_on_surface' (O) `
            'minecraft:spawns_on_block_filter' $stoneBlocks 'minecraft:brightness_filter' $monsterLight `
            'minecraft:weight' (O default 80) 'minecraft:herd' (O min_size 1 max_size 1) `
            'minecraft:biome_filter' (O all_of @((O test 'has_biome_tag' value 'overworld'), (O test 'has_biome_tag' operator '!=' value 'the_end'), (O test 'has_biome_tag' operator '!=' value 'nether')))))
$foxBlocks = @('minecraft:netherrack', 'minecraft:soul_sand', 'minecraft:basalt', 'minecraft:soul_soil')
$notPeaceful = O min 'easy' max 'hard'
Write-SpawnRule 'foxhound' @(
    (O 'minecraft:spawns_on_surface' (O) 'minecraft:spawns_underground' (O) 'minecraft:spawns_on_block_filter' $foxBlocks `
            'minecraft:difficulty_filter' $notPeaceful 'minecraft:weight' (O default 30) 'minecraft:herd' (O min_size 1 max_size 2) `
            'minecraft:biome_filter' (O any_of @((O test 'has_biome_tag' value 'nether_wastes'), (O test 'has_biome_tag' value 'basalt_deltas')))),
    (O 'minecraft:spawns_on_surface' (O) 'minecraft:spawns_underground' (O) 'minecraft:spawns_on_block_filter' $foxBlocks `
            'minecraft:difficulty_filter' $notPeaceful 'minecraft:weight' (O default 2) 'minecraft:herd' (O min_size 1 max_size 1) `
            'minecraft:biome_filter' (O test 'has_biome_tag' value 'soulsand_valley')))
Write-SpawnRule 'wraith' @((O 'minecraft:spawns_on_surface' (O) 'minecraft:spawns_underground' (O) `
            'minecraft:spawns_on_block_filter' @('minecraft:soul_sand', 'minecraft:soul_soil') 'minecraft:brightness_filter' $monsterLight `
            'minecraft:difficulty_filter' $notPeaceful 'minecraft:weight' (O default 5) 'minecraft:herd' (O min_size 1 max_size 3) `
            'minecraft:biome_filter' (O test 'has_biome_tag' value 'soulsand_valley')))

# ================================================================================================
# Behaviour entities
# ================================================================================================
function Write-Entity([string]$Id, [hashtable]$Name, $Description, $Groups, $Components, $Events) {
    $desc = O identifier "lepton:$Id" is_spawnable $true is_summonable $true is_experimental $false
    if ($Description) { foreach ($k in $Description.Keys) { $desc[$k] = $Description[$k] } }
    Write-PackJson (BPath "entities/$Id.json") (O format_version $EntityFormat 'minecraft:entity' (O `
                description $desc component_groups $Groups components $Components events $Events))
    Add-PackText "entity.lepton:$Id.name" $Name
    if ($desc.is_spawnable) { Add-PackText "item.spawn_egg.entity.lepton:$Id.name" @{ en = "$($Name.en) Spawn Egg"; zh = "$($Name.zh)刷怪蛋" } }
}

# ---- Stoneling --------------------------------------------------------------------------------
$variantEvents = O
for ($i = 0; $i -le 10; $i++) { $variantEvents["lepton:variant_$i"] = Event-Set 'lepton:variant' $i }
$tempt = { param($items) O 'minecraft:behavior.tempt' (O priority 0 speed_multiplier 0.6 within_radius 10 can_tempt_vertically $true items @($items)) }
$stonelingGroups = O `
    'lepton:wild' (O 'minecraft:despawn' (O despawn_from_distance (O min_distance 32 max_distance 128))) `
    'lepton:tame' (O 'minecraft:persistent' (O)) `
    'lepton:carry_loot' (O 'minecraft:equipment' (O table 'loot_tables/entities/stoneling_carry.json' slot_drop_chance @(
            (O slot 'slot.weapon.mainhand' drop_chance 0.0)))) `
    'lepton:loot_heart' (O 'minecraft:loot' (O table 'loot_tables/entities/stoneling.json')) `
    'lepton:tempt_diamond' (& $tempt 'minecraft:diamond') `
    'lepton:tempt_glow_shroom' (& $tempt 'lepton:glow_shroom') `
    'lepton:wary' (O 'minecraft:behavior.random_stroll' (O priority 1 speed_multiplier 0.1 interval 120)) `
    'lepton:startled' (O 'minecraft:behavior.avoid_mob_type' (O priority 2 max_flee 16 ignore_visibility $false remove_target $true entity_types @(
            (O filters (O all_of @((Is-Family 'player'), (Not-Creative))) max_dist 4 walk_speed_multiplier 0.5 sprint_speed_multiplier 0.5))))
$stonelingComponents = O `
    'minecraft:type_family' (O family @('stoneling', 'mob')) `
    'minecraft:health' (O value 8 max 8) `
    'minecraft:knockback_resistance' (O value 1.0) `
    'minecraft:movement' (O value 0.7) `
    'minecraft:movement.basic' (O) `
    'minecraft:jump.static' (O) `
    'minecraft:variable_max_auto_step' (O base_value 1.0625 jump_prevented_value 1.0625) `
    'minecraft:navigation.walk' (O avoid_water $true avoid_damage_blocks $true can_path_over_water $false) `
    'minecraft:collision_box' (O width 0.5 height 0.9) `
    'minecraft:physics' (O) `
    'minecraft:pushable' (O is_pushable $true is_pushable_by_piston $true) `
    'minecraft:breathable' (O total_supply 15 suffocate_time 0 breathes_water $true breathes_air $true) `
    'minecraft:damage_sensor' (O triggers @((O cause 'fall' deals_damage 'no'), (O cause 'contact' deals_damage 'no'))) `
    'minecraft:follow_range' (O value 16) `
    'minecraft:nameable' (O) `
    'minecraft:conditional_bandwidth_optimization' (O) `
    'minecraft:behavior.move_to_block' (O priority 4 tick_interval 300 search_range 16 search_height 2 goal_radius 1.0 speed_multiplier 0.2 `
        stay_duration 120 start_chance 1.0 target_selection_method 'nearest' target_blocks @('minecraft:diamond_ore', 'minecraft:deepslate_diamond_ore')) `
    'minecraft:behavior.random_stroll' (O priority 5 speed_multiplier 0.2)
$variantRandom = @(for ($i = 0; $i -le 10; $i++) { O weight 1 set_property (O 'lepton:variant' $i) })
$stonelingEvents = O `
    'minecraft:entity_spawned' (O sequence @(
        (O add (O component_groups @('lepton:wild', 'lepton:carry_loot', 'lepton:loot_heart', 'lepton:tempt_diamond'))),
        (O randomize $variantRandom))) `
    'lepton:from_heart' (O add (O component_groups @('lepton:tame', 'lepton:loot_heart', 'lepton:tempt_diamond')) set_property (O 'lepton:player_made' $true)) `
    'lepton:make_player_made' (O remove (O component_groups @('lepton:wild', 'lepton:wary', 'lepton:startled')) add (O component_groups @('lepton:tame')) `
        set_property (O 'lepton:player_made' $true 'lepton:startled' $false)) `
    'lepton:startle' (O remove (O component_groups @('lepton:tempt_diamond', 'lepton:tempt_glow_shroom', 'lepton:wary')) add (O component_groups @('lepton:startled')) set_property (O 'lepton:startled' $true)) `
    'lepton:tempt_none' (O remove (O component_groups @('lepton:tempt_diamond', 'lepton:tempt_glow_shroom'))) `
    'lepton:tempt_diamond' (O remove (O component_groups @('lepton:tempt_glow_shroom')) add (O component_groups @('lepton:tempt_diamond'))) `
    'lepton:tempt_glow_shroom' (O remove (O component_groups @('lepton:tempt_diamond')) add (O component_groups @('lepton:tempt_glow_shroom'))) `
    'lepton:wary_on' (O add (O component_groups @('lepton:wary'))) `
    'lepton:wary_off' (O remove (O component_groups @('lepton:wary'))) `
    'lepton:loot_on' (O add (O component_groups @('lepton:loot_heart'))) `
    'lepton:loot_off' (O remove (O component_groups @('lepton:loot_heart'))) `
    'lepton:lichen' (Event-Set 'lepton:lichen' $true)
foreach ($k in $variantEvents.Keys) { $stonelingEvents[$k] = $variantEvents[$k] }
Write-Entity 'stoneling' @{ en = 'Stoneling'; zh = '石精灵' } (O properties (O `
            'lepton:variant' (O type 'int' range @(0, 10) default 0 client_sync $true) `
            'lepton:lichen' (O type 'bool' default $false client_sync $true) `
            'lepton:player_made' (O type 'bool' default $false) `
            'lepton:startled' (O type 'bool' default $false))) $stonelingGroups $stonelingComponents $stonelingEvents

# ---- Forgotten --------------------------------------------------------------------------------
$attackTargets = @(
    (O filters (O all_of @((Is-Family 'player'), (Not-Creative))) max_dist 16),
    (O filters (Is-Family 'irongolem') max_dist 16))
Write-Entity 'forgotten' @{ en = 'Forgotten'; zh = '遗忘者' } (O properties (O 'lepton:init' (O type 'bool' default $false))) (O `
        'lepton:ranged' (O 'minecraft:behavior.ranged_attack' (O priority 4 attack_interval_min 1.0 attack_interval_max 2.0 attack_radius 15.0)) `
        'lepton:melee' (O 'minecraft:behavior.melee_attack' (O priority 4 speed_multiplier 1.2 track_target $true))) (O `
        'minecraft:type_family' (O family @('forgotten', 'skeleton', 'undead', 'monster', 'mob')) `
        'minecraft:health' (O value 60 max 60) `
        'minecraft:movement' (O value 0.3) `
        'minecraft:knockback_resistance' (O value 1.0) `
        'minecraft:attack' (O damage 2) `
        'minecraft:collision_box' (O width 0.7 height 2.4) `
        'minecraft:movement.basic' (O) `
        'minecraft:jump.static' (O) `
        'minecraft:can_climb' (O) `
        'minecraft:navigation.walk' (O is_amphibious $true avoid_sun $false avoid_water $true) `
        'minecraft:physics' (O) `
        'minecraft:pushable' (O is_pushable $true is_pushable_by_piston $true) `
        'minecraft:breathable' (O total_supply 15 suffocate_time 0 breathes_water $true) `
        'minecraft:follow_range' (O value 16 max 16) `
        'minecraft:nameable' (O) `
        'minecraft:despawn' (O despawn_from_distance (O)) `
        'minecraft:equipment' (O table 'loot_tables/entities/forgotten_gear.json' slot_drop_chance @(
            (O slot 'slot.weapon.mainhand' drop_chance 0.0), (O slot 'slot.weapon.offhand' drop_chance 0.0), (O slot 'slot.armor.head' drop_chance 0.0))) `
        'minecraft:shooter' (O def 'minecraft:arrow') `
        'minecraft:loot' (O table 'loot_tables/entities/forgotten.json') `
        'minecraft:experience_reward' (O on_death 'query.last_hit_by_player ? 5 + (query.equipment_count * math.random(1,3)) : 0') `
        'minecraft:behavior.hurt_by_target' (O priority 1) `
        'minecraft:behavior.nearest_attackable_target' (O priority 2 must_see $true reselect_targets $true entity_types $attackTargets) `
        'minecraft:behavior.avoid_mob_type' (O priority 3 entity_types @((O filters (Is-Family 'wolf') max_dist 6 walk_speed_multiplier 1.0 sprint_speed_multiplier 1.2))) `
        'minecraft:behavior.random_stroll' (O priority 5 speed_multiplier 1.0) `
        'minecraft:behavior.look_at_player' (O priority 6 look_distance 8) `
        'minecraft:behavior.random_look_around' (O priority 7)) (O `
        'minecraft:entity_spawned' (O add (O component_groups @('lepton:ranged'))) `
        'lepton:to_melee' (O remove (O component_groups @('lepton:ranged')) add (O component_groups @('lepton:melee'))) `
        'lepton:to_ranged' (O remove (O component_groups @('lepton:melee')) add (O component_groups @('lepton:ranged'))) `
        'lepton:init' (Event-Set 'lepton:init' $true))

# ---- Foxhound ---------------------------------------------------------------------------------
$healItems = @($Meats | ForEach-Object { O item $_ heal_amount $Heal[$_] })
$restBlocks = @('minecraft:lit_furnace', 'minecraft:lit_blast_furnace', 'minecraft:lit_smoker', 'minecraft:furnace', 'minecraft:blast_furnace',
    'minecraft:smoker', 'minecraft:magma', 'minecraft:glowstone', 'minecraft:sea_lantern', 'minecraft:shroomlight',
    'minecraft:lit_redstone_lamp', 'minecraft:ochre_froglight', 'minecraft:verdant_froglight', 'minecraft:pearlescent_froglight',
    'minecraft:jack_o_lantern', 'minecraft:crying_obsidian', 'minecraft:beacon', 'minecraft:campfire', 'minecraft:soul_campfire')
$foxGroups = O `
    'lepton:wild' (O `
        'minecraft:type_family' (O family @('foxhound', 'monster', 'mob')) `
        'minecraft:health' (O value 8 max 8) `
        'minecraft:despawn' (O despawn_from_distance (O)) `
        'minecraft:behavior.nearest_attackable_target' (O priority 5 must_see $false reselect_targets $true entity_types @(
            (O filters (O all_of @((Is-Family 'player'), (Not-Creative), (O test 'is_difficulty' value 'peaceful' operator '!='))) max_dist 16),
            (O filters (O any_of @((Is-Family 'sheep'), (Is-Family 'rabbit'))) max_dist 16)))) `
    'lepton:tame' (O `
        'minecraft:type_family' (O family @('foxhound', 'mob')) `
        'minecraft:is_tamed' (O) `
        'minecraft:persistent' (O) `
        'minecraft:health' (O value 30 max 30) `
        'minecraft:sittable' (O) `
        'minecraft:healable' (O items $healItems) `
        'minecraft:breedable' (O require_tame $true inherit_tamed $true mutation_factor (O variant 0.0) breeds_with (O mate_type 'lepton:foxhound' baby_type 'lepton:foxhound' `
                breed_event (O event 'minecraft:entity_born' target 'baby')) breed_items @($Meats)) `
        'minecraft:behavior.breed' (O priority 5 speed_multiplier 1.0) `
        'minecraft:behavior.follow_owner' (O priority 9 speed_multiplier 1.0 start_distance 10 stop_distance 2) `
        'minecraft:behavior.owner_hurt_by_target' (O priority 1) `
        'minecraft:behavior.owner_hurt_target' (O priority 3) `
        'minecraft:behavior.move_to_block' (O priority 6 tick_interval 40 search_range 8 search_height 2 goal_radius 0.85 speed_multiplier 0.8 `
            stay_duration 60 start_chance 1.0 target_selection_method 'nearest' target_offset @(0, 1, 0) target_blocks $restBlocks `
            on_reach @((O event 'lepton:rest' target 'self')) on_stay_completed @((O event 'lepton:wake' target 'self')))) `
    'lepton:baby' (O 'minecraft:is_baby' (O) 'minecraft:scale' (O value 0.5) 'minecraft:ageable' (O duration 1200 feed_items @($Meats) grow_up (O event 'minecraft:ageable_grow_up' target 'self'))) `
    'lepton:red' (O 'minecraft:variant' (O value 0)) `
    'lepton:blue' (O 'minecraft:variant' (O value 1)) `
    'lepton:black' (O 'minecraft:variant' (O value 2))
$foxComponents = O `
    'minecraft:type_family' (O family @('foxhound', 'mob')) `
    'minecraft:health' (O value 8 max 8) `
    'minecraft:movement' (O value 0.3) `
    'minecraft:attack' (O damage 4) `
    'minecraft:fire_immune' (O) `
    'minecraft:collision_box' (O width 0.8 height 0.8) `
    'minecraft:movement.basic' (O) `
    'minecraft:jump.static' (O) `
    'minecraft:can_climb' (O) `
    'minecraft:navigation.walk' (O avoid_water $true avoid_damage_blocks $false can_path_over_lava $true can_walk_in_lava $true) `
    'minecraft:physics' (O) `
    'minecraft:pushable' (O is_pushable $true is_pushable_by_piston $true) `
    'minecraft:breathable' (O total_supply 15 suffocate_time 0) `
    'minecraft:nameable' (O) `
    'minecraft:leashable' (O soft_distance 4.0 hard_distance 6.0 max_distance 10.0) `
    'minecraft:follow_range' (O value 16) `
    'minecraft:loot' (O table 'loot_tables/entities/foxhound.json') `
    'minecraft:experience_reward' (O on_bred 'math.random(1,7)' on_death 'query.last_hit_by_player ? math.random(1,3) : 0') `
    'minecraft:behavior.float' (O priority 0) `
    'minecraft:behavior.leap_at_target' (O priority 3 yd 0.4 must_be_on_ground $true) `
    'minecraft:behavior.melee_attack' (O priority 4 speed_multiplier 1.0 track_target $true) `
    'minecraft:behavior.hurt_by_target' (O priority 2) `
    'minecraft:behavior.random_stroll' (O priority 11 speed_multiplier 1.0) `
    'minecraft:behavior.look_at_player' (O priority 13 look_distance 8) `
    'minecraft:behavior.random_look_around' (O priority 14)
$foxEvents = O `
    'minecraft:entity_spawned' (O sequence @(
        (O add (O component_groups @('lepton:wild'))),
        (O randomize @(
            (O weight 255 sequence @(
                (O filters (O test 'has_biome_tag' value 'soulsand_valley') add (O component_groups @('lepton:blue'))),
                (O filters (O test 'has_biome_tag' value 'soulsand_valley' operator '!=') add (O component_groups @('lepton:red'))))),
            (O weight 1 add (O component_groups @('lepton:black'))))))) `
    'minecraft:entity_born' (O add (O component_groups @('lepton:tame', 'lepton:baby'))) `
    'minecraft:ageable_grow_up' (O remove (O component_groups @('lepton:baby'))) `
    'lepton:on_tame' (O remove (O component_groups @('lepton:wild')) add (O component_groups @('lepton:tame'))) `
    'lepton:set_blue' (O remove (O component_groups @('lepton:red', 'lepton:black')) add (O component_groups @('lepton:blue'))) `
    'lepton:set_black' (O remove (O component_groups @('lepton:red', 'lepton:blue')) add (O component_groups @('lepton:black'))) `
    'lepton:rest' (Event-Set 'lepton:resting' $true) `
    'lepton:wake' (Event-Set 'lepton:resting' $false) `
    'lepton:tater_on' (Event-Set 'lepton:tatering' $true) `
    'lepton:tater_off' (Event-Set 'lepton:tatering' $false)
Write-Entity 'foxhound' @{ en = 'Foxhound'; zh = '猎狐犬' } (O properties (O `
            'lepton:resting' (O type 'bool' default $false client_sync $true) `
            'lepton:tatering' (O type 'bool' default $false client_sync $true))) $foxGroups $foxComponents $foxEvents

# ---- Wraith -----------------------------------------------------------------------------------
# The voice (an index into the wraithSounds config list, -1 = not chosen yet) lives on the entity.
$WraithVoiceMax = 63
$wraithEvents = O 'lepton:aggro' (O add (O component_groups @('lepton:aggro')))
for ($i = 0; $i -le $WraithVoiceMax; $i++) { $wraithEvents["lepton:voice_$i"] = Event-Set 'lepton:voice' $i }
Write-Entity 'wraith' @{ en = 'Wraith'; zh = '游魂' } (O properties (O 'lepton:voice' (O type 'int' range @(-1, $WraithVoiceMax) default -1))) (O `
        'lepton:aggro' (O `
            'minecraft:behavior.hurt_by_target' (O priority 1) `
            'minecraft:behavior.nearest_attackable_target' (O priority 2 must_see $true reselect_targets $true within_radius 35 entity_types @(
                (O filters (O all_of @((Is-Family 'player'), (Not-Creative))) max_dist 35),
                (O filters (O any_of @((Is-Family 'villager'), (Is-Family 'wandering_trader'))) max_dist 35),
                (O filters (Is-Family 'irongolem') max_dist 35))))) (O `
        'minecraft:type_family' (O family @('wraith', 'undead', 'monster', 'mob')) `
        'minecraft:health' (O value 20 max 20) `
        'minecraft:movement' (O value 0.28) `
        'minecraft:attack' (O damage 3) `
        'minecraft:knockback_resistance' (O value 1.0) `
        'minecraft:follow_range' (O value 35 max 35) `
        'minecraft:fire_immune' (O) `
        'minecraft:collision_box' (O width 0.6 height 1.95) `
        'minecraft:movement.basic' (O) `
        'minecraft:jump.static' (O) `
        'minecraft:can_climb' (O) `
        'minecraft:navigation.walk' (O is_amphibious $true avoid_water $true can_path_over_lava $false) `
        'minecraft:physics' (O) `
        'minecraft:pushable' (O is_pushable $true is_pushable_by_piston $true) `
        'minecraft:breathable' (O total_supply 15 suffocate_time 0 breathes_water $true) `
        'minecraft:damage_sensor' (O triggers @((O cause 'fall' deals_damage 'no'))) `
        'minecraft:nameable' (O) `
        'minecraft:despawn' (O despawn_from_distance (O)) `
        'minecraft:loot' (O table 'loot_tables/entities/wraith.json') `
        'minecraft:experience_reward' (O on_death 'query.last_hit_by_player ? 5 : 0') `
        'minecraft:behavior.melee_attack' (O priority 3 speed_multiplier 1.0 track_target $false) `
        'minecraft:behavior.random_stroll' (O priority 7 speed_multiplier 1.0) `
        'minecraft:behavior.look_at_player' (O priority 8 look_distance 8) `
        'minecraft:behavior.random_look_around' (O priority 9)) $wraithEvents

# ---- Soul bead --------------------------------------------------------------------------------
Write-Entity 'soul_bead' @{ en = 'Soul Bead'; zh = '灵魂珠' } (O is_spawnable $false) (O) (O `
        'minecraft:type_family' (O family @('soul_bead', 'inanimate')) `
        'minecraft:collision_box' (O width 0.01 height 0.01) `
        'minecraft:physics' (O has_gravity $false has_collision $false) `
        'minecraft:pushable' (O is_pushable $false is_pushable_by_piston $false) `
        'minecraft:fire_immune' (O) `
        'minecraft:damage_sensor' (O triggers (O cause 'all' deals_damage 'no')) `
        'minecraft:health' (O value 1 max 1)) (O)

# ================================================================================================
# Resource pack: client entities, animations, render controllers, attachable, particles, sounds
# ================================================================================================
function Write-ClientEntity([string]$Id, $Desc) {
    $d = O identifier "lepton:$Id"
    foreach ($k in $Desc.Keys) { $d[$k] = $Desc[$k] }
    Write-PackJson (RPath "entity/$Id.entity.json") (O format_version '1.10.0' 'minecraft:client_entity' (O description $d))
}
function Write-Animations([string]$File, $Anims) {
    Write-PackJson (RPath "animations/$File.animation.json") (O format_version '1.8.0' animations $Anims)
}
function Write-RenderControllers([string]$File, $Ctrls) {
    Write-PackJson (RPath "render_controllers/$File.render_controllers.json") (O format_version '1.8.0' render_controllers $Ctrls)
}
function Particle-Loop([string]$Effect, [double]$Length = 0.05) { return (O loop $true animation_length $Length particle_effects (O '0.0' @((O effect $Effect)))) }
$legSwing = 'math.cos(query.modified_distance_moved * 38.17) * 40.0 * query.modified_move_speed'

# ---- Stoneling --------------------------------------------------------------------------------
$stTextures = O
$i = 0
foreach ($v in $script:StonelingVariants.Keys) { $stTextures["v$i"] = "$Tex/stoneling_$v"; $i++ }
$stTextures.lichen = "$Tex/stoneling_lichen"
Write-ClientEntity 'stoneling' (O `
        materials (O default 'entity_alphatest') `
        textures $stTextures `
        geometry (O default 'geometry.lepton.stoneling' lichen 'geometry.lepton.stoneling_lichen') `
        scripts (O initialize @('variable.hold_angle = math.random(-45, 45);') animate @('walk', 'hold')) `
        animations (O walk 'animation.lepton.stoneling.walk' hold 'animation.lepton.stoneling.hold') `
        render_controllers @('controller.render.lepton.stoneling', (O 'controller.render.lepton.stoneling_lichen' "query.property('lepton:lichen')")) `
        enable_attachables $true `
        spawn_egg (O base_color '#a1a1a1' overlay_color '#505050'))
Write-Animations 'lepton_stoneling' (O `
        'animation.lepton.stoneling.walk' (O loop $true bones (O `
            leg0 (O rotation @($legSwing, 0, 0)) leg1 (O rotation @("-($legSwing)", 0, 0)) `
            arm0 (O rotation @("-($legSwing) * 0.6", 0, 0)) arm1 (O rotation @("($legSwing) * 0.6", 0, 0)) `
            head (O rotation @('math.sin(query.life_time * 120) * 2', 0, 'math.cos(query.modified_distance_moved * 19.0) * 4 * query.modified_move_speed')))) `
        'animation.lepton.stoneling.hold' (O loop $true bones (O rightItem (O rotation @(0, 0, 'variable.hold_angle')))))
$stArray = @(for ($j = 0; $j -lt $i; $j++) { "Texture.v$j" })
Write-RenderControllers 'lepton_stoneling' (O `
        'controller.render.lepton.stoneling' (O arrays (O textures (O 'Array.variants' $stArray)) geometry 'Geometry.default' `
            materials @((O '*' 'Material.default')) textures @("Array.variants[query.property('lepton:variant')]")) `
        'controller.render.lepton.stoneling_lichen' (O geometry 'Geometry.lichen' materials @((O '*' 'Material.default')) textures @('Texture.lichen')))

# ---- Forgotten --------------------------------------------------------------------------------
Write-ClientEntity 'forgotten' (O `
        materials (O default 'entity_alphatest') `
        textures (O default "$Tex/forgotten") `
        geometry (O default 'geometry.lepton.forgotten') `
        scripts (O scale '1.2' animate @('walk', 'aim', 'spell')) `
        animations (O walk 'animation.lepton.forgotten.walk' aim 'animation.lepton.forgotten.aim' spell 'animation.lepton.forgotten.spell') `
        particle_effects (O spell 'lepton:forgotten_spell') `
        render_controllers @('controller.render.lepton.forgotten') `
        enable_attachables $true `
        spawn_egg (O base_color '#969487' overlay_color '#3a3330'))
Write-Animations 'lepton_forgotten' (O `
        'animation.lepton.forgotten.walk' (O loop $true bones (O `
            rightLeg (O rotation @($legSwing, 0, 0)) leftLeg (O rotation @("-($legSwing)", 0, 0)) `
            rightArm (O rotation @("query.has_target ? 0 : -($legSwing) * 0.8", 0, 0)) leftArm (O rotation @("query.has_target ? 0 : ($legSwing) * 0.8", 0, 0)))) `
        'animation.lepton.forgotten.aim' (O loop $true bones (O `
            rightArm (O rotation @("query.has_target ? -90.0 + math.sin(query.life_time * 60) * 3 : 0", "query.has_target ? -6 : 0", 0)) `
            leftArm (O rotation @("query.has_target ? -90.0 : 0", "query.has_target ? 30 : 0", 0)))) `
        'animation.lepton.forgotten.spell' (Particle-Loop 'spell' 0.1))
Write-RenderControllers 'lepton_forgotten' (O 'controller.render.lepton.forgotten' (O geometry 'Geometry.default' `
            materials @((O '*' 'Material.default')) textures @('Texture.default')))

# ---- Foxhound ---------------------------------------------------------------------------------
$foxTextures = O
$foxArray = @()
foreach ($coat in $script:FoxhoundCoats.Keys) {
    foreach ($mood in 'idle', 'hostile', 'sleeping') { $foxTextures["${coat}_$mood"] = "$Tex/foxhound_${coat}_$mood"; $foxArray += "Texture.${coat}_$mood" }
}
$resting = "query.property('lepton:resting')"
Write-ClientEntity 'foxhound' (O `
        materials (O default 'entity_alphatest') `
        textures $foxTextures `
        geometry (O default 'geometry.lepton.foxhound') `
        scripts (O animate @('walk', (O sit 'query.is_sitting'), (O rest $resting), 'tail', 'fx')) `
        animations (O walk 'animation.lepton.foxhound.walk' sit 'animation.lepton.foxhound.sit' rest 'animation.lepton.foxhound.rest' `
            tail 'animation.lepton.foxhound.tail' fx 'controller.animation.lepton.foxhound.fx' `
            flame 'animation.lepton.foxhound.flame' soul 'animation.lepton.foxhound.soul' smoke 'animation.lepton.foxhound.smoke' `
            tater 'animation.lepton.foxhound.tater') `
        particle_effects (O flame 'lepton:foxhound_flame' soul 'lepton:foxhound_soul_flame' smoke 'lepton:foxhound_smoke' tater 'lepton:foxhound_large_smoke') `
        render_controllers @('controller.render.lepton.foxhound') `
        spawn_egg (O base_color '#890d0d' overlay_color '#f2af4b'))
Write-Animations 'lepton_foxhound' (O `
        'animation.lepton.foxhound.walk' (O loop $true bones (O `
            leg0 (O rotation @($legSwing, 0, 0)) leg1 (O rotation @("-($legSwing)", 0, 0)) `
            leg2 (O rotation @("-($legSwing)", 0, 0)) leg3 (O rotation @($legSwing, 0, 0)))) `
        'animation.lepton.foxhound.sit' (O loop $true bones (O `
            body (O rotation @(-25, 0, 0) position @(0, -2, 0)) `
            head (O rotation @(25, 0, 0)) `
            leg2 (O rotation @(-90, 0, 0) position @(0, -4, 2)) leg3 (O rotation @(-90, 0, 0) position @(0, -4, 2)) `
            tail (O rotation @(30, 0, 0)))) `
        'animation.lepton.foxhound.rest' (O loop $true bones (O `
            root (O rotation @(0, 0, 90) position @(4, 0, 0)))) `
        'animation.lepton.foxhound.tail' (O loop $true bones (O tail (O rotation @(0, 'math.sin(query.life_time * 400) * (query.is_tamed ? 20 : 6)', 0)))) `
        'animation.lepton.foxhound.flame' (Particle-Loop 'flame' 0.1) `
        'animation.lepton.foxhound.soul' (Particle-Loop 'soul' 0.1) `
        'animation.lepton.foxhound.smoke' (Particle-Loop 'smoke' 0.1) `
        'animation.lepton.foxhound.tater' (Particle-Loop 'tater' 0.5))
Write-PackJson (RPath 'animation_controllers/lepton_foxhound.animation_controllers.json') (O format_version '1.10.0' animation_controllers (O `
            'controller.animation.lepton.foxhound.fx' (O initial_state 'default' states (O `
                default (O animations @((O flame "!$resting && query.variant != 1"), (O soul "!$resting && query.variant == 1"), (O smoke $resting),
                        (O tater "query.property('lepton:tatering')")))))))
Write-RenderControllers 'lepton_foxhound' (O 'controller.render.lepton.foxhound' (O arrays (O textures (O 'Array.coats' $foxArray)) `
            geometry 'Geometry.default' materials @((O '*' 'Material.default')) `
            textures @("Array.coats[math.clamp(query.variant, 0, 2) * 3 + ($resting ? 2 : ((!query.is_tamed || query.has_target) ? 1 : 0))]")))

# ---- Wraith -----------------------------------------------------------------------------------
Write-ClientEntity 'wraith' (O `
        materials (O default 'entity_alphatest') `
        textures (O default "$Tex/wraith") `
        geometry (O default 'geometry.lepton.wraith') `
        scripts (O animate @('float', 'fx')) `
        animations (O float 'animation.lepton.wraith.float' fx 'animation.lepton.wraith.fx') `
        particle_effects (O mist 'lepton:wraith_mist' soul 'lepton:wraith_soul') `
        render_controllers @('controller.render.lepton.wraith') `
        spawn_egg (O base_color '#ececec' overlay_color '#bdbdbd'))
Write-Animations 'lepton_wraith' (O `
        'animation.lepton.wraith.float' (O loop $true bones (O `
            body (O position @(0, 'math.sin(query.life_time * 90) * 1.0 + 1.0', 0)) `
            rightArm (O rotation @('math.sin(query.life_time * 70) * 6', 0, 0)) leftArm (O rotation @('math.cos(query.life_time * 70) * 6', 0, 0)))) `
        'animation.lepton.wraith.fx' (O loop $true animation_length 0.1 particle_effects (O '0.0' @((O effect 'mist'), (O effect 'soul')))))
Write-RenderControllers 'lepton_wraith' (O 'controller.render.lepton.wraith' (O geometry 'Geometry.default' `
            materials @((O '*' 'Material.default')) textures @('Texture.default')))

# ---- Soul bead --------------------------------------------------------------------------------
Write-ClientEntity 'soul_bead' (O `
        materials (O default 'entity_alphatest') `
        textures (O default 'textures/particle/lepton_dust') `
        geometry (O default 'geometry.lepton.empty') `
        scripts (O animate @('trail')) `
        animations (O trail 'animation.lepton.soul_bead.trail') `
        particle_effects (O dust 'lepton:soul_bead_dust' fall 'lepton:soul_bead_fall') `
        render_controllers @('controller.render.default'))
Write-Animations 'lepton_soul_bead' (O 'animation.lepton.soul_bead.trail' (O loop $true animation_length 0.05 particle_effects (O '0.0' @((O effect 'dust'), (O effect 'fall')))))

# ---- Forgotten Hat (worn) -----------------------------------------------------------------------
Write-PackJson (RPath 'attachables/forgotten_hat.json') (O format_version '1.10.0' 'minecraft:attachable' (O description (O `
                identifier 'lepton:forgotten_hat' `
                materials (O default 'armor' enchanted 'armor_enchanted') `
                textures (O default "$Tex/forgotten_hat_worn" enchanted 'textures/misc/enchanted_item_glint') `
                geometry (O default 'geometry.lepton.forgotten_hat') `
                scripts (O parent_setup 'variable.helmet_layer_visible = 0.0;') `
                render_controllers @('controller.render.armor'))))

# ---- Particles --------------------------------------------------------------------------------
function Write-Particle([string]$Id, [string]$Texture, $Emitter, [string]$Lifetime, $Size, $Motion = $null, $Tint = $null, [double]$Speed = 0.0, [switch]$Lit) {
    $comp = O
    foreach ($k in $Emitter.Keys) { $comp[$k] = $Emitter[$k] }
    $comp['minecraft:particle_lifetime_expression'] = O max_lifetime $Lifetime
    $comp['minecraft:particle_initial_speed'] = $Speed
    $comp['minecraft:particle_motion_dynamic'] = if ($Motion) { $Motion } else { O }
    $comp['minecraft:particle_appearance_billboard'] = O size @($Size) facing_camera_mode 'lookat_xyz' uv (O texture_width 8 texture_height 8 uv @(0, 0) uv_size @(8, 8))
    if ($Tint) { $comp['minecraft:particle_appearance_tinting'] = O color @($Tint) }
    if ($Lit) { $comp['minecraft:particle_appearance_lighting'] = O }
    Write-PackJson (RPath "particles/$($Id -replace ':', '_').particle.json") (O format_version '1.10.0' particle_effect (O `
                description (O identifier $Id basic_render_parameters (O material 'particles_alpha' texture $Texture)) components $comp))
}
function Burst($N, [double[]]$Half, [double[]]$Offset) {
    return (O 'minecraft:emitter_rate_instant' (O num_particles $N) 'minecraft:emitter_lifetime_once' (O active_time 0.05) `
            'minecraft:emitter_shape_box' (O half_dimensions @($Half) offset @($Offset) direction 'outwards'))
}
$fade = @(0.08, 0.08)
Write-Particle 'lepton:forgotten_spell' 'textures/particle/lepton_spell' (Burst 1 @(0.7, 1.2, 0.7) @(0, 1.2, 0)) '0.6 + math.random(0, 0.4)' @(0.1, 0.1) (O linear_acceleration @(0, 0.4, 0)) @(1, 1, 1, 0.85)
Write-Particle 'lepton:foxhound_flame' 'textures/particle/lepton_dust' (Burst 1 @(0.4, 0.35, 0.4) @(0, 0.4, 0)) '0.4 + math.random(0, 0.3)' $fade (O linear_acceleration @(0, 0.6, 0)) @(1.0, 0.62, 0.18, 1)
Write-Particle 'lepton:foxhound_soul_flame' 'textures/particle/lepton_dust' (Burst 1 @(0.4, 0.35, 0.4) @(0, 0.4, 0)) '0.4 + math.random(0, 0.3)' $fade (O linear_acceleration @(0, 0.6, 0)) @(0.35, 0.9, 1.0, 1)
Write-Particle 'lepton:foxhound_smoke' 'textures/particle/lepton_mist' (Burst 1 @(0.4, 0.2, 0.4) @(0, 0.3, 0)) '0.8 + math.random(0, 0.4)' @(0.1, 0.1) (O linear_acceleration @(0, 0.3, 0)) @(0.35, 0.35, 0.35, 0.8)
Write-Particle 'lepton:foxhound_large_smoke' 'textures/particle/lepton_mist' (Burst 1 @(0.4, 0.3, 0.4) @(0, 0.4, 0)) '1.2 + math.random(0, 0.6)' @(0.25, 0.25) (O linear_acceleration @(0, 0.5, 0)) @(0.2, 0.2, 0.2, 0.9)
Write-Particle 'lepton:wraith_mist' 'textures/particle/lepton_mist' (Burst 1 @(0.5, 1.1, 0.5) @(0, 1.0, 0)) '0.8 + math.random(0, 0.5)' @(0.08, 0.08) (O linear_acceleration @(0, -0.05, 0)) @(0.75, 0.82, 0.8, 0.9)
Write-Particle 'lepton:wraith_soul' 'textures/particle/lepton_spell' (O 'minecraft:emitter_rate_instant' (O num_particles 'math.random(0, 1) < 0.2 ? 1 : 0') `
        'minecraft:emitter_lifetime_once' (O active_time 0.05) 'minecraft:emitter_shape_box' (O half_dimensions @(0.5, 0, 0.5) offset @(0, 0.1, 0) direction 'outwards')) `
    '1.5 + math.random(0, 0.5)' @(0.12, 0.12) (O linear_acceleration @(0, 0.25, 0)) @(0.45, 0.95, 1.0, 0.9)
Write-Particle 'lepton:soul_bead_dust' 'textures/particle/lepton_dust' (Burst 20 @(0.2, 0.2, 0.2) @(0, 0, 0)) '0.6 + math.random(0, 0.4)' @(0.06, 0.06) $null @(0.2, 0.12, 0.1, 1)
Write-Particle 'lepton:soul_bead_fall' 'textures/particle/lepton_dust' (O 'minecraft:emitter_rate_instant' (O num_particles 'math.random(0, 1) < 0.05 ? 1 : 0') `
        'minecraft:emitter_lifetime_once' (O active_time 0.05) 'minecraft:emitter_shape_box' (O half_dimensions @(0.2, 0.2, 0.2) offset @(0, 0, 0) direction 'outwards')) `
    '2.0' @(0.05, 0.05) (O linear_acceleration @(0, -0.4, 0)) @(0.32, 0.24, 0.18, 1)
Write-Particle 'lepton:stoneling_poof' 'textures/particle/lepton_mist' (O 'minecraft:emitter_rate_instant' (O num_particles 60) `
        'minecraft:emitter_lifetime_once' (O active_time 0.05) 'minecraft:emitter_shape_box' (O half_dimensions @(0.5, 0.5, 0.5) offset @(0, 0.5, 0) direction 'outwards')) `
    '0.8 + math.random(0, 0.6)' @(0.2, 0.2) (O linear_drag_coefficient 2.0 linear_acceleration @(0, 0.2, 0)) @(0.95, 0.95, 0.95, 1) 3.0

# ---- Sounds (vanilla Bedrock sound files; Quark's own sounds are not copied, see the spec) -------
function Snd([string[]]$Files, [double]$Volume = 1.0, [double]$Pitch = 1.0) {
    return (O category 'neutral' sounds @($Files | ForEach-Object { O name $_ volume $Volume pitch $Pitch }))
}
Write-PackJson (RPath 'sounds.d/mobsdeep.json') (O `
        'lepton.stoneling.meep' (Snd @('sounds/mob/bat/idle1', 'sounds/mob/bat/idle2') 0.8 1.6) `
        'lepton.stoneling.purr' (Snd @('sounds/mob/cat/purr1', 'sounds/mob/cat/purr2', 'sounds/mob/cat/purr3') 0.25 1.4) `
        'lepton.stoneling.give' (Snd @('sounds/block/itemframe/add_item1', 'sounds/block/itemframe/add_item2', 'sounds/block/itemframe/add_item3', 'sounds/block/itemframe/add_item4')) `
        'lepton.stoneling.take' (Snd @('sounds/block/itemframe/remove_item1', 'sounds/block/itemframe/remove_item2', 'sounds/block/itemframe/remove_item3', 'sounds/block/itemframe/remove_item4')) `
        'lepton.stoneling.eat' (Snd @('sounds/dig/stone1', 'sounds/dig/stone2', 'sounds/dig/stone3', 'sounds/dig/stone4') 1.0 0.8) `
        'lepton.stoneling.cry' (Snd @('sounds/mob/silverfish/hit1', 'sounds/mob/silverfish/hit2', 'sounds/mob/silverfish/hit3') 1.0 1.3) `
        'lepton.stoneling.die' (Snd @('sounds/random/break') 1.0 0.8) `
        'lepton.stoneling.michael' (Snd @('sounds/mob/villager/idle1', 'sounds/mob/villager/idle2', 'sounds/mob/villager/idle3') 1.0 0.9) `
        'lepton.foxhound.eat' (Snd @('sounds/mob/fox/eat1', 'sounds/mob/fox/eat2', 'sounds/mob/fox/eat3') 0.65) `
        'lepton.foxhound.crackle' (Snd @('sounds/block/furnace/fire_crackle1', 'sounds/block/furnace/fire_crackle2', 'sounds/block/furnace/fire_crackle3')) `
        'lepton.soul_bead.idle' (Snd @('sounds/mob/ghast/moan1', 'sounds/mob/ghast/moan2', 'sounds/mob/ghast/moan3', 'sounds/mob/ghast/moan4', 'sounds/mob/ghast/moan5', 'sounds/mob/ghast/moan6', 'sounds/mob/ghast/moan7')) `
        'lepton.soul_bead.spawn' (Snd @('sounds/mob/ghast/scream1', 'sounds/mob/ghast/scream2', 'sounds/mob/ghast/scream3', 'sounds/mob/ghast/scream4', 'sounds/mob/ghast/scream5') 0.6 1.2))

# ---- Textures -----------------------------------------------------------------------------------
Invoke-MobsdeepTextures { param($p) Add-PackFile $p }

Complete-PackUnit
