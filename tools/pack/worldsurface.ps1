# U28 worldsurface: Monster Box and the Glimmering Weald blocks (glow shroom, glow shroom block /
# stem / ring, glow lichen growth, potted variants), their textures, geometry, loot tables, the
# Monster Box loot tables, the growl sound and the Glimmering Weald fog.
#
#   pwsh -NoProfile -File tools/pack/worldsurface.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/worldsurface.ps1"

Start-PackUnit 'worldsurface'

$texOut = {
    param($kind, $name)
    Add-PackFile (Join-Path $script:RP "textures/$kind/$name.png")
}
New-WorldsurfaceTextures $texOut

function T([string]$Name) { return "textures/blocks/$Name" }
function Mat([string]$Texture, [string]$Render = 'opaque') {
    return [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = $Render }
}
function Face([string]$Mi, [int[]]$Uv = @(0, 0), [int[]]$Size = @(16, 16)) {
    return [ordered]@{ uv = $Uv; uv_size = $Size; material_instance = $Mi }
}
function Write-Geo([string]$Name, [object[]]$Bones) {
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = "geometry.lepton.worldsurface_$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($Bones)
            })
    }
    Write-PackJson (Join-Path $script:RP "models/blocks/lepton_worldsurface_$Name.geo.json") $geo
    return "geometry.lepton.worldsurface_$Name"
}
function Bone([string]$Name, [object[]]$Cubes) { return [ordered]@{ name = $Name; pivot = @(0, 0, 0); cubes = @($Cubes) } }
# Two crossed planes, $Y0 = bottom, $Scale = plane size in pixels.
function Get-CrossCubes([double]$Y0 = 0, [double]$Size = 16) {
    $h = $Size / 2
    foreach ($rot in 45, -45) {
        [ordered]@{
            origin = @(-$h, $Y0, 0); size = @($Size, $Size, 0); pivot = @(0, $Y0, 0); rotation = @(0, $rot, 0)
            uv = [ordered]@{ north = (Face 'plant'); south = (Face 'plant') }
        }
    }
}
function Box([double[]]$Origin, [double[]]$Size) { return [ordered]@{ origin = $Origin; size = $Size } }
function Write-Loot([string]$Path, $Pools) {
    Write-PackJson (Join-Path $script:BP "loot_tables/$Path.json") ([ordered]@{ pools = @($Pools) })
    return "loot_tables/$Path.json"
}
function Base([double]$Hardness, [double]$Resistance, [string]$MapColor, [string[]]$Tags) {
    $c = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = $Hardness }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = $Resistance }
    }
    if ($MapColor) { $c['minecraft:map_color'] = $MapColor }
    foreach ($t in $Tags) { $c["tag:$t"] = [ordered]@{} }
    return $c
}
$nature = [ordered]@{ category = 'nature' }
$hidden = [ordered]@{ category = 'none'; is_hidden_in_commands = $false }
$selfLoot = { param($id) Write-Loot "blocks/$(Get-Path $id)" @([ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = $id; weight = 1 }) }) }

# ---- Monster Box ------------------------------------------------------------------------------
$geoBox = Write-Geo 'monster_box' @(
    (Bone 'cage' @([ordered]@{ origin = @(-8, 0, -8); size = @(16, 16, 16); uv = [ordered]@{
                    north = (Face 'cage'); south = (Face 'cage'); east = (Face 'cage'); west = (Face 'cage'); up = (Face 'cage'); down = (Face 'cage') } })),
    (Bone 'core' @([ordered]@{ origin = @(-5, 3, -5); size = @(10, 10, 10); uv = [ordered]@{
                    north = (Face 'core' @(3, 3) @(10, 10)); south = (Face 'core' @(3, 3) @(10, 10)); east = (Face 'core' @(3, 3) @(10, 10))
                    west = (Face 'core' @(3, 3) @(10, 10)); up = (Face 'core' @(3, 3) @(10, 10)); down = (Face 'core' @(3, 3) @(10, 10)) } }))
)
$c = Base 25 25 '#a7a7a7' @('minecraft:is_pickaxe_item_destructible', 'stone', 'lepton:monster_box')
$c['minecraft:geometry'] = $geoBox
$c['minecraft:material_instances'] = [ordered]@{
    '*' = (Mat (T 'monster_box') 'alpha_test'); cage = (Mat (T 'monster_box') 'alpha_test'); core = (Mat (T 'monster_box_core') 'alpha_test')
}
$c['minecraft:loot'] = Write-Loot 'blocks/monster_box' @()
Write-BlockJson -Id 'lepton:monster_box' -Menu ([ordered]@{ category = 'items' }) -Components $c
Register-BlockClient 'lepton:monster_box' 'metal' @{ en = 'Monster Box'; zh = '怪物盒子' }

# Quark's misc/monster_box (extra drops of box mobs) and misc/monster_box_spawns (one roll per mob).
$e = { param($name, $w, $min, $max) $x = [ordered]@{ type = 'item'; name = $name; weight = $w }; if ($min) { $x.functions = @([ordered]@{ function = 'set_count'; count = [ordered]@{ min = $min; max = $max } }) }; $x }
$d = { param($name, $w, $min, $max) [ordered]@{ type = 'item'; name = $name; weight = $w; functions = @([ordered]@{ function = 'set_damage'; damage = [ordered]@{ min = $min; max = $max } }) } }
$discs = '13', 'cat', 'blocks', 'chirp', 'far', 'mall', 'mellohi', 'stal', 'strad', 'ward', '11', 'wait'
$entries = @(
    (& $e 'minecraft:raw_iron' 40 4 10), (& $e 'minecraft:raw_copper' 40 4 10), (& $e 'minecraft:raw_gold' 30 3 8),
    (& $e 'minecraft:diamond' 10 1 2), (& $e 'minecraft:web' 30 3 10), (& $e 'minecraft:tnt' 10 1 3),
    (& $e 'minecraft:infested_stone' 4 12 32), (& $e 'minecraft:infested_cobblestone' 4 12 32), (& $e 'minecraft:infested_stone_bricks' 4 12 32),
    (& $e 'minecraft:zombie_head' 10), (& $e 'minecraft:creeper_head' 10), (& $e 'minecraft:skeleton_skull' 10),
    (& $e 'minecraft:bone_block' 40 8 16), (& $e 'minecraft:coal' 40 12 24),
    (& $d 'minecraft:diamond_pickaxe' 1 0.2 0.5), (& $d 'minecraft:diamond_sword' 1 0.2 0.49),
    (& $e 'minecraft:bread' 25 4 8), (& $e 'minecraft:rabbit_hide' 25 4 8), (& $e 'minecraft:golden_apple' 1), (& $e 'minecraft:name_tag' 10),
    [ordered]@{ type = 'item'; name = 'minecraft:book'; weight = 5; functions = @([ordered]@{ function = 'enchant_randomly' }) }
) + @($discs | ForEach-Object { & $e "minecraft:music_disc_$_" 1 }) + @(
    (& $e 'minecraft:iron_horse_armor' 6), (& $e 'minecraft:golden_horse_armor' 3), (& $e 'minecraft:diamond_horse_armor' 1),
    (& $e 'minecraft:saddle' 4), (& $e 'lepton:monster_box' 1)
)
$null = Write-Loot 'lepton/monster_box' @([ordered]@{ rolls = 1; entries = $entries })
$null = Write-Loot 'lepton/monster_box_spawns' @([ordered]@{ rolls = 1; entries = @(
            (& $e 'minecraft:witch_spawn_egg' 1), (& $e 'minecraft:cave_spider_spawn_egg' 2), (& $e 'minecraft:zombie_spawn_egg' 7)) })

# ---- Glow Shroom / Glow Lichen Growth (crossed sprites) ---------------------------------------
$geoCross = Write-Geo 'cross' @((Bone 'plant' @(Get-CrossCubes 0 16)))
$plantSel = Box @(-3, 0, -3) @(6, 6, 6)

$c = Base 0 0 '#4ec3d6' @('lepton:glow_shroom', 'plant')
$c['minecraft:geometry'] = $geoCross
$c['minecraft:material_instances'] = [ordered]@{ '*' = (Mat (T 'glow_shroom') 'alpha_test'); plant = (Mat (T 'glow_shroom') 'alpha_test') }
$c['minecraft:light_emission'] = 10
$c['minecraft:light_dampening'] = 0
$c['minecraft:collision_box'] = $false
$c['minecraft:selection_box'] = $plantSel
$c['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up'); block_filter = @('minecraft:deepslate', 'minecraft:barrier') }) }
$c['minecraft:loot'] = & $selfLoot 'lepton:glow_shroom'
Write-BlockJson -Id 'lepton:glow_shroom' -Menu $nature -Components $c
Register-BlockClient 'lepton:glow_shroom' 'fungus' @{ en = 'Glow Shroom'; zh = '发光菇' }

$c = Base 0 0 '#5f8f6f' @('lepton:glow_lichen_growth', 'plant')
$c['minecraft:geometry'] = $geoCross
$c['minecraft:material_instances'] = [ordered]@{ '*' = (Mat (T 'glow_lichen_growth') 'alpha_test'); plant = (Mat (T 'glow_lichen_growth') 'alpha_test') }
$c['minecraft:light_emission'] = 8
$c['minecraft:light_dampening'] = 0
$c['minecraft:collision_box'] = $false
$c['minecraft:selection_box'] = $plantSel
$c['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up') }) }
$c['minecraft:loot'] = & $selfLoot 'lepton:glow_lichen_growth'
Write-BlockJson -Id 'lepton:glow_lichen_growth' -Menu $nature -Components $c
Register-BlockClient 'lepton:glow_lichen_growth' 'vines' @{ en = 'Glow Lichen Growth'; zh = '发光地衣增长物' }

# ---- Glow Shroom Block / Stem (huge-mushroom faces) -------------------------------------------
# One bone per (face, skin|inside); the face states pick the visible bone. The model's x axis is
# mirrored (its "east" face is the world's west face).
$faceKey = [ordered]@{ north = 'north'; south = 'south'; east = 'west'; west = 'east'; up = 'up'; down = 'down' }
$bones = foreach ($f in $faceKey.Keys) {
    foreach ($kind in 'skin', 'inner') {
        Bone "${f}_$kind" @([ordered]@{ origin = @(-8, 0, -8); size = @(16, 16, 16); uv = [ordered]@{ $faceKey[$f] = (Face $kind) } })
    }
}
$geoHuge = Write-Geo 'huge_shroom' @($bones)
$vis = [ordered]@{}
foreach ($f in $faceKey.Keys) {
    $vis["${f}_skin"] = "q.block_state('lepton:$f')"
    $vis["${f}_inner"] = "!q.block_state('lepton:$f')"
}
$faceStates = [ordered]@{}
foreach ($f in $faceKey.Keys) { $faceStates["lepton:$f"] = @($true, $false) }
$shroomDrops = Write-Loot 'blocks/glow_shroom_block' @([ordered]@{ rolls = 1; entries = @(
            [ordered]@{ type = 'empty'; weight = 7 },
            [ordered]@{ type = 'item'; name = 'lepton:glow_shroom'; weight = 1 },
            [ordered]@{ type = 'item'; name = 'lepton:glow_shroom'; weight = 1; functions = @([ordered]@{ function = 'set_count'; count = 2 }) }) })
foreach ($spec in @(
        @{ Id = 'lepton:glow_shroom_block'; Skin = 'glow_shroom_block'; Light = 12; Map = '#41b9d1'; Loot = $shroomDrops; Name = @{ en = 'Glow Shroom Block'; zh = '发光菇方块' } },
        @{ Id = 'lepton:glow_shroom_stem'; Skin = 'glow_shroom_stem'; Light = 0; Map = '#d7e1db'; Loot = (Write-Loot 'blocks/glow_shroom_stem' @()); Name = @{ en = 'Glow Shroom Stem'; zh = '发光菇柄' } })) {
    $c = Base 0.2 0.2 $spec.Map @('minecraft:is_axe_item_destructible', 'lepton:glow_shroom_block')
    $c['minecraft:geometry'] = [ordered]@{ identifier = $geoHuge; bone_visibility = $vis }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Mat (T $spec.Skin)); skin = (Mat (T $spec.Skin)); inner = (Mat (T 'glow_shroom_inside')) }
    if ($spec.Light) { $c['minecraft:light_emission'] = $spec.Light }
    $c['minecraft:loot'] = $spec.Loot
    Write-BlockJson -Id $spec.Id -Menu $nature -States $faceStates -Components $c
    Register-BlockClient $spec.Id 'wood' $spec.Name
}

# ---- Glow Shroom Ring ---------------------------------------------------------------------------
# facing = the clicked face (minecraft:block_face): the ring sticks out of its wall that way.
$ringBoxes = [ordered]@{
    # model coordinates (x mirrored): [origin], [size]
    north = @(@(-8, 7, -3), @(16, 2, 11)); south = @(@(-8, 7, -8), @(16, 2, 11))
    west  = @(@(-8, 7, -8), @(11, 2, 16)); east = @(@(-3, 7, -8), @(11, 2, 16))
}
$ringSel = [ordered]@{
    north = (Box @(-8, 4, -3) @(16, 8, 11)); south = (Box @(-8, 4, -8) @(16, 8, 11))
    west  = (Box @(-3, 4, -8) @(11, 8, 16)); east = (Box @(-8, 4, -8) @(11, 8, 16))
}
$ringBones = foreach ($f in $ringBoxes.Keys) {
    $o = $ringBoxes[$f][0]; $s = $ringBoxes[$f][1]
    Bone "ring_$f" @([ordered]@{ origin = $o; size = $s; uv = [ordered]@{
                up = (Face 'ring'); down = (Face 'ring'); north = (Face 'ring' @(0, 0) @(16, 2)); south = (Face 'ring' @(0, 0) @(16, 2))
                east = (Face 'ring' @(0, 0) @(16, 2)); west = (Face 'ring' @(0, 0) @(16, 2)) } })
}
$geoRing = Write-Geo 'ring' @($ringBones)
$rvis = [ordered]@{}
foreach ($f in $ringBoxes.Keys) { $rvis["ring_$f"] = "q.block_state('minecraft:block_face') == '$f'" }
$c = Base 0 0 '#a7a7a7' @('lepton:glow_shroom_ring', 'plant')
$c['minecraft:geometry'] = [ordered]@{ identifier = $geoRing; bone_visibility = $rvis }
$c['minecraft:material_instances'] = [ordered]@{ '*' = (Mat (T 'glow_shroom_ring') 'alpha_test'); ring = (Mat (T 'glow_shroom_ring') 'alpha_test') }
$c['minecraft:light_dampening'] = 0
$c['minecraft:collision_box'] = $false
$c['minecraft:selection_box'] = $ringSel.north
$c['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('side') }) }
$c['minecraft:liquid_detection'] = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
$c['minecraft:loot'] = & $selfLoot 'lepton:glow_shroom_ring'
$perms = foreach ($f in 'south', 'west', 'east') {
    [ordered]@{ condition = "q.block_state('minecraft:block_face') == '$f'"; components = [ordered]@{ 'minecraft:selection_box' = $ringSel[$f] } }
}
$traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
Write-BlockJson -Id 'lepton:glow_shroom_ring' -Menu $nature -Traits $traits -Components $c -Permutations $perms
Register-BlockClient 'lepton:glow_shroom_ring' 'fungus' @{ en = 'Glow Shroom Ring'; zh = '发光菇环' }

# ---- Potted plants -------------------------------------------------------------------------------
$geoPot = Write-Geo 'potted' @(
    (Bone 'pot' @([ordered]@{ origin = @(-3, 0, -3); size = @(6, 6, 6); uv = [ordered]@{
                    north = (Face 'pot' @(5, 10) @(6, 6)); south = (Face 'pot' @(5, 10) @(6, 6)); east = (Face 'pot' @(5, 10) @(6, 6))
                    west = (Face 'pot' @(5, 10) @(6, 6)); up = (Face 'dirt' @(5, 5) @(6, 6)); down = (Face 'pot' @(5, 5) @(6, 6)) } })),
    (Bone 'plant' @(Get-CrossCubes 4 16))
)
foreach ($spec in @(
        @{ Id = 'lepton:potted_glow_shroom'; Plant = 'lepton:glow_shroom'; Tex = 'glow_shroom'; Light = 10; Name = @{ en = 'Potted Glow Shroom'; zh = '发光菇盆栽' } },
        @{ Id = 'lepton:potted_glow_lichen_growth'; Plant = 'lepton:glow_lichen_growth'; Tex = 'glow_lichen_growth'; Light = 8; Name = @{ en = 'Potted Glow Lichen'; zh = '发光地衣盆栽' } })) {
    $c = Base 0 0 '#7c4536' @('lepton:potted_plant')
    $c['minecraft:geometry'] = $geoPot
    $c['minecraft:material_instances'] = [ordered]@{
        '*' = (Mat 'textures/blocks/flower_pot' 'alpha_test'); pot = (Mat 'textures/blocks/flower_pot' 'alpha_test'); dirt = (Mat 'textures/blocks/dirt' 'alpha_test')
        plant = (Mat (T $spec.Tex) 'alpha_test')
    }
    $c['minecraft:light_emission'] = $spec.Light
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:collision_box'] = Box @(-3, 0, -3) @(6, 6, 6)
    $c['minecraft:selection_box'] = Box @(-3, 0, -3) @(6, 6, 6)
    $c['minecraft:loot'] = Write-Loot "blocks/$(Get-Path $spec.Id)" @(
        [ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = 'minecraft:flower_pot'; weight = 1 }) },
        [ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = $spec.Plant; weight = 1 }) })
    Write-BlockJson -Id $spec.Id -Menu $hidden -Components $c
    Register-BlockClient $spec.Id 'stone' $spec.Name
}

# ---- Sounds, fog ------------------------------------------------------------------------------------
Write-PackJson (Join-Path $script:RP 'sounds.d/worldsurface.json') ([ordered]@{
        'lepton.monster_box.growl' = [ordered]@{
            category = 'block'; subtitle = 'quark.subtitles.growl'
            sounds   = @(1..4 | ForEach-Object { [ordered]@{ name = "sounds/mob/ravager/idle$_"; pitch = 0.6; volume = 1.0 } })
        }
    })
Add-PackText 'quark.subtitles.growl' @{ en = 'Monsters arise'; zh = '怪物出现' }
Write-PackJson (Join-Path $script:RP 'fogs/glimmering_weald.json') ([ordered]@{
        format_version          = '1.16.100'
        'minecraft:fog_settings' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:glimmering_weald' }
            distance    = [ordered]@{
                air   = [ordered]@{ fog_start = 0.92; fog_end = 1.0; fog_color = '#C0D8FF'; render_distance_type = 'render' }
                water = [ordered]@{ fog_start = 0; fog_end = 60.0; fog_color = '#050533'; render_distance_type = 'fixed' }
            }
        }
    })

Complete-PackUnit
