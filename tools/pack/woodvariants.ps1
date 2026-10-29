# U6 woodvariants: vertical planks, variant bookshelves, variant ladders, wooden posts, hedges, leaf
# carpets and hollow logs of the vanilla woods (docs/spec/woodvariants.md).
#
#   pwsh -NoProfile -File tools/pack/woodvariants.ps1     # regenerates and overwrites the committed output
#
# Other units (U7 newwoods) dot-source this file to reuse the generators for their own woods:
#
#   . "$PSScriptRoot/woodvariants.ps1"                   # defines the New-Wv* functions only
#   New-WvPost -Id 'lepton:azalea_post' -SideTexture 'textures/blocks/azalea_log' -Name @{ en = 'Azalea Post'; zh = '杜鹃木杆' } -Sound wood
#
# Every generated block carries the tags of docs/spec/woodvariants.md §2.5, so the native behaviour in
# src/feature/woodvariants applies to it.

. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/woodvariants.ps1"

$script:WvGeoFormat = '1.21.0'

# ------------------------------------------------------------------------------------------------
# Geometry helpers (world-space models; faces whose texture grain must follow a horizontal axis
# are rotated with uv_rotation)
# ------------------------------------------------------------------------------------------------

# A cube for $Box with world-aligned UVs (lib New-GeoCube), then per-face overrides:
#   $Mat:  face -> material instance name ('*' = every face)
#   $Rot:  list of faces whose texture is turned 90 degrees (grain along the face's horizontal axis)
#   $Only: keep only these faces
function New-WvCube($Box, [hashtable]$Mat = @{}, [string[]]$Rot = @(), [string[]]$Only = @()) {
    $c = New-GeoCube $Box
    $size = @{
        north = @(($Box.X1 - $Box.X0), ($Box.Y1 - $Box.Y0)); south = @(($Box.X1 - $Box.X0), ($Box.Y1 - $Box.Y0))
        east = @(($Box.Z1 - $Box.Z0), ($Box.Y1 - $Box.Y0)); west = @(($Box.Z1 - $Box.Z0), ($Box.Y1 - $Box.Y0))
        up = @(($Box.X1 - $Box.X0), ($Box.Z1 - $Box.Z0)); down = @(($Box.X1 - $Box.X0), ($Box.Z1 - $Box.Z0))
    }
    # the across-axis start of each face (where a rotated strip is sampled)
    $across = @{ north = $Box.Y0; south = $Box.Y0; east = $Box.Y0; west = $Box.Y0; up = $Box.Z0; down = $Box.Z0 }
    $faces = [ordered]@{}
    foreach ($f in @($c.uv.Keys)) {
        if ($Only.Count -and $Only -notcontains $f) { continue }
        $face = $c.uv[$f]
        $m = if ($Mat.ContainsKey($f)) { $Mat[$f] } elseif ($Mat.ContainsKey('*')) { $Mat['*'] } else { $f }
        $face.material_instance = $m
        if ($Rot -contains $f) {
            $len = $size[$f][0]; $wid = $size[$f][1]
            $face.uv = @((Get-Num $across[$f]), 0)
            $face.uv_size = @((Get-Num $wid), (Get-Num $len))
            $face.uv_rotation = 90
        }
        $faces[$f] = $face
    }
    $c.uv = $faces
    return $c
}

# Writes models/blocks/lepton_wv_<name>.geo.json (recorded for the unit). $Bones: name -> cubes.
function Write-WvGeometry([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) { [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($kv.Value) } }
    $geo = [ordered]@{
        format_version       = $script:WvGeoFormat
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier = "geometry.lepton.wv_$Name"; texture_width = 16; texture_height = 16
                    visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($bonesJson)
            })
    }
    Write-PackJson -Path (Join-Path $script:RP "models/blocks/lepton_wv_$Name.geo.json") -Object $geo
    return "geometry.lepton.wv_$Name"
}

function Get-Mat([string]$Texture, [string]$Render = 'opaque', [string]$Tint) {
    $m = [ordered]@{ texture = (Add-TerrainTexture $Texture); render_method = $Render }
    if ($Tint -and $Tint -ne 'none') { $m.tint_method = $Tint }
    return $m
}

$script:Axes = @('x', 'y', 'z')
# block_face (clicked face) -> axis
function Get-AxisCond([string]$Axis) {
    switch ($Axis) {
        'y' { Join-Or @((Get-StateCond 'minecraft:block_face' 'up'), (Get-StateCond 'minecraft:block_face' 'down')) }
        'x' { Join-Or @((Get-StateCond 'minecraft:block_face' 'east'), (Get-StateCond 'minecraft:block_face' 'west')) }
        'z' { Join-Or @((Get-StateCond 'minecraft:block_face' 'north'), (Get-StateCond 'minecraft:block_face' 'south')) }
    }
}
$script:BlockFaceTrait = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
$script:Waterloggable = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }

function Get-WvBase([double]$Hardness, [double]$Resistance, [string]$MapColor, [bool]$Flammable, [string[]]$Tags, [string]$Tool = 'axe') {
    $c = Get-BaseComponents @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags }
    return $c
}

function Write-WvRecipe([string]$Name, [string[]]$Pattern, [hashtable]$Key, [string]$Result, [int]$Count, [int]$Priority = 0) {
    $k = [ordered]@{}
    foreach ($ch in ($Key.Keys | Sort-Object)) {
        $v = $Key[$ch]
        $k[$ch] = if ($v.StartsWith('#')) { [ordered]@{ tag = $v.Substring(1) } } else { [ordered]@{ item = $v } }
    }
    $r = [ordered]@{
        description = [ordered]@{ identifier = "lepton:$Name" }
        tags        = @('crafting_table')
        pattern     = @($Pattern)
        key         = $k
        unlock      = @(@($Key.Values | Where-Object { -not $_.StartsWith('#') } | Sort-Object -Unique) | ForEach-Object { [ordered]@{ item = $_ } })
        result      = [ordered]@{ item = $Result; count = $Count }
    }
    if ($Priority) { $r.priority = $Priority }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{ format_version = $script:RecipeFormat; 'minecraft:recipe_shaped' = $r })
}

# ------------------------------------------------------------------------------------------------
# Generators (one block each). -ModuleTag: lepton:wv_<module> (config gate, docs §2.5).
# ------------------------------------------------------------------------------------------------

function New-WvVerticalPlanks {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [Parameter(Mandatory)][string]$Planks, [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable)
    Assert-PackUnit
    New-FullBlock -Id $Id -Textures $Texture -Name $Name -Sound $Sound -Hardness 2 -Resistance 3 -MapColor $MapColor -Flammable:(-not $NonFlammable) `
        -Tool axe -Tags @('lepton:vertical_planks', 'lepton:wv_vertical_planks') -Group 'minecraft:itemGroup.name.planks'
    $p = Get-Path $Id
    Write-ShapedRecipe $p @('#', '#', '#') @{ '#' = $Planks } $Id 3
    Write-ShapelessRecipe "${p}_revert" @($Id) $Planks 1
}

function New-WvBookshelf {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$SideTexture, [Parameter(Mandatory)][string]$TopTexture,
        [Parameter(Mandatory)][hashtable]$Name, [Parameter(Mandatory)][string]$Planks, [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable)
    Assert-PackUnit
    $p = Get-Path $Id
    $loot = "loot_tables/blocks/$p.json"
    Write-PackJson (Join-Path $script:BP $loot) ([ordered]@{ pools = @([ordered]@{ rolls = 1; entries = @(
                    [ordered]@{ type = 'item'; name = $Id; weight = 1; conditions = @([ordered]@{ condition = 'match_tool'; enchantments = @([ordered]@{ enchantment = 'silk_touch'; levels = [ordered]@{ range_min = 1 } }) }) }
                ) }, [ordered]@{ rolls = 1; conditions = @([ordered]@{ condition = 'match_tool'; enchantments = @([ordered]@{ enchantment = 'silk_touch'; levels = [ordered]@{ range_max = 0 } }) })
                entries = @([ordered]@{ type = 'item'; name = 'minecraft:book'; weight = 1; functions = @([ordered]@{ function = 'set_count'; count = 3 }, [ordered]@{ function = 'explosion_decay' }) }) }) })
    New-FullBlock -Id $Id -Textures @{ side = $SideTexture; top = $TopTexture } -Name $Name -Sound $Sound -Hardness 1.5 -Resistance 1.5 -MapColor $MapColor `
        -Flammable:(-not $NonFlammable) -Tool axe -Tags @('lepton:bookshelf', 'lepton:wv_variant_bookshelves') -Category 'items' -Loot $loot
    Write-WvRecipe $p @('###', 'XXX', '###') @{ '#' = $Planks; 'X' = 'minecraft:book' } $Id 1 -1
    # Quark: the lectern takes any bookshelf
    Write-WvRecipe "lectern_from_$p" @('SSS', ' B ', ' S ') @{ 'S' = '#minecraft:wooden_slabs'; 'B' = $Id } 'minecraft:lectern' 1
}

function New-WvLadder {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [Parameter(Mandatory)][string]$Planks, [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable)
    Assert-PackUnit
    $geo = Write-LadderGeometry
    $c = Get-WvBase 0.4 0.4 $MapColor (-not $NonFlammable) @('lepton:ladder', 'lepton:wv_variant_ladders')
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:liquid_detection'] = $script:Waterloggable
    $c['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('side') }) }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Get-Mat $Texture 'alpha_test') }
    $vis = [ordered]@{}
    foreach ($d in 'north', 'south', 'west', 'east') { $vis[$d] = Get-StateCond 'minecraft:block_face' $d }
    $c['minecraft:geometry'] = [ordered]@{ identifier = $geo; bone_visibility = $vis }
    $p = Get-Path $Id
    $c['minecraft:loot'] = Write-LootTable $p $Id
    $perms = foreach ($d in 'north', 'south', 'west', 'east') {
        $b = $script:LadderBoxes[$d]
        [ordered]@{ condition = (Get-StateCond 'minecraft:block_face' $d); components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $b; 'minecraft:selection_box' = ConvertTo-BoxJson $b } }
    }
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'items' }) -Traits $script:BlockFaceTrait -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
    # a flat item icon like the vanilla ladder
    Add-PackText "item.$Id.name" $Name
    $icon = Add-ItemTexture $Texture
    Write-PackJson (Join-Path $script:BP "items/$p.json") ([ordered]@{
            format_version = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $Id; menu_category = [ordered]@{ category = 'items' } }
                components  = [ordered]@{ 'minecraft:icon' = $icon; 'minecraft:block_placer' = [ordered]@{ block = $Id; replace_block_item = $true }; 'minecraft:max_stack_size' = 64 }
            }
        })
    Write-WvRecipe $p @('# #', '#W#', '# #') @{ '#' = 'minecraft:stick'; 'W' = $Planks } $Id 4 -1
}

# Java LadderBlock: facing = the clicked face; the ladder lies against the opposite side.
$script:LadderBoxes = @{
    north = New-Box 0 0 13 16 16 16; south = New-Box 0 0 0 16 16 3
    west = New-Box 13 0 0 16 16 16; east = New-Box 0 0 0 3 16 16
}
function Write-LadderGeometry {
    $planes = @{
        north = @{ box = (New-Box 0 0 15.2 16 16 15.2); faces = @('north', 'south') }
        south = @{ box = (New-Box 0 0 0.8 16 16 0.8); faces = @('north', 'south') }
        west  = @{ box = (New-Box 15.2 0 0 15.2 16 16); faces = @('east', 'west') }
        east  = @{ box = (New-Box 0.8 0 0 0.8 16 16); faces = @('east', 'west') }
    }
    $bones = [ordered]@{}
    foreach ($d in 'north', 'south', 'west', 'east') { $bones[$d] = @(New-WvCube $planes[$d].box @{ '*' = '*' } @() $planes[$d].faces) }
    return Write-WvGeometry 'ladder' $bones
}

# Post: a 4x4 beam along the axis, connection stubs (other_post) and small chains on the four other sides.
$script:PostDirs = [ordered]@{
    down = @{ axis = 'y'; box = (New-Box 6 0 6 10 6 10) }; up = @{ axis = 'y'; box = (New-Box 6 10 6 10 16 10) }
    north = @{ axis = 'z'; box = (New-Box 6 6 0 10 10 6) }; south = @{ axis = 'z'; box = (New-Box 6 6 10 10 10 16) }
    west = @{ axis = 'x'; box = (New-Box 0 6 6 6 10 10) }; east = @{ axis = 'x'; box = (New-Box 10 6 6 16 10 10) }
}
$script:PostBeams = @{ x = (New-Box 0 6 6 16 10 10); y = (New-Box 6 0 6 10 16 10); z = (New-Box 6 6 0 10 10 16) }
# faces to rotate so the grain follows the piece's axis
$script:GrainRot = @{ x = @('north', 'south', 'up', 'down'); y = @(); z = @('east', 'west') }

function New-ChainPlanes([string]$Dir) {
    $b = $script:PostDirs[$Dir].box
    $axis = $script:PostDirs[$Dir].axis
    $cubes = @()
    foreach ($k in 0, 1) {
        switch ($axis) {
            'y' { $box = if ($k) { New-Box 8 $b.Y0 6.5 8 $b.Y1 9.5 } else { New-Box 6.5 $b.Y0 8 9.5 $b.Y1 8 }; $faces = if ($k) { @('east', 'west') } else { @('north', 'south') }; $rot = @(0, 45, 0) }
            'z' { $box = if ($k) { New-Box 8 6.5 $b.Z0 8 9.5 $b.Z1 } else { New-Box 6.5 8 $b.Z0 9.5 8 $b.Z1 }; $faces = if ($k) { @('east', 'west') } else { @('up', 'down') }; $rot = @(0, 0, 45) }
            'x' { $box = if ($k) { New-Box $b.X0 8 6.5 $b.X1 8 9.5 } else { New-Box $b.X0 6.5 8 $b.X1 9.5 8 }; $faces = if ($k) { @('up', 'down') } else { @('north', 'south') }; $rot = @(45, 0, 0) }
        }
        $cube = New-WvCube $box @{ '*' = 'chain' } $(if ($axis -eq 'y') { @() } else { $faces }) $faces
        # sample the link strips (columns 0..2 / 3..5)
        foreach ($f in @($cube.uv.Keys)) {
            $face = $cube.uv[$f]
            $len = if ($axis -eq 'y') { $b.Y1 - $b.Y0 } elseif ($axis -eq 'x') { $b.X1 - $b.X0 } else { $b.Z1 - $b.Z0 }
            $face.uv = @((3 * $k), 0)
            $face.uv_size = @(3, (Get-Num $len))
        }
        $cube.pivot = @(0, 8, 0)
        $cube.rotation = $rot
        $cubes += $cube
    }
    return $cubes
}

function Write-PostGeometry {
    $bones = [ordered]@{}
    foreach ($a in $script:Axes) { $bones["beam_$a"] = @(New-WvCube $script:PostBeams[$a] @{ '*' = 'side' } $script:GrainRot[$a]) }
    foreach ($d in $script:PostDirs.Keys) {
        $a = $script:PostDirs[$d].axis
        $bones["post_$d"] = @(New-WvCube $script:PostDirs[$d].box @{ '*' = 'side' } $script:GrainRot[$a])
        $bones["chain_$d"] = @(New-ChainPlanes $d)
    }
    return Write-WvGeometry 'post' $bones
}

function New-WvPost {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$SideTexture, [Parameter(Mandatory)][hashtable]$Name,
        [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable, [switch]$Strippable, [string]$ChainTexture = 'textures/blocks/woodvariants/post_chain')
    Assert-PackUnit
    $geo = Write-PostGeometry
    $tags = @('lepton:post', 'lepton:wv_wooden_posts')
    if ($Strippable) { $tags += 'lepton:strippable' }
    $c = Get-WvBase 2 3 $MapColor (-not $NonFlammable) $tags
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:liquid_detection'] = $script:Waterloggable
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Get-Mat $SideTexture); side = (Get-Mat $SideTexture); chain = (Get-Mat $ChainTexture 'alpha_test') }
    $vis = [ordered]@{}
    foreach ($a in $script:Axes) { $vis["beam_$a"] = Get-AxisCond $a }
    foreach ($d in $script:PostDirs.Keys) {
        $vis["post_$d"] = Get-StateCond "lepton:connect_$d" 'other_post'
        $vis["chain_$d"] = Get-StateCond "lepton:connect_$d" 'chain'
    }
    $c['minecraft:geometry'] = [ordered]@{ identifier = $geo; bone_visibility = $vis }
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    $perms = foreach ($a in $script:Axes) {
        $b = $script:PostBeams[$a]
        [ordered]@{ condition = (Get-AxisCond $a); components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $b; 'minecraft:selection_box' = ConvertTo-BoxJson $b } }
    }
    $states = [ordered]@{}
    foreach ($d in $script:PostDirs.Keys) { $states["lepton:connect_$d"] = @('none', 'chain', 'other_post') }
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'construction'; group = 'minecraft:itemGroup.name.wood' }) -States $states -Traits $script:BlockFaceTrait -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
}

# Hedge: log post + leaf body, leaf arms to other hedges; "extend" (a hedge below) replaces the post by leaves.
$script:HedgeArms = @{
    north = New-Box 2 2 0 14 16 2; south = New-Box 2 2 14 14 16 16; west = New-Box 0 2 2 2 16 14; east = New-Box 14 2 2 16 16 14
}
$script:HedgeArmCollision = @{
    north = New-Box 2 1 0 14 16 2; south = New-Box 2 1 14 14 16 16; west = New-Box 0 1 2 2 16 14; east = New-Box 14 1 2 16 16 14
}
function Write-HedgeGeometry {
    $bones = [ordered]@{
        log    = @(New-WvCube (New-Box 6 0 6 10 15 10) @{ '*' = 'log' })
        leaves = @(New-WvCube (New-Box 2 2 2 14 16 14) @{ '*' = 'leaf' })
        extend = @(New-WvCube (New-Box 2 0 2 14 16 14) @{ '*' = 'leaf' })
    }
    foreach ($d in 'north', 'south', 'west', 'east') { $bones[$d] = @(New-WvCube $script:HedgeArms[$d] @{ '*' = 'leaf' }) }
    return Write-WvGeometry 'hedge' $bones
}

function New-WvHedge {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$LogTexture, [Parameter(Mandatory)][string]$LeafTexture,
        [Parameter(Mandatory)][hashtable]$Name, [Parameter(Mandatory)][string]$Leaves, [Parameter(Mandatory)][string[]]$Logs,
        [string]$Tint = 'default_foliage', [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable, [string]$RecipeName)
    Assert-PackUnit
    $geo = Write-HedgeGeometry
    $c = Get-WvBase 2 3 $MapColor (-not $NonFlammable) @('lepton:hedge', 'lepton:wv_hedges', 'dirt', 'minecraft:is_hoe_item_destructible')
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:liquid_detection'] = $script:Waterloggable
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Get-Mat $LeafTexture 'alpha_test' $Tint); leaf = (Get-Mat $LeafTexture 'alpha_test' $Tint); log = (Get-Mat $LogTexture) }
    $ext = Get-StateCond 'lepton:extend' $true
    $vis = [ordered]@{ log = "!$ext"; leaves = "!$ext"; extend = $ext }
    foreach ($d in 'north', 'south', 'west', 'east') { $vis[$d] = Get-StateCond "lepton:$d" $true }
    $c['minecraft:geometry'] = [ordered]@{ identifier = $geo; bone_visibility = $vis }
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    $perms = Get-ConnectionPermutations { param($d) (Get-StateCond "lepton:$d" $false) } { param($d) $script:HedgeArmCollision[$d] } (New-Box 2 0 2 14 16 14)
    $states = [ordered]@{ 'lepton:extend' = @($false, $true) }
    foreach ($d in 'north', 'east', 'south', 'west') { $states["lepton:$d"] = @($false, $true) }
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.leaves' }) -States $states -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
    $base = if ($RecipeName) { $RecipeName } else { Get-Path $Id }
    foreach ($log in $Logs) { Write-ShapedRecipe "${base}_from_$(Get-Path $log)" @('L', 'W') @{ 'L' = $Leaves; 'W' = $log } $Id 2 }
}

function New-WvLeafCarpet {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$LeafTexture, [Parameter(Mandatory)][hashtable]$Name,
        [Parameter(Mandatory)][string]$Leaves, [string]$Tint = 'default_foliage', [string]$MapColor)
    Assert-PackUnit
    if (-not $script:CarpetGeo) {
        $script:CarpetGeo = Write-WvGeometry 'leaf_carpet' ([ordered]@{ carpet = @(New-WvCube (New-Box 0 0 0 16 1 16) @{ '*' = 'leaf' }) })
    }
    $c = Get-WvBase 0 0 $MapColor $true @('lepton:leaf_carpet', 'lepton:wv_leaf_carpet') 'hoe'
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:collision_box'] = $false
    $c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 1 16)
    $c['minecraft:replaceable'] = [ordered]@{}
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Get-Mat $LeafTexture 'alpha_test' $Tint); leaf = (Get-Mat $LeafTexture 'alpha_test' $Tint) }
    $c['minecraft:geometry'] = $script:CarpetGeo
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.leaves' }) -Components $c
    Register-BlockClient $Id 'grass' $Name
    Write-ShapedRecipe (Get-Path $Id) @('##') @{ '#' = $Leaves } $Id 3
}

# Hollow log: four 2 px walls along the axis. Outer faces: log side, inner faces: stripped log side,
# wall ends: log top (world-aligned, so the ring of the log top shows).
$script:HollowWalls = @{
    y = @(
        @{ box = (New-Box 0 0 0 16 16 2); inner = 'south' }, @{ box = (New-Box 0 0 14 16 16 16); inner = 'north' },
        @{ box = (New-Box 0 0 2 2 16 14); inner = 'east' }, @{ box = (New-Box 14 0 2 16 16 14); inner = 'west' })
    x = @(
        @{ box = (New-Box 0 0 0 16 2 16); inner = 'up' }, @{ box = (New-Box 0 14 0 16 16 16); inner = 'down' },
        @{ box = (New-Box 0 2 0 16 14 2); inner = 'south' }, @{ box = (New-Box 0 2 14 16 14 16); inner = 'north' })
    z = @(
        @{ box = (New-Box 0 0 0 16 2 16); inner = 'up' }, @{ box = (New-Box 0 14 0 16 16 16); inner = 'down' },
        @{ box = (New-Box 0 2 0 2 14 16); inner = 'east' }, @{ box = (New-Box 14 2 0 16 14 16); inner = 'west' })
}
$script:AxisEnds = @{ x = @('east', 'west'); y = @('up', 'down'); z = @('north', 'south') }
function Write-HollowGeometry {
    $bones = [ordered]@{}
    foreach ($a in $script:Axes) {
        $bones["axis_$a"] = @(foreach ($w in $script:HollowWalls[$a]) {
                $mat = @{ '*' = 'side' }
                $mat[$w.inner] = 'inside'
                foreach ($e in $script:AxisEnds[$a]) { $mat[$e] = 'end' }
                New-WvCube $w.box $mat $script:GrainRot[$a]
            })
    }
    return Write-WvGeometry 'hollow_log' $bones
}

function New-WvHollowLog {
    param([Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$SideTexture, [Parameter(Mandatory)][string]$TopTexture,
        [Parameter(Mandatory)][string]$InsideTexture, [Parameter(Mandatory)][hashtable]$Name, [Parameter(Mandatory)][string]$Log,
        [string]$Sound = 'wood', [string]$MapColor, [switch]$NonFlammable)
    Assert-PackUnit
    $geo = Write-HollowGeometry
    $c = Get-WvBase 2 2 $MapColor (-not $NonFlammable) @('lepton:hollow_log', 'lepton:wv_hollow_logs', 'log')
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:liquid_detection'] = $script:Waterloggable
    $c['minecraft:material_instances'] = [ordered]@{
        '*' = (Get-Mat $SideTexture); side = (Get-Mat $SideTexture); end = (Get-Mat $TopTexture); inside = (Get-Mat $InsideTexture)
    }
    $vis = [ordered]@{}
    foreach ($a in $script:Axes) { $vis["axis_$a"] = Get-AxisCond $a }
    $c['minecraft:geometry'] = [ordered]@{ identifier = $geo; bone_visibility = $vis }
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    $c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 16 16)
    $perms = foreach ($a in $script:Axes) {
        [ordered]@{ condition = (Get-AxisCond $a); components = [ordered]@{ 'minecraft:collision_box' = (Get-CollisionJson @($script:HollowWalls[$a] | ForEach-Object { $_.box })) } }
    }
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'nature'; group = 'minecraft:itemGroup.name.log' }) -Traits $script:BlockFaceTrait -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
    Write-ShapedRecipe (Get-Path $Id) @(' L ', 'L L', ' L ') @{ 'L' = $Log } $Id 4
}

# ------------------------------------------------------------------------------------------------
# U6: the vanilla woods
# ------------------------------------------------------------------------------------------------

function Invoke-WoodVariantsPack {
    Start-PackUnit 'woodvariants'
    $texDir = Join-Path $script:RP 'textures/blocks/woodvariants'
    foreach ($p in (Write-WoodVariantTextures $texDir)) { $null = Add-PackFile $p }
    $T = 'textures/blocks/'
    $W = 'textures/blocks/woodvariants/'

    # name, zh wood, planks texture, log side/top, stripped side/top, sounds, nether
    $woods = [ordered]@{
        oak      = @{ en = 'Oak'; zh = '橡木'; planks = 'planks_oak'; log = 'log_oak'; top = 'log_oak_top'; slog = 'stripped_oak_log'; stop = 'stripped_oak_log_top'; leaves = 'leaves_oak'; leafEn = 'Oak'; leafZh = '橡树树叶'; tint = 'default_foliage'; color = '#a2834f' }
        spruce   = @{ en = 'Spruce'; zh = '云杉木'; planks = 'planks_spruce'; log = 'log_spruce'; top = 'log_spruce_top'; slog = 'stripped_spruce_log'; stop = 'stripped_spruce_log_top'; leaves = 'leaves_spruce'; leafEn = 'Spruce'; leafZh = '云杉树叶'; tint = 'evergreen_foliage'; color = '#735532' }
        birch    = @{ en = 'Birch'; zh = '白桦木'; planks = 'planks_birch'; log = 'log_birch'; top = 'log_birch_top'; slog = 'stripped_birch_log'; stop = 'stripped_birch_log_top'; leaves = 'leaves_birch'; leafEn = 'Birch'; leafZh = '白桦树叶'; tint = 'birch_foliage'; color = '#c0af79' }
        jungle   = @{ en = 'Jungle'; zh = '丛林木'; planks = 'planks_jungle'; log = 'log_jungle'; top = 'log_jungle_top'; slog = 'stripped_jungle_log'; stop = 'stripped_jungle_log_top'; leaves = 'leaves_jungle'; leafEn = 'Jungle'; leafZh = '丛林树叶'; tint = 'default_foliage'; color = '#a0734d' }
        acacia   = @{ en = 'Acacia'; zh = '金合欢木'; planks = 'planks_acacia'; log = 'log_acacia'; top = 'log_acacia_top'; slog = 'stripped_acacia_log'; stop = 'stripped_acacia_log_top'; leaves = 'leaves_acacia'; leafEn = 'Acacia'; leafZh = '金合欢树叶'; tint = 'default_foliage'; color = '#a95a31' }
        dark_oak = @{ en = 'Dark Oak'; zh = '深色橡木'; planks = 'planks_big_oak'; log = 'log_big_oak'; top = 'log_big_oak_top'; slog = 'stripped_dark_oak_log'; stop = 'stripped_dark_oak_log_top'; leaves = 'leaves_big_oak'; leafEn = 'Dark Oak'; leafZh = '深色橡树树叶'; tint = 'default_foliage'; color = '#43301a' }
        mangrove = @{ en = 'Mangrove'; zh = '红树木'; planks = 'mangrove_planks'; log = 'mangrove_log_side'; top = 'mangrove_log_top'; slog = 'stripped_mangrove_log_side'; stop = 'stripped_mangrove_log_top'; leaves = 'mangrove_leaves'; leafEn = 'Mangrove'; leafZh = '红树'; tint = 'default_foliage'; color = '#773934'; vlog = 'mangrove_log' }
        cherry   = @{ en = 'Cherry'; zh = '樱花木'; planks = 'cherry_planks'; log = 'cherry_log_side'; top = 'cherry_log_top'; slog = 'stripped_cherry_log_side'; stop = 'stripped_cherry_log_top'; leaves = 'cherry_leaves'; leafEn = 'Cherry'; leafZh = '樱花树叶'; tint = 'none'; color = '#e2b2a6'; sound = 'cherry_wood' }
        bamboo   = @{ en = 'Bamboo'; zh = '竹'; planks = 'bamboo_planks'; color = '#d1bb59'; sound = 'bamboo_wood' }
        crimson  = @{ en = 'Crimson'; zh = '绯红木'; planks = 'huge_fungus/crimson_planks'; log = 'huge_fungus/crimson_log_side'; top = 'huge_fungus/crimson_log_top'; slog = 'huge_fungus/stripped_crimson_stem_side'; stop = 'huge_fungus/stripped_crimson_stem_top'; color = '#6b344b'; sound = 'nether_wood'; nether = $true }
        warped   = @{ en = 'Warped'; zh = '诡异木'; planks = 'huge_fungus/warped_planks'; log = 'huge_fungus/warped_stem_side'; top = 'huge_fungus/warped_stem_top'; slog = 'huge_fungus/stripped_warped_stem_side'; stop = 'huge_fungus/stripped_warped_stem_top'; color = '#2b6963'; sound = 'nether_wood'; nether = $true }
    }
    $verticalZh = @{ oak = '竖纹橡木木板'; spruce = '竖纹云杉木板'; birch = '竖纹白桦木板'; jungle = '竖纹丛林木板'; acacia = '竖纹金合欢木板'; dark_oak = '竖纹深色橡木木板'
        mangrove = '竖纹红树木板'; cherry = '竖纹樱花木板'; bamboo = '竖纹竹板'; crimson = '竖纹绯红木板'; warped = '竖纹诡异木板' }
    $hollowZh = @{ oak = '空心橡木原木'; spruce = '空心云杉原木'; birch = '空心白桦原木'; jungle = '空心丛林原木'; acacia = '空心金合欢原木'; dark_oak = '空心深色橡木原木'
        mangrove = '空心红树原木'; cherry = '空心樱花原木'; crimson = '空心绯红菌柄'; warped = '空心诡异菌柄' }

    foreach ($w in $woods.Keys) {
        $d = $woods[$w]
        $nether = [bool]$d.nether
        $sound = if ($d.sound) { $d.sound } else { 'wood' }
        $logSound = if ($nether) { 'stem' } else { $sound }
        $planks = "minecraft:${w}_planks"
        $common = @{ MapColor = $d.color; NonFlammable = $nether }

        # VerticalPlanks (ALL)
        New-WvVerticalPlanks -Id "lepton:vertical_${w}_planks" -Texture "${W}vertical_${w}_planks" -Planks $planks -Sound $sound @common `
            -Name @{ en = "Vertical $($d.en) Planks"; zh = $verticalZh[$w] }

        # VariantBookshelves / VariantLadders (NON_OAK)
        if ($w -ne 'oak') {
            New-WvBookshelf -Id "lepton:${w}_bookshelf" -SideTexture "${W}${w}_bookshelf" -TopTexture "$T$($d.planks)" -Planks $planks -Sound $sound @common `
                -Name @{ en = "$($d.en) Bookshelf"; zh = "$($d.zh)书架" }
            New-WvLadder -Id "lepton:${w}_ladder" -Texture "${W}${w}_ladder" -Planks $planks -Sound $sound @common `
                -Name @{ en = "$($d.en) Ladder"; zh = "$($d.zh)梯子" }
        }

        # WoodenPosts (ALL): from the wood / hyphae / bamboo block
        if ($w -eq 'bamboo') {
            $postTex = "${W}bamboo_post"; $sPostTex = "${W}stripped_bamboo_post"
            $woodItem = 'minecraft:bamboo_block'; $sWoodItem = 'minecraft:stripped_bamboo_block'
        } else {
            $postTex = "$T$($d.log)"; $sPostTex = "$T$($d.slog)"
            $wordWood = if ($nether) { 'hyphae' } else { 'wood' }
            $woodItem = "minecraft:${w}_$wordWood"; $sWoodItem = "minecraft:stripped_${w}_$wordWood"
        }
        New-WvPost -Id "lepton:${w}_post" -SideTexture $postTex -Sound $logSound @common -Strippable -Name @{ en = "$($d.en) Post"; zh = "$($d.zh)杆" }
        New-WvPost -Id "lepton:stripped_${w}_post" -SideTexture $sPostTex -Sound $logSound @common -Name @{ en = "Stripped $($d.en) Post"; zh = "去皮$($d.zh)杆" }
        Write-ShapedRecipe "${w}_post" @('F', 'F', 'F') @{ 'F' = $woodItem } "lepton:${w}_post" 8
        Write-ShapedRecipe "stripped_${w}_post" @('F', 'F', 'F') @{ 'F' = $sWoodItem } "lepton:stripped_${w}_post" 8

        # HollowLogs (ALL_WITH_LOGS)
        if ($w -ne 'bamboo') {
            $word = if ($nether) { 'stem' } else { 'log' }
            $hollowEn = if ($nether) { "Hollow $($d.en) Stem" } else { "Hollow $($d.en) Log" }
            New-WvHollowLog -Id "lepton:hollow_${w}_$word" -SideTexture "$T$($d.log)" -TopTexture "$T$($d.top)" -InsideTexture "$T$($d.slog)" `
                -Log "minecraft:${w}_$word" -Sound $logSound @common -Name @{ en = $hollowEn; zh = $hollowZh[$w] }
        }

        # Hedges / LeafCarpet (OVERWORLD_WITH_TREE)
        if ($d.leaves) {
            $logs = @("minecraft:${w}_log", "minecraft:${w}_wood", "minecraft:stripped_${w}_log", "minecraft:stripped_${w}_wood")
            New-WvHedge -Id "lepton:${w}_hedge" -LogTexture "$T$($d.log)" -LeafTexture "$T$($d.leaves)" -Tint $d.tint -Leaves "minecraft:${w}_leaves" -Logs $logs `
                -Sound $sound -MapColor $d.color -Name @{ en = "$($d.leafEn) Leaf Hedge"; zh = "$($d.leafZh)篱" }
            New-WvLeafCarpet -Id "lepton:${w}_leaf_carpet" -LeafTexture "$T$($d.leaves)" -Tint $d.tint -Leaves "minecraft:${w}_leaves" `
                -MapColor $d.color -Name @{ en = "$($d.leafEn) Leaf Carpet"; zh = "$($d.leafZh)地毯" }
        }
    }

    # azalea / flowering azalea (vanilla leaves): hedges from oak logs (Quark's recipe without AzaleaWood)
    $oakLogs = @('minecraft:oak_log', 'minecraft:oak_wood', 'minecraft:stripped_oak_log', 'minecraft:stripped_oak_wood')
    foreach ($az in @(
            @{ id = 'azalea'; en = 'Azalea'; zh = '杜鹃树叶'; tex = 'azalea_leaves'; leaves = 'minecraft:azalea_leaves' },
            @{ id = 'flowering_azalea'; en = 'Flowering Azalea'; zh = '盛开的杜鹃树叶'; tex = 'azalea_leaves_flowers'; leaves = 'minecraft:azalea_leaves_flowered' })) {
        New-WvHedge -Id "lepton:$($az.id)_hedge" -LogTexture "${T}log_oak" -LeafTexture "$T$($az.tex)" -Tint 'none' -Leaves $az.leaves -Logs $oakLogs `
            -RecipeName "$($az.id)_hedge_oak" -MapColor '#5e7c16' -Name @{ en = "$($az.en) Leaf Hedge"; zh = "$($az.zh)篱" }
        New-WvLeafCarpet -Id "lepton:$($az.id)_leaf_carpet" -LeafTexture "$T$($az.tex)" -Tint 'none' -Leaves $az.leaves -MapColor '#5e7c16' `
            -Name @{ en = "$($az.en) Leaf Carpet"; zh = "$($az.zh)地毯" }
    }

    # Quark's oak bookshelf / oak ladder recipes (vanilla results)
    Write-WvRecipe 'oak_bookshelf' @('###', 'XXX', '###') @{ '#' = 'minecraft:oak_planks'; 'X' = 'minecraft:book' } 'minecraft:bookshelf' 1 -1
    Write-WvRecipe 'oak_ladder' @('# #', '#W#', '# #') @{ '#' = 'minecraft:stick'; 'W' = 'minecraft:oak_planks' } 'minecraft:ladder' 4 -1

    Complete-PackUnit
}

if ($MyInvocation.InvocationName -ne '.') { Invoke-WoodVariantsPack }
