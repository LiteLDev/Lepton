# U9 variantcontainers: variant chests / trapped chests and the deepslate / blackstone furnaces.
#
#   pwsh -NoProfile -File tools/pack/variantcontainers.ps1      # regenerates and overwrites the committed output
#
# Chests are block geometry (no block entity renderer for custom blocks): body + lid + latch, the
# lid in 5 frames (0, 22.5, 45, 67.5, 90 degrees, Bedrock block geometry allows 22.5 degree steps),
# picked with bone visibility on `lepton:lid`. The model is built for `minecraft:cardinal_direction`
# north (the placer looked north, the front faces south) and rotated for the other directions.
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/variantcontainers.ps1"

Start-PackUnit 'variantcontainers'

foreach ($f in Invoke-VariantContainerTextures) { [void](Add-PackFile $f) }

# Lid rotation sign about the hinge (x axis): positive lifts the front edge in Bedrock geometry.
$LidSign = 1
$LidAngles = @(0, 22.5, 45, 67.5, 90)

# ---- chest geometry -------------------------------------------------------------------------------

# UV of a sub-rectangle of atlas region $R: columns [$U0, $U0 + $W) of the region, full height.
function Get-Uv($R, [double]$U0 = 0, [double]$W = -1) {
    if ($W -lt 0) { $W = $R[2] }
    return [ordered]@{ uv = @((Get-Num ($R[0] + $U0)), (Get-Num $R[1])); uv_size = @((Get-Num $W), (Get-Num $R[3])) }
}

# A cube from a world-aligned box (block pixels, x east, z south) with explicit per-face UVs
# ($Faces: world face -> uv). Geometry x is mirrored (lib.ps1): the world east face is the
# model's "west" face.
function New-ChestCube($Box, $Faces, $Pivot = $null, [double]$RotX = 0) {
    $w = $Box.X1 - $Box.X0; $h = $Box.Y1 - $Box.Y0; $d = $Box.Z1 - $Box.Z0
    $uv = [ordered]@{
        north = $Faces.north; east = $Faces.west; south = $Faces.south; west = $Faces.east
        up = $Faces.up; down = $Faces.down
    }
    $cube = [ordered]@{
        origin = @((Get-Num (8 - $Box.X1)), (Get-Num $Box.Y0), (Get-Num ($Box.Z0 - 8)))
        size   = @((Get-Num $w), (Get-Num $h), (Get-Num $d))
        uv     = $uv
    }
    if ($Pivot) {
        $cube.pivot = @($Pivot | ForEach-Object { Get-Num $_ })
        $cube.rotation = @((Get-Num $RotX), 0, 0)
    }
    return $cube
}

# Faces of one chest part. $Kind: single | west (the double chest's west half = Java type right)
# | east (Java type left). $Part: body | lid.
function Get-PartFaces([string]$Kind, [string]$Part) {
    $A = $script:ChestAtlas
    $P = if ($Part -eq 'body') { 'BODY' } else { 'LID' }
    $side = Get-Uv $A["S_${P}_SIDE"]
    if ($Kind -eq 'single') {
        $top = if ($Part -eq 'body') { $A.S_FLOOR } else { $A.S_LID_TOP }
        $bottom = if ($Part -eq 'body') { $A.S_BOTTOM } else { $A.S_LID_UNDER }
        return [ordered]@{
            south = Get-Uv $A["S_${P}_FRONT"]; north = Get-Uv $A["S_${P}_BACK"]; east = $side; west = $side
            up = Get-Uv $top; down = Get-Uv $bottom
        }
    }
    # composites are 30 wide: the west half is columns 0..15 seen from the front / above / below,
    # 15..30 seen from behind
    $front = if ($Kind -eq 'west') { 0 } else { 15 }
    $back = if ($Kind -eq 'west') { 15 } else { 0 }
    $top = if ($Part -eq 'body') { $A.D_FLOOR } else { $A.D_LID_TOP }
    $bottom = if ($Part -eq 'body') { $A.D_BOTTOM } else { $A.D_LID_UNDER }
    return [ordered]@{
        south = Get-Uv $A["D_${P}_FRONT"] $front 15; north = Get-Uv $A["D_${P}_BACK"] $back 15
        east = $side; west = $side
        up = Get-Uv $top $front 15; down = Get-Uv $bottom $front 15
    }
}

function Get-LatchFaces([string]$Kind) {
    $A = $script:ChestAtlas
    $u0 = 0; $w = 2
    if ($Kind -eq 'west') { $u0 = 0; $w = 1 }      # the west half shows the latch's left column
    if ($Kind -eq 'east') { $u0 = 1; $w = 1 }
    $bu0 = if ($Kind -eq 'west') { 1 } elseif ($Kind -eq 'east') { 0 } else { 0 }
    return [ordered]@{
        south = Get-Uv $A.L_FRONT $u0 $w; north = Get-Uv $A.L_BACK $bu0 $w
        east = Get-Uv $A.L_SIDE; west = Get-Uv $A.L_SIDE
        up = Get-Uv $A.L_TOP $u0 $w; down = Get-Uv $A.L_BOTTOM $u0 $w
    }
}

# x extent of the chest per kind: single 1..15, west half 1..16, east half 0..15.
function Get-XRange([string]$Kind) {
    switch ($Kind) { 'single' { return @(1, 15) } 'west' { return @(1, 16) } 'east' { return @(0, 15) } }
}
function Get-LatchX([string]$Kind) {
    switch ($Kind) { 'single' { return @(7, 9) } 'west' { return @(15, 16) } 'east' { return @(0, 1) } }
}

function Write-ChestGeometry([string]$Kind) {
    $x = Get-XRange $Kind; $lx = Get-LatchX $Kind
    $bones = [System.Collections.Generic.List[object]]::new()
    $bones.Add([ordered]@{ name = 'body'; pivot = @(0, 0, 0); cubes = @(New-ChestCube (New-Box $x[0] 0 1 $x[1] 10 15) (Get-PartFaces $Kind 'body')) })
    $pivot = @(0, 10, -7)    # hinge: top of the body, back edge (z = 1 -> -7 in model space)
    for ($i = 0; $i -lt $LidAngles.Count; $i++) {
        $rot = $LidSign * $LidAngles[$i]
        $bones.Add([ordered]@{
                name  = "lid_$i"
                pivot = @(0, 0, 0)
                cubes = @(
                    (New-ChestCube (New-Box $x[0] 10 1 $x[1] 14 15) (Get-PartFaces $Kind 'lid') $pivot $rot),
                    (New-ChestCube (New-Box $lx[0] 7 15 $lx[1] 11 16) (Get-LatchFaces $Kind) $pivot $rot)
                )
            })
    }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @(
            [ordered]@{
                description = [ordered]@{
                    identifier            = "geometry.lepton.variantcontainers.chest_$Kind"
                    texture_width         = $script:ChestAtlas.Width
                    texture_height        = $script:ChestAtlas.Height
                    visible_bounds_width  = 3
                    visible_bounds_height = 3
                    visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($bones)
            }
        )
    }
    Write-PackJson -Path (Join-Path $script:RP "models/blocks/variantcontainers_chest_$Kind.geo.json") -Object $geo
    return "geometry.lepton.variantcontainers.chest_$Kind"
}

$geoOf = [ordered]@{ single = (Write-ChestGeometry 'single'); left = (Write-ChestGeometry 'east'); right = (Write-ChestGeometry 'west') }
# Java ChestType for cardinal north (front south): left = partner clockwise of the front = west, so
# the chest is the pair's east half; right = the west half.
$xOf = @{ single = (Get-XRange 'single'); left = (Get-XRange 'east'); right = (Get-XRange 'west') }

# ---- chest blocks ---------------------------------------------------------------------------------

$Woods = [ordered]@{
    oak      = @{ En = 'Oak'; Zh = '橡木'; Planks = 'minecraft:oak_planks'; Sound = 'wood'; Map = '#8f7748'; Logs = @('oak_log', 'oak_wood', 'stripped_oak_log', 'stripped_oak_wood') }
    spruce   = @{ En = 'Spruce'; Zh = '云杉木'; Planks = 'minecraft:spruce_planks'; Sound = 'wood'; Map = '#815631'; Logs = @('spruce_log', 'spruce_wood', 'stripped_spruce_log', 'stripped_spruce_wood') }
    birch    = @{ En = 'Birch'; Zh = '白桦木'; Planks = 'minecraft:birch_planks'; Sound = 'wood'; Map = '#f7e9a3'; Logs = @('birch_log', 'birch_wood', 'stripped_birch_log', 'stripped_birch_wood') }
    jungle   = @{ En = 'Jungle'; Zh = '丛林木'; Planks = 'minecraft:jungle_planks'; Sound = 'wood'; Map = '#976d4d'; Logs = @('jungle_log', 'jungle_wood', 'stripped_jungle_log', 'stripped_jungle_wood') }
    acacia   = @{ En = 'Acacia'; Zh = '金合欢木'; Planks = 'minecraft:acacia_planks'; Sound = 'wood'; Map = '#d87f33'; Logs = @('acacia_log', 'acacia_wood', 'stripped_acacia_log', 'stripped_acacia_wood') }
    dark_oak = @{ En = 'Dark Oak'; Zh = '深色橡木'; Planks = 'minecraft:dark_oak_planks'; Sound = 'wood'; Map = '#664c33'; Logs = @('dark_oak_log', 'dark_oak_wood', 'stripped_dark_oak_log', 'stripped_dark_oak_wood') }
    mangrove = @{ En = 'Mangrove'; Zh = '红树木'; Planks = 'minecraft:mangrove_planks'; Sound = 'wood'; Map = '#993333'; Logs = @('mangrove_log', 'mangrove_wood', 'stripped_mangrove_log', 'stripped_mangrove_wood') }
    bamboo   = @{ En = 'Bamboo'; Zh = '竹'; Planks = 'minecraft:bamboo_planks'; Sound = 'bamboo_wood'; Map = '#e5e533'; Logs = @('bamboo_block', 'stripped_bamboo_block') }
    cherry   = @{ En = 'Cherry'; Zh = '樱花木'; Planks = 'minecraft:cherry_planks'; Sound = 'cherry_wood'; Map = '#d1b1a1'; Logs = @('cherry_log', 'cherry_wood', 'stripped_cherry_log', 'stripped_cherry_wood') }
    crimson  = @{ En = 'Crimson'; Zh = '绯红木'; Planks = 'minecraft:crimson_planks'; Sound = 'nether_wood'; Map = '#943f61'; Logs = @('crimson_stem', 'crimson_hyphae', 'stripped_crimson_stem', 'stripped_crimson_hyphae') }
    warped   = @{ En = 'Warped'; Zh = '诡异木'; Planks = 'minecraft:warped_planks'; Sound = 'nether_wood'; Map = '#3a8e8c'; Logs = @('warped_stem', 'warped_hyphae', 'stripped_warped_stem', 'stripped_warped_hyphae') }
}
$Stones = [ordered]@{
    nether_brick = @{ En = 'Nether Brick'; Zh = '下界砖'; Planks = 'minecraft:nether_brick'; Sound = 'nether_brick'; Map = '#700200'; Hardness = 2.0 }
    purpur       = @{ En = 'Purpur'; Zh = '紫珀'; Planks = 'minecraft:purpur_block'; Sound = 'stone'; Map = '#b27fb2'; Hardness = 1.5 }
    prismarine   = @{ En = 'Prismarine'; Zh = '海晶石'; Planks = 'minecraft:prismarine'; Sound = 'stone'; Map = '#4c7f99'; Hardness = 1.5 }
}

function Write-ChestBlock([string]$Mat, $Info, [bool]$Wooden, [bool]$Trapped) {
    $id = if ($Trapped) { "lepton:trapped_${Mat}_chest" } else { "lepton:${Mat}_chest" }
    $path = Get-Path $id
    $tex = Add-TerrainTexture "textures/blocks/variantcontainers/$path"
    $hard = if ($Wooden) { 2.5 } else { $Info.Hardness }
    $res = if ($Wooden) { 2.5 } else { 6 }
    $tool = if ($Wooden) { 'axe' } else { 'pickaxe' }
    $c = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = $hard }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = $res }
        'minecraft:map_color'                 = $Info.Map
        'minecraft:geometry'                  = [ordered]@{ identifier = $geoOf.single; bone_visibility = [ordered]@{} }
        'minecraft:material_instances'        = [ordered]@{ '*' = [ordered]@{ texture = $tex; render_method = 'alpha_test' } }
        'minecraft:light_dampening'           = 0
        'minecraft:movable'                   = [ordered]@{ movement_type = 'immovable' }
        'minecraft:redstone_conductivity'     = [ordered]@{ redstone_conductor = $false; allows_wire_to_step_down = $true }
        'minecraft:loot'                      = (Write-LootTable $path $id)
    }
    for ($i = 0; $i -lt $LidAngles.Count; $i++) { $c['minecraft:geometry'].bone_visibility["lid_$i"] = "q.block_state('lepton:lid') == $i" }
    $c['minecraft:geometry'].bone_visibility.body = $true
    $tags = @($script:ToolTags[$tool], $(if ($Trapped) { 'lepton:variant_trapped_chest' } else { 'lepton:variant_chest' }))
    if ($Wooden) { $tags += 'lepton:wooden_chest'; $tags += 'wood' } else { $tags += 'stone' }
    foreach ($t in ($tags | Sort-Object -Unique)) { $c["tag:$t"] = [ordered]@{} }

    $states = [ordered]@{ 'lepton:type' = @('single', 'left', 'right'); 'lepton:lid' = @(0, 1, 2, 3, 4) }
    if ($Trapped) { $states['lepton:power'] = @(0..15) }
    $traits = [ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 } }

    $perms = [System.Collections.Generic.List[object]]::new()
    # facing: the model's front faces south for a north-looking placer
    $rotations = [ordered]@{ north = 0; west = 90; south = 180; east = -90 }
    foreach ($kv in $rotations.GetEnumerator()) {
        $perms.Add([ordered]@{
                condition  = Get-StateCond 'minecraft:cardinal_direction' $kv.Key
                components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, $kv.Value, 0) } }
            })
    }
    foreach ($type in 'single', 'left', 'right') {
        $x = $xOf[$type]
        $box = ConvertTo-BoxJson (New-Box $x[0] 0 1 $x[1] 14 15)
        $geo = [ordered]@{ identifier = $geoOf[$type]; bone_visibility = $c['minecraft:geometry'].bone_visibility }
        $perms.Add([ordered]@{
                condition  = Get-StateCond 'lepton:type' $type
                components = [ordered]@{ 'minecraft:geometry' = $geo; 'minecraft:collision_box' = $box; 'minecraft:selection_box' = $box }
            })
    }
    if ($Trapped) {
        # Java TrappedChestBlock: weak power on every side = viewers (max 15), strong power downwards.
        # Power 0 keeps a (silent) producer, so the circuit component exists in every state; the
        # module sets its strength natively whenever the viewers change.
        for ($p = 0; $p -le 15; $p++) {
            $perms.Add([ordered]@{
                    condition  = "q.block_state('lepton:power') == $p"
                    components = [ordered]@{ 'minecraft:redstone_producer' = [ordered]@{ power = $p; strongly_powered_face = 'down' } }
                })
        }
    }
    Write-BlockJson -Id $id -Menu ([ordered]@{ category = 'items' }) -States $states -Traits $traits -Components $c -Permutations $perms
    $kindEn = if ($Trapped) { 'Trapped Chest' } else { 'Chest' }
    $kindZh = if ($Trapped) { '陷阱箱' } else { '箱子' }
    Register-BlockClient $id $Info.Sound @{ en = "$($Info.En) $kindEn"; zh = "$($Info.Zh)$kindZh" }
    return $id
}

function Write-ChestRecipes([string]$Mat, $Info, [bool]$Wooden) {
    $chest = "lepton:${Mat}_chest"; $trapped = "lepton:trapped_${Mat}_chest"
    Write-PriorityRecipe "${Mat}_chest" @('###', '# #', '###') @{ '#' = $Info.Planks } $chest 1
    Write-ShapelessRecipe "trapped_${Mat}_chest" @($chest, 'minecraft:tripwire_hook') $trapped 1
    if ($Wooden) {
        foreach ($log in $Info.Logs) { Write-PriorityRecipe "${Mat}_chest_wood_$log" @('###', '# #', '###') @{ '#' = "minecraft:$log" } $chest 4 }
        Write-ShapelessRecipe "${Mat}_chest_revert" @($chest) 'minecraft:chest' 1
        Write-ShapelessRecipe "trapped_${Mat}_chest_revert" @($trapped) 'minecraft:trapped_chest' 1
    }
}

# A shaped recipe that wins over a vanilla recipe matching the same grid (the vanilla chest /
# furnace recipes take any planks / stone): Bedrock prefers the lower priority value (vanilla: chest 0,
# furnace -1).
function Write-PriorityRecipe([string]$Name, [string[]]$Pattern, [hashtable]$Key, [string]$Result, [int]$Count) {
    $k = [ordered]@{}
    foreach ($ch in ($Key.Keys | Sort-Object)) { $k[$ch] = [ordered]@{ item = $Key[$ch] } }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version            = $script:RecipeFormat
            'minecraft:recipe_shaped' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @('crafting_table')
                pattern     = @($Pattern)
                key         = $k
                unlock      = @(@($Key.Values | Sort-Object -Unique) | ForEach-Object { [ordered]@{ item = $_ } })
                result      = [ordered]@{ item = $Result; count = $Count }
                priority    = -2
            }
        })
}

foreach ($mat in $Woods.Keys) {
    [void](Write-ChestBlock $mat $Woods[$mat] $true $false)
    [void](Write-ChestBlock $mat $Woods[$mat] $true $true)
    Write-ChestRecipes $mat $Woods[$mat] $true
}
foreach ($mat in $Stones.Keys) {
    [void](Write-ChestBlock $mat $Stones[$mat] $false $false)
    [void](Write-ChestBlock $mat $Stones[$mat] $false $true)
    Write-ChestRecipes $mat $Stones[$mat] $false
}

# ---- furnaces -------------------------------------------------------------------------------------

$Furnaces = [ordered]@{
    deepslate  = @{ En = 'Deepslate Furnace'; Zh = '深板岩熔炉'; Base = 'minecraft:cobbled_deepslate'; Sound = 'deepslate'; Map = '#646464'; Soul = $false; Hardness = 3.0 }
    blackstone = @{ En = 'Blackstone Furnace'; Zh = '黑石熔炉'; Base = 'minecraft:blackstone'; Sound = 'stone'; Map = '#191919'; Soul = $true; Hardness = 1.5 }
}

foreach ($kind in $Furnaces.Keys) {
    $F = $Furnaces[$kind]
    $id = "lepton:${kind}_furnace"
    $t = { param($n) Add-TerrainTexture "textures/blocks/variantcontainers/${kind}_furnace_$n" }
    $side = & $t 'side'; $top = & $t 'top'; $front = & $t 'front'; $frontOn = & $t 'front_on'
    $frontSoul = if ($F.Soul) { & $t 'front_on_soul' } else { $null }
    $mi = { param($f) [ordered]@{ texture = $f; render_method = 'opaque' } }
    $c = [ordered]@{
        # Quark copies the base block's properties (deepslate 3 / 6, blackstone 1.5 / 6)
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = $F.Hardness }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 6 }
        'minecraft:map_color'                 = $F.Map
        'minecraft:geometry'                  = 'minecraft:geometry.full_block'
        'minecraft:material_instances'        = [ordered]@{
            '*' = & $mi $side; up = & $mi $top; down = & $mi $top
            north = & $mi $side; south = & $mi $side; east = & $mi $side; west = & $mi $side
        }
        'minecraft:movable'                   = [ordered]@{ movement_type = 'immovable' }
        'minecraft:loot'                      = (Write-LootTable "${kind}_furnace" $id)
        'tag:minecraft:is_pickaxe_item_destructible' = [ordered]@{}
        'tag:lepton:variant_furnace'          = [ordered]@{}
        'tag:stone'                           = [ordered]@{}
    }
    if ($F.Soul) { $c['tag:lepton:soul_furnace'] = [ordered]@{} }
    $states = [ordered]@{ 'lepton:lit' = @($false, $true) }
    if ($F.Soul) { $states['lepton:soul'] = @($false, $true) }
    $traits = [ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 } }
    $perms = [System.Collections.Generic.List[object]]::new()
    $opposite = @{ north = 'south'; south = 'north'; east = 'west'; west = 'east' }
    $fires = @(@{ Lit = $false; Soul = $false; Tex = $front })
    $fires += @{ Lit = $true; Soul = $false; Tex = $frontOn }
    if ($F.Soul) { $fires += @{ Lit = $true; Soul = $true; Tex = $frontSoul } }
    foreach ($dir in 'north', 'south', 'east', 'west') {
        foreach ($fire in $fires) {
            $cond = @((Get-StateCond 'minecraft:cardinal_direction' $dir), (Get-StateCond 'lepton:lit' $fire.Lit))
            if ($F.Soul -and $fire.Lit) { $cond += Get-StateCond 'lepton:soul' $fire.Soul }
            $m = [ordered]@{
                '*' = & $mi $side; up = & $mi $top; down = & $mi $top
                north = & $mi $side; south = & $mi $side; east = & $mi $side; west = & $mi $side
            }
            $m[$opposite[$dir]] = & $mi $fire.Tex          # the front faces the placer
            $pc = [ordered]@{ 'minecraft:material_instances' = $m }
            if ($fire.Lit) { $pc['minecraft:light_emission'] = 13 }
            $perms.Add([ordered]@{ condition = Join-And $cond; components = $pc })
        }
    }
    Write-BlockJson -Id $id -Menu ([ordered]@{ category = 'items' }) -States $states -Traits $traits -Components $c -Permutations $perms
    Register-BlockClient $id $F.Sound @{ en = $F.En; zh = $F.Zh }

    Write-PriorityRecipe "${kind}_furnace" @('###', '# #', '###') @{ '#' = $F.Base } $id 1
    Write-ShapedRecipe "${kind}_blast_furnace" @('III', 'IXI', '###') @{ I = 'minecraft:iron_ingot'; X = $id; '#' = 'minecraft:smooth_stone' } 'minecraft:blast_furnace' 1
    # the smoker takes any log (Java #minecraft:logs); Bedrock's item tag is minecraft:logs
    Write-PackJson (Join-Path $script:BP "recipes/${kind}_smoker.json") ([ordered]@{
            format_version            = $script:RecipeFormat
            'minecraft:recipe_shaped' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:${kind}_smoker" }
                tags        = @('crafting_table')
                pattern     = @(' # ', '#X#', ' # ')
                key         = [ordered]@{ '#' = [ordered]@{ tag = 'minecraft:logs' }; X = [ordered]@{ item = $id } }
                unlock      = @([ordered]@{ item = $id })
                result      = [ordered]@{ item = 'minecraft:smoker'; count = 1 }
            }
        })
}

Complete-PackUnit
