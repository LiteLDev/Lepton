# U23 gadgets pack: items (abacus, pathfinder's quill, ambience discs, Endermosh), the tiny potato
# block (geometry for every look and jump frame), recipes, sounds and names.
#
#   pwsh tools/pack/gadgets.ps1           # textures + JSON (keeps the committed .ogg files)
#   pwsh tools/pack/gadgets.ps1 -Audio    # also re-synthesises the audio (python + ffmpeg, ~3 min)
#
# Output is committed. See docs/spec/gadgets.md.

param([switch]$Audio)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$sounds = @('ambient_drips', 'ambient_ocean', 'ambient_rain', 'ambient_wind', 'ambient_fire', 'ambient_clock',
    'ambient_crickets', 'ambient_chatter', 'endermosh', 'potato_do_it', 'potato_soda', 'potato_yung')
$soundDir = Join-Path $script:RP 'sounds/lepton/gadgets'

# The audio is expensive to make: keep it out of Start-PackUnit's cleanup unless -Audio.
if ($Audio) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) "lepton-gadgets-audio"
    New-Item -ItemType Directory -Force $tmp | Out-Null
    & python -I (Join-Path $PSScriptRoot 'gadgets_audio.py') $tmp
    if ($LASTEXITCODE) { throw 'gadgets_audio.py failed' }
    New-Item -ItemType Directory -Force $soundDir | Out-Null
    foreach ($s in $sounds) {
        & ffmpeg -hide_banner -loglevel error -y -i (Join-Path $tmp "$s.wav") -c:a libvorbis -q:a 3 -map_metadata -1 `
            -fflags +bitexact -flags:a +bitexact (Join-Path $soundDir "$s.ogg")
        if ($LASTEXITCODE) { throw "ffmpeg failed for $s" }
    }
}

Start-PackUnit 'gadgets'

$texOut = & pwsh -NoProfile -File (Join-Path $PSScriptRoot '../textures/gadgets.ps1')
if ($LASTEXITCODE) { throw 'tools/textures/gadgets.ps1 failed' }
foreach ($p in $texOut) { [void](Add-PackFile $p) }
# The .ogg files are not recorded (Start-PackUnit would delete them): they only change with -Audio.
foreach ($s in $sounds) {
    $p = Join-Path $soundDir "$s.ogg"
    if (-not (Test-Path $p)) { throw "missing ${p}: run with -Audio" }
}

$tex = 'textures/items/lepton_gadgets'
$btex = 'textures/blocks/lepton_gadgets'

function Write-Item([string]$Path, [string]$Icon, [hashtable]$Name, $Menu, $Extra) {
    $id = "lepton:$Path"
    # data-driven items show "item.<id>" without a display_name: point it at the texts.d key
    $c = [ordered]@{ 'minecraft:icon' = (Add-ItemTexture "$tex/$Icon"); 'minecraft:display_name' = [ordered]@{ value = "item.$id.name" }; 'minecraft:max_stack_size' = 1 }
    if ($Extra) { foreach ($k in $Extra.Keys) { $c[$k] = $Extra[$k] } }
    Write-PackJson (Join-Path $script:BP "items/$Path.json") ([ordered]@{
            format_version   = $script:ItemFormat
            'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = $id; menu_category = $Menu }; components = $c }
        })
    Add-PackText "item.$id.name" $Name
}

# ---- items --------------------------------------------------------------------------------------
Write-Item 'abacus' 'abacus' @{ en = 'Abacus'; zh = '算盘' } ([ordered]@{ category = 'equipment' })
Write-Item 'pathfinders_quill' 'pathfinders_quill' @{ en = "Pathfinder's Quill"; zh = '寻路羽毛笔' } ([ordered]@{ category = 'equipment' })

# Record items only accept vanilla sound events: the discs name record.13 and Lepton replaces its playback
# natively (src/feature/gadgets/Discs.cpp) with the lepton.ambient.* / lepton.music.endermosh sounds.
$discMenu = [ordered]@{ category = 'items'; group = 'minecraft:itemGroup.name.record' }
foreach ($d in 'drips', 'ocean', 'rain', 'wind', 'fire', 'clock', 'crickets', 'chatter') {
    Write-Item "music_disc_$d" "music_disc_$d" @{ en = 'Ambience Disc'; zh = '环境唱片' } $discMenu ([ordered]@{
            'minecraft:rarity' = 'rare'
            'minecraft:record' = [ordered]@{ comparator_signal = 15; duration = 1000000; sound_event = 'record.13' }
        })
}
Write-Item 'music_disc_endermosh' 'music_disc_endermosh' @{ en = 'Music Disc'; zh = '音乐唱片' } $discMenu ([ordered]@{
        'minecraft:rarity' = 'rare'
        'minecraft:record' = [ordered]@{ comparator_signal = 14; duration = 189.4; sound_event = 'record.13' }
    })

# ---- recipes ------------------------------------------------------------------------------------
Write-PackJson (Join-Path $script:BP 'recipes/abacus.json') ([ordered]@{
        format_version            = $script:RecipeFormat
        'minecraft:recipe_shaped' = [ordered]@{
            description = [ordered]@{ identifier = 'lepton:abacus' }
            tags        = @('crafting_table')
            pattern     = @('WSW', 'WIW', 'WSW')
            key         = [ordered]@{ I = [ordered]@{ item = 'minecraft:iron_ingot' }; S = [ordered]@{ item = 'minecraft:stick' }; W = [ordered]@{ tag = 'minecraft:planks' } }
            unlock      = @([ordered]@{ item = 'minecraft:iron_ingot' })
            result      = [ordered]@{ item = 'lepton:abacus'; count = 1 }
        }
    })
Write-ShapedRecipe 'tiny_potato_heart' @('H', 'P') @{ H = 'lepton:diamond_heart'; P = 'minecraft:potato' } 'lepton:tiny_potato' 1
Write-ShapedRecipe 'tiny_potato_no_heart' @('D', 'P') @{ D = 'minecraft:diamond'; P = 'minecraft:potato' } 'lepton:tiny_potato' 1
Write-ShapedRecipe 'tiny_potato_no_heart_emerald' @('D', 'P') @{ D = 'minecraft:emerald'; P = 'minecraft:potato' } 'lepton:tiny_potato' 1

# ---- tiny potato ----------------------------------------------------------------------------------
# Looks (variant index) -> texture and shape. Keep in sync with src/feature/gadgets/Potato.cpp.
$variants = @(
    @('default', 'base'), @('angry', 'base'), @('halloween', 'base'), @('ace', 'base'), @('agender', 'base'),
    @('aro', 'base'), @('aureylian', 'base'), @('bi', 'base'), @('bob', 'bob'), @('bosnia', 'base'),
    @('default', 'dinnerbone'), @('enby', 'base'), @('pride', 'base'), @('genderfluid', 'base'), @('lesbian', 'base'),
    @('gray', 'base'), @('default', 'double'), @('default', 'flat'), @('pan', 'base'), @('robo', 'base'),
    @('default', 'triple'), @('default', 'sus'), @('tomato', 'tomato'), @('trans', 'base'), @('default', 'invisible')
)
$frames = 10

# UV regions (texture pixels) of the potato texture layout.
function Get-TaterUv([double]$SideH = 6) {
    return [ordered]@{
        north = [ordered]@{ uv = @(4, 4); uv_size = @(4, $SideH) }
        south = [ordered]@{ uv = @(12, 4); uv_size = @(4, $SideH) }
        east  = [ordered]@{ uv = @(8, 4); uv_size = @(4, $SideH) }
        west  = [ordered]@{ uv = @(0, 4); uv_size = @(4, $SideH) }
        up    = [ordered]@{ uv = @(4, 0); uv_size = @(4, 4) }
        down  = [ordered]@{ uv = @(8, 0); uv_size = @(4, 4) }
    }
}
$tomatoUv = [ordered]@{
    north = [ordered]@{ uv = @(5, 0); uv_size = @(5, 5) }; south = [ordered]@{ uv = @(5, 5); uv_size = @(5, 5) }
    east = [ordered]@{ uv = @(10, 0); uv_size = @(5, 5) }; west = [ordered]@{ uv = @(10, 5); uv_size = @(5, 5) }
    up = [ordered]@{ uv = @(0, 0); uv_size = @(5, 5) }; down = [ordered]@{ uv = @(0, 5); uv_size = @(5, 5) }
}
function Get-GlassesUv {
    $f = { param($u, $w) [ordered]@{ uv = @($u, 0); uv_size = @($w, 5); material_instance = 'glasses' } }
    return [ordered]@{ north = & $f 0 16; south = & $f 0 16; east = & $f 0 1; west = & $f 15 1; up = & $f 0 1; down = & $f 0 1 }
}

# A cube in block pixel coordinates (0..16, x east, y up, z south), offset by (dx, dy).
function New-TaterCube([double]$X0, [double]$Y0, [double]$Z0, [double]$X1, [double]$Y1, [double]$Z1, $Uv, [double]$Dx = 0, [double]$Dy = 0) {
    return [ordered]@{
        origin = @((Get-Num ([Math]::Round(8 - $X1 - $Dx, 4))), (Get-Num ([Math]::Round($Y0 + $Dy, 4))), (Get-Num ($Z0 - 8)))
        size   = @((Get-Num ($X1 - $X0)), (Get-Num ($Y1 - $Y0)), (Get-Num ($Z1 - $Z0)))
        uv     = $Uv
    }
}

function Get-ShapeCubes([string]$Shape, [double]$Dx, [double]$Dy) {
    $c = { param($a, $b, $cc, $d, $e, $f, $uv) New-TaterCube $a $b $cc $d $e $f $uv $Dx $Dy }
    switch ($Shape) {
        'base' { return @(& $c 6 0 6 10 6 10 (Get-TaterUv)) }
        'dinnerbone' { return @(& $c 6 0 6 10 6 10 (Get-TaterUv)) }
        'bob' { return @(& $c 6 0 6 10 4 10 (Get-TaterUv 4)) }
        'flat' { return @(& $c 6 0 6 10 1.8 10 (Get-TaterUv)) }
        'double' { return @((& $c 6 0 6 10 6 10 (Get-TaterUv)), (& $c -2 0 6 2 6 10 (Get-TaterUv))) }
        'triple' { return @((& $c 6 0 6 10 6 10 (Get-TaterUv)), (& $c 4 0 7 6 3 9 (Get-TaterUv)), (& $c 10 0 7 12 3 9 (Get-TaterUv))) }
        'sus' {
            $pack = [ordered]@{}
            foreach ($f in 'north', 'south', 'east', 'west', 'up', 'down') { $pack[$f] = [ordered]@{ uv = @(12, 5); uv_size = @(3, 3) } }
            return @((& $c 6 0 6 10 6 10 (Get-TaterUv)), (& $c 6.5 1.5 10 9.5 5 10.5 $pack), (& $c 6.2 4.3 5.8 9.8 5.3 6 (Get-GlassesUv)))
        }
        'tomato' { return @(& $c 5.5 0 5.5 10.5 5 10.5 $tomatoUv) }
    }
    return @()
}

# One block geometry, a bone per shape shown by bone_visibility (like the shape generators); the
# dinnerbone shape is the base potato turned upside down. The jump (Quark: t ticks left -> up
# |sin(t/10 pi)| * 0.2 blocks, wiggle sin * 0.05 blocks along the potato's x) is a per-frame
# minecraft:transformation together with the facing rotation (frame k shows t = 2k). Quark's
# extra 2 degree tilt is left out: block transformations rotate in 90 degree steps only.
$shapes = @('base', 'bob', 'dinnerbone', 'flat', 'double', 'triple', 'sus', 'tomato')
$bones = [System.Collections.Generic.List[object]]::new()
foreach ($shape in $shapes) {
    $bone = [ordered]@{ name = $shape; pivot = @(0, 3, 0) }
    if ($shape -eq 'dinnerbone') { $bone.rotation = @(0, 0, 180) }
    $bone.cubes = @(Get-ShapeCubes $shape 0 0)
    $bones.Add($bone)
}
Write-PackJson (Join-Path $script:RP 'models/blocks/lepton_gadgets_tiny_potato.geo.json') ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = 'geometry.lepton.tiny_potato'; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0)
                }
                bones       = @($bones)
            })
    })

# Block states hold at most 16 values: the look is lepton:look (0..15) + 16 * lepton:look_page (0..1).
$vc = { param($v) "(q.block_state('lepton:look') == $($v % 16) && q.block_state('lepton:look_page') == $([Math]::Floor($v / 16)))" }
$mat = { param($look) [ordered]@{ texture = (Add-TerrainTexture "$btex/tiny_potato_$look"); render_method = 'alpha_test' } }
$glasses = & $mat 'sus'
$box = [ordered]@{ origin = @(-2, 0, -2); size = @(4, 6, 4) }
$visibility = [ordered]@{}
$shapeCond = @{}
foreach ($shape in $shapes + @('invisible')) {
    if ($shape -eq 'base') { continue }
    $vs = @(for ($v = 0; $v -lt $variants.Count; $v++) { if ($variants[$v][1] -eq $shape) { $v } })
    $shapeCond[$shape] = Join-Or @($vs | ForEach-Object { & $vc $_ })
}
$shapeCond['base'] = "!($(Join-Or @($shapes + @('invisible') | Where-Object { $_ -ne 'base' } | ForEach-Object { $shapeCond[$_] })))"
foreach ($shape in $shapes) { $visibility[$shape] = $shapeCond[$shape] }
$c = [ordered]@{
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.25 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 0.25 }
    'minecraft:map_color'                 = '#cfa462'
    'minecraft:light_dampening'           = 0
    'minecraft:collision_box'             = $box
    'minecraft:selection_box'             = $box
    'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.lepton.tiny_potato'; bone_visibility = $visibility }
    'minecraft:material_instances'        = [ordered]@{ '*' = (& $mat 'default'); glasses = $glasses }
    'minecraft:loot'                      = 'loot_tables/empty.json'
    'minecraft:liquid_detection'          = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
    'tag:lepton:tiny_potato'              = [ordered]@{}
}
$perms = [System.Collections.Generic.List[object]]::new()
# facing (model front = north) x jump frame
foreach ($r in @(@('north', 0, 1, 0), @('west', 90, 0, 1), @('south', 180, -1, 0), @('east', 270, 0, -1))) {
    for ($f = 0; $f -le $frames; $f++) {
        $s = [Math]::Sin(($f * 2) / 10.0 * [Math]::PI)
        $up = [Math]::Round([Math]::Abs($s) * 0.2, 4)
        $wig = [Math]::Round($s * 0.05, 4)
        # Quark translates along the potato's local x after turning it: world offset of local +x
        $tr = @((Get-Num ([Math]::Round($wig * $r[2], 4))), (Get-Num $up), (Get-Num ([Math]::Round($wig * $r[3], 4))))
        $perms.Add([ordered]@{ condition = "q.block_state('minecraft:cardinal_direction') == '$($r[0])' && q.block_state('lepton:jump') == $f"
                components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, $r[1], 0); translation = $tr } } })
    }
}
for ($v = 1; $v -lt $variants.Count; $v++) {
    $look = $variants[$v][0]
    if ($look -eq 'default') { continue }
    $perms.Add([ordered]@{ condition = (& $vc $v); components = [ordered]@{ 'minecraft:material_instances' = [ordered]@{ '*' = (& $mat $look); glasses = $glasses } } })
}
Write-BlockJson -Id 'lepton:tiny_potato' -Menu ([ordered]@{ category = 'items' }) `
    -States ([ordered]@{ 'lepton:look' = @(0..15); 'lepton:look_page' = @(0, 1); 'lepton:jump' = @(0..$frames) }) `
    -Traits ([ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 180 } }) `
    -Components $c -Permutations $perms
Register-BlockClient 'lepton:tiny_potato' 'cloth' @{ en = 'Tiny Potato'; zh = '小土豆' }

# ---- held items: one display entity per item, holding it like a hand ----------------------------
# Properties: lepton:face (the potato side: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east; the
# entity is turned towards that side, or the potato's facing for up/down), lepton:kind (0 item
# sprite, 1 block, 2 a tiny potato: my son). Bones mirror Bedrock's held-item rig (rightItem).
Write-PackJson (Join-Path $script:BP 'entities/tiny_potato_item.json') ([ordered]@{
        format_version     = '1.21.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{
                identifier = 'lepton:tiny_potato_item'; is_spawnable = $false; is_summonable = $true; is_experimental = $false
                properties = [ordered]@{
                    'lepton:face' = [ordered]@{ type = 'int'; range = @(0, 5); default = 1; client_sync = $true }
                    'lepton:kind' = [ordered]@{ type = 'int'; range = @(0, 2); default = 0; client_sync = $true }
                    'lepton:yaw'  = [ordered]@{ type = 'int'; range = @(0, 3); default = 0; client_sync = $true }
                }
            }
            components  = [ordered]@{
                'minecraft:type_family'                        = [ordered]@{ family = @('lepton_potato_item', 'inanimate') }
                'minecraft:collision_box'                      = [ordered]@{ width = 0.01; height = 0.01 }
                'minecraft:physics'                            = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'                           = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:damage_sensor'                      = [ordered]@{ triggers = [ordered]@{ cause = 'all'; deals_damage = 'no' } }
                'minecraft:health'                             = [ordered]@{ value = 1; max = 1 }
                'minecraft:fire_immune'                        = [ordered]@{}
                'minecraft:knockback_resistance'               = [ordered]@{ value = 1.0 }
                'minecraft:body_rotation_blocked'              = [ordered]@{}
                'minecraft:conditional_bandwidth_optimization' = [ordered]@{
                    default_values = [ordered]@{ max_optimized_distance = 0.0; max_dropped_ticks = 0; use_motion_prediction_hints = $false }
                }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'models/entity/lepton_tiny_potato_item.geo.json') ([ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = 'geometry.lepton.tiny_potato_item'; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0)
                }
                bones       = @(
                    [ordered]@{ name = 'root'; pivot = @(0, 0, 0) },
                    [ordered]@{ name = 'face'; parent = 'root'; pivot = @(0, 8, 0) },
                    [ordered]@{ name = 'holder'; parent = 'face'; pivot = @(0, 8, 0) },
                    [ordered]@{ name = 'rightArm'; parent = 'holder'; pivot = @(0, 8, 0) },
                    [ordered]@{ name = 'rightItem'; parent = 'rightArm'; pivot = @(0, 8, 0) }
                )
            })
    })
$pf = "q.property('lepton:face')"
$pk = "q.property('lepton:kind')"
# Centre heights in px: on top of the 6 px potato, half way up the sides, flat on the ground below.
$centreY = "($pf == 1 ? 7.4 : ($pf == 0 ? 0.3 : 3.0))"
# yaw of the pose per side (south = 0); up/down use the potato's facing, passed in lepton:yaw
$sideYaw = "($pf == 2 ? 180 : ($pf == 4 ? 90 : ($pf == 5 ? -90 : ($pf == 3 ? 0 : q.property('lepton:yaw') * 90))))"
Write-PackJson (Join-Path $script:RP 'animations/lepton_tiny_potato_item.animation.json') ([ordered]@{
        format_version = '1.8.0'
        animations     = [ordered]@{
            'animation.lepton.tiny_potato_item.pose' = [ordered]@{
                loop  = $true
                bones = [ordered]@{
                    # the entity keeps yaw 0 (model -z = world south): the side turns the whole pose
                    root     = [ordered]@{ position = @(0, "$centreY - 8", 0); rotation = @(0, $sideYaw, 0) }
                    face     = [ordered]@{ rotation = @("($pf == 0 ? -90 : 0)", 0, 0) }
                    # x/y: undo the hand offset of Bedrock's held-item pose so the item sits centred
                    holder   = [ordered]@{ position = @(2, 1, "($pf >= 2 ? -2.6 : 0)"); scale = "($pk == 1 ? 0.25 : ($pk == 2 ? 0.6 : 0.35))" }
                    rightArm = [ordered]@{ rotation = @(0, "($pk == 0 ? 90 : 0)", 0) }
                }
            }
        }
    })
Write-PackJson (Join-Path $script:RP 'entity/lepton_tiny_potato_item.entity.json') ([ordered]@{
        format_version            = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier         = 'lepton:tiny_potato_item'
                materials          = [ordered]@{ default = 'entity_alphatest' }
                textures           = [ordered]@{ default = 'textures/blocks/lepton_gadgets/tiny_potato_default' }
                geometry           = [ordered]@{ default = 'geometry.lepton.tiny_potato_item' }
                animations         = [ordered]@{ pose = 'animation.lepton.tiny_potato_item.pose' }
                scripts            = [ordered]@{ animate = @('pose') }
                render_controllers = @('controller.render.default')
                enable_attachables = $true
            }
        }
    })
# ---- sounds -----------------------------------------------------------------------------------------
$defs = [ordered]@{}
foreach ($d in 'chatter', 'clock', 'crickets', 'drips', 'fire', 'ocean', 'rain', 'wind') {
    $defs["lepton.ambient.$d"] = [ordered]@{ category = 'record'; min_distance = 0; max_distance = 64
        sounds = @([ordered]@{ name = "sounds/lepton/gadgets/ambient_$d"; stream = $true; volume = 1.0 }) }
}
$defs['lepton.music.endermosh'] = [ordered]@{ category = 'record'; min_distance = 0; max_distance = 64
    sounds = @([ordered]@{ name = 'sounds/lepton/gadgets/endermosh'; stream = $true; volume = 0.6 }) }
$defs['lepton.music.endermosh_fight'] = [ordered]@{ category = 'music'
    sounds = @([ordered]@{ name = 'sounds/lepton/gadgets/endermosh'; stream = $true; volume = 0.6 }) }
foreach ($p in 'do_it', 'soda', 'yung') {
    $defs["lepton.block.potato.$p"] = [ordered]@{ category = 'block'; sounds = @([ordered]@{ name = "sounds/lepton/gadgets/potato_$p"; volume = 1.0 }) }
}
Write-PackJson (Join-Path $script:RP 'sounds.d/gadgets.json') $defs

Complete-PackUnit
