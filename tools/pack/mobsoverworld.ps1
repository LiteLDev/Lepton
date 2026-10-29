# U20 mobsoverworld: items, potion projectiles, loot, recipes, default spawn rules, texts and the
# item texture fragment. The hand-written entity files (bp/entities/{crab,shiba,toretoise}.json,
# rp/entity/*, animations, render controllers) live next to the output; models and textures come
# from tools/textures/mobsoverworld.ps1.
#
#   pwsh -NoProfile -File tools/pack/mobsoverworld.ps1     # regenerates the committed output

$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$BP = Join-Path $root 'assets/pack/bp'
$RP = Join-Path $root 'assets/pack/rp'

function Write-Json([string]$Path, $Object) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $text = ($Object | ConvertTo-Json -Depth 64) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($Path, $text + "`n")
}

$texts = [ordered]@{}   # key -> @{ en; zh }
$itemTextures = [ordered]@{}
function Add-Text([string]$Key, [string]$En, [string]$Zh) { $texts[$Key] = @{ en = $En; zh = $Zh } }

function Write-Item([string]$Path, [hashtable]$Components, [string]$Category = 'items', [string]$Group = $null, [string]$Icon = $null) {
    $iconKey = "lepton_mobsoverworld_$(if ($Icon) { $Icon } else { $Path })"
    $itemTextures[$iconKey] = [ordered]@{ textures = "textures/items/$(if ($Icon) { $Icon } else { $Path })" }
    $menu = [ordered]@{ category = $Category }
    if ($Group) { $menu.group = $Group }
    $comps = [ordered]@{ 'minecraft:icon' = $iconKey }
    foreach ($k in ($Components.Keys | Sort-Object)) { $comps[$k] = $Components[$k] }
    Write-Json (Join-Path $BP "items/$Path.json") ([ordered]@{
            format_version  = '1.21.60'
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Path"; menu_category = $menu }
                components  = $comps
            }
        })
}

# --- crab items ---------------------------------------------------------------------------------
$food = { param($n, $s) @{
        'minecraft:food'          = [ordered]@{ nutrition = $n; saturation_modifier = $s }
        'minecraft:use_animation' = 'eat'
        'minecraft:use_modifiers' = [ordered]@{ use_duration = 1.6; movement_modifier = 0.35 }
        'minecraft:tags'          = [ordered]@{ tags = @('minecraft:is_food') }
    } }
Write-Item 'crab_leg' (& $food 1 0.3) 'items' 'minecraft:itemGroup.name.miscFood'
Write-Item 'cooked_crab_leg' (& $food 8 0.8) 'items' 'minecraft:itemGroup.name.miscFood'
Write-Item 'crab_shell' @{ 'minecraft:max_stack_size' = 64 } 'items'
Write-Item 'crab_bucket' @{ 'minecraft:max_stack_size' = 1 } 'items' 'minecraft:itemGroup.name.bucket'
Add-Text 'item.lepton:crab_leg.name' 'Raw Crab Leg' '生螃蟹腿'
Add-Text 'item.lepton:cooked_crab_leg.name' 'Cooked Crab Leg' '熟螃蟹腿'
Add-Text 'item.lepton:crab_shell.name' 'Crab Shell' '螃蟹壳'
Add-Text 'item.lepton:crab_bucket.name' 'Bucket of Crab' '螃蟹桶'

# --- potions of Stability -----------------------------------------------------------------------
$strengths = @(
    @{ prefix = ''; en = ''; zh = '' }
    @{ prefix = 'long_'; en = ''; zh = '' }
    @{ prefix = 'strong_'; en = ''; zh = '' }
)
foreach ($s in $strengths) {
    $p = $s.prefix
    Write-Item "${p}resilience_potion" @{
        'minecraft:max_stack_size' = 1
        'minecraft:glint'          = $true
        'minecraft:food'           = [ordered]@{ nutrition = 0; saturation_modifier = 0; can_always_eat = $true; using_converts_to = 'minecraft:glass_bottle' }
        'minecraft:use_animation'  = 'drink'
        'minecraft:use_modifiers'  = [ordered]@{ use_duration = 1.6; movement_modifier = 0.35 }
    } 'items' 'minecraft:itemGroup.name.potion' 'resilience_potion'
    Add-Text "item.lepton:${p}resilience_potion.name" 'Potion of Stability' '稳定药水'
    foreach ($kind in 'splash', 'lingering') {
        $entity = "lepton:${p}resilience_${kind}_potion_entity"
        Write-Item "${p}resilience_${kind}_potion" @{
            'minecraft:max_stack_size' = 1
            'minecraft:glint'          = $true
            'minecraft:throwable'      = [ordered]@{ do_swing_animation = $true; launch_power_scale = 0.5; max_launch_power = 0.5 }
            'minecraft:projectile'     = [ordered]@{ projectile_entity = $entity; minimum_critical_power = 1.0 }
        } 'items' $(if ($kind -eq 'splash') { 'minecraft:itemGroup.name.splashPotion' } else { 'minecraft:itemGroup.name.lingeringPotion' }) "resilience_${kind}_potion"
        if ($kind -eq 'splash') { Add-Text "item.lepton:${p}resilience_${kind}_potion.name" 'Splash Potion of Stability' '喷溅型稳定药水' }
        else { Add-Text "item.lepton:${p}resilience_${kind}_potion.name" 'Lingering Potion of Stability' '滞留型稳定药水' }
        $path = "${p}resilience_${kind}_potion_entity"
        Write-Json (Join-Path $BP "entities/$path.json") ([ordered]@{
                format_version     = '1.21.0'
                'minecraft:entity' = [ordered]@{
                    description = [ordered]@{ identifier = $entity; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
                    components  = [ordered]@{
                        'minecraft:type_family'                         = [ordered]@{ family = @('lepton_potion', 'projectile') }
                        'minecraft:collision_box'                       = [ordered]@{ width = 0.25; height = 0.25 }
                        'minecraft:projectile'                          = [ordered]@{
                            on_hit       = [ordered]@{ remove_on_hit = @{} }
                            power        = 0.5
                            gravity      = 0.05
                            angle_offset = -20.0
                            inertia      = 0.99
                            liquid_inertia = 0.8
                            hit_sound    = 'glass'
                        }
                        'minecraft:physics'                             = @{}
                        'minecraft:pushable'                            = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $true }
                        'minecraft:conditional_bandwidth_optimization' = @{}
                    }
                }
            })
        Write-Json (Join-Path $RP "entity/$path.entity.json") ([ordered]@{
                format_version            = '1.10.0'
                'minecraft:client_entity' = [ordered]@{
                    description = [ordered]@{
                        identifier         = $entity
                        materials          = [ordered]@{ default = 'snowball' }
                        textures           = [ordered]@{ default = "textures/items/resilience_${kind}_potion" }
                        geometry           = [ordered]@{ default = 'geometry.item_sprite' }
                        render_controllers = @('controller.render.item_sprite')
                        animations         = [ordered]@{ flying = 'animation.actor.billboard' }
                        scripts            = [ordered]@{ animate = @('flying') }
                    }
                }
            })
        Add-Text "entity.$entity.name" $(if ($kind -eq 'splash') { 'Splash Potion of Stability' } else { 'Lingering Potion of Stability' }) $(if ($kind -eq 'splash') { '喷溅型稳定药水' } else { '滞留型稳定药水' })
    }
}

# --- entity names and spawn eggs ------------------------------------------------------------------
Add-Text 'entity.lepton:crab.name' 'Crab' '螃蟹'
Add-Text 'entity.lepton:shiba.name' 'Shiba' '柴犬'
Add-Text 'entity.lepton:toretoise.name' 'Oretoise' '矿石陆龟'
Add-Text 'item.spawn_egg.entity.lepton:crab.name' 'Crab Spawn Egg' '螃蟹刷怪蛋'
Add-Text 'item.spawn_egg.entity.lepton:shiba.name' 'Shiba Spawn Egg' '柴犬刷怪蛋'
Add-Text 'item.spawn_egg.entity.lepton:toretoise.name' 'Oretoise Spawn Egg' '矿石陆龟刷怪蛋'

# --- loot -----------------------------------------------------------------------------------------
Write-Json (Join-Path $BP 'loot_tables/entities/lepton_crab.json') ([ordered]@{
        pools = @(
            [ordered]@{
                rolls      = 1
                conditions = @([ordered]@{ condition = 'killed_by_player' }, [ordered]@{ condition = 'random_chance_with_looting'; chance = 0.25; looting_multiplier = 0.03 })
                entries    = @([ordered]@{ type = 'item'; name = 'lepton:crab_shell'; weight = 1 })
            }
            [ordered]@{
                rolls   = 1
                entries = @([ordered]@{
                        type      = 'item'; name = 'lepton:crab_leg'; weight = 1
                        functions = @(
                            [ordered]@{ function = 'set_count'; count = [ordered]@{ min = 0; max = 1 } }
                            [ordered]@{ function = 'furnace_smelt'; conditions = @([ordered]@{ condition = 'entity_properties'; entity = 'this'; properties = [ordered]@{ on_fire = $true } }) }
                            [ordered]@{ function = 'looting_enchant'; count = [ordered]@{ min = 0; max = 1 } }
                        )
                    })
            }
        )
    })
Write-Json (Join-Path $BP 'loot_tables/entities/lepton_empty.json') ([ordered]@{ pools = @() })

# --- recipes --------------------------------------------------------------------------------------
Write-Json (Join-Path $BP 'recipes/cooked_crab_leg.json') ([ordered]@{
        format_version             = '1.21.60'
        'minecraft:recipe_furnace' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:cooked_crab_leg' }
            tags        = @('furnace', 'smoker', 'campfire', 'soul_campfire')
            input       = 'lepton:crab_leg'
            output      = 'lepton:cooked_crab_leg'
        }
    })

# --- fragments ------------------------------------------------------------------------------------
Write-Json (Join-Path $RP 'item_texture.d/mobsoverworld.json') $itemTextures
foreach ($loc in @(@{ dir = 'en_US'; k = 'en' }, @{ dir = 'zh_CN'; k = 'zh' })) {
    $lines = foreach ($key in $texts.Keys) { "$key=$($texts[$key][$loc.k])" }
    $path = Join-Path $RP "texts.d/$($loc.dir)/mobsoverworld.lang"
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}
Write-Host 'mobsoverworld: pack files written'
