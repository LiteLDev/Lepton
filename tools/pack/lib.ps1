# Pack generators for Lepton: blocks, shapes and wood sets (behavior + resource pack JSON).
#
# Dot-source it from tools/pack/<unit>.ps1 and commit what it writes:
#
#   . "$PSScriptRoot/lib.ps1"
#   Start-PackUnit 'stones'                       # forgets what the last run of this unit wrote
#   New-ShapeSet -Material 'polished_calcite' -Shapes block,stairs,slab,vertical_slab,wall `
#       -Textures 'textures/blocks/polished_calcite' -Name @{ en = 'Polished Calcite'; zh = '磨制方解石' } `
#       -Hardness 1.5 -Resistance 6 -MapColor '#dfe0dc' -Tool pickaxe -Stonecutter
#   New-ShapeSet -Material 'stone_bricks' -Prefix 'stone_brick' -BaseBlock 'minecraft:stone_bricks' `
#       -Slab 'minecraft:stone_brick_slab' -Shapes vertical_slab -Textures 'textures/blocks/stonebrick' `
#       -Name @{ en = 'Stone Brick'; zh = '石砖' } -Hardness 1.5 -Resistance 6 -Tool pickaxe -Stonecutter
#   New-WoodSet -Wood 'azalea' -Name @{ en = 'Azalea'; zh = '杜鹃' } -Textures @{ ... }
#   Complete-PackUnit                             # writes the unit's fragments + file list
#
# Everything is deterministic (stable key order, LF, no timestamps): re-running a unit script on
# unchanged input gives byte-identical files. See tools/pack/README.md for the block contract
# (state names, tags) that src/game/Shapes.cpp implements natively.

$ErrorActionPreference = 'Stop'
$script:RepoRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$script:BP = Join-Path $script:RepoRoot 'assets/pack/bp'
$script:RP = Join-Path $script:RepoRoot 'assets/pack/rp'
$script:FormatVersion = '1.26.0'
$script:RecipeFormat = '1.20.10'
$script:ItemFormat = '1.21.60'
# Bedrock block geometry: +x of the model is world west (the model is mirrored on x relative to
# world coordinates; collision/selection boxes are not).
$script:GeoMirrorX = $true
$script:Pack = $null

# ------------------------------------------------------------------------------------------------
# Unit session and file writing
# ------------------------------------------------------------------------------------------------

function Start-PackUnit {
    param([Parameter(Mandatory)][string]$Unit)
    if ($Unit -notmatch '^[a-z0-9_]+$') { throw "Start-PackUnit: unit '$Unit' must be lowercase [a-z0-9_]" }
    $list = Join-Path $PSScriptRoot "generated/$Unit.txt"
    if (Test-Path $list) {
        foreach ($rel in Get-Content $list) {
            $p = Join-Path $script:RepoRoot $rel
            if ($rel -and (Test-Path $p)) { Remove-Item -Force $p }
        }
    }
    $script:Pack = [ordered]@{
        Unit    = $Unit
        Files   = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
        Blocks  = [System.Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
        Terrain = [System.Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
        Items   = [System.Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
        TextsEn = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
        TextsZh = [System.Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
    }
}

function Assert-PackUnit { if (-not $script:Pack) { throw 'Call Start-PackUnit <unit> first' } }

function Get-RelPath([string]$Path) {
    return [IO.Path]::GetRelativePath($script:RepoRoot, $Path).Replace('\', '/')
}

# Writes text with LF line endings, UTF-8 without BOM, trailing newline. Records the file for the
# unit unless -Shared (shared files, e.g. geometry, are never deleted by Start-PackUnit).
function Write-PackText {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][AllowEmptyString()][string]$Text, [switch]$Shared)
    $text = $Text.Replace("`r`n", "`n")
    if (-not $text.EndsWith("`n")) { $text += "`n" }
    $dir = Split-Path -Parent $Path
    if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
    [IO.File]::WriteAllText($Path, $text, [Text.UTF8Encoding]::new($false))
    if (-not $Shared -and $script:Pack) { [void]$script:Pack.Files.Add((Get-RelPath $Path)) }
}

# Records a file the unit script wrote itself (e.g. a texture PNG), so the next Start-PackUnit
# removes it if the script stops producing it. Returns the path.
function Add-PackFile([string]$Path) {
    Assert-PackUnit
    [void]$script:Pack.Files.Add((Get-RelPath $Path))
    return $Path
}

function ConvertTo-PackJson($Object) { return ($Object | ConvertTo-Json -Depth 100) }

function Write-PackJson {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Object, [switch]$Shared)
    Write-PackText -Path $Path -Text (ConvertTo-PackJson $Object) -Shared:$Shared
}

function Add-PackText([string]$Key, [hashtable]$Name) {
    Assert-PackUnit
    if (-not $Name.en -or -not $Name.zh) { throw "Add-PackText: '$Key' needs en and zh names" }
    $script:Pack.TextsEn[$Key] = $Name.en
    $script:Pack.TextsZh[$Key] = $Name.zh
}

# Registers a terrain texture for a texture path ('textures/blocks/foo', vanilla or ours) and
# returns its atlas key. Keys are prefixed with the unit, so units never collide.
function Add-TerrainTexture([string]$TexturePath) {
    Assert-PackUnit
    $base = ($TexturePath -replace '^textures/blocks/', '' -replace '[^A-Za-z0-9_]', '_')
    $key = "lepton_$($script:Pack.Unit)_$base"
    $script:Pack.Terrain[$key] = [ordered]@{ textures = $TexturePath }
    return $key
}

function Add-ItemTexture([string]$TexturePath) {
    Assert-PackUnit
    $base = ($TexturePath -replace '^textures/items/', '' -replace '[^A-Za-z0-9_]', '_')
    $key = "lepton_$($script:Pack.Unit)_$base"
    $script:Pack.Items[$key] = [ordered]@{ textures = $TexturePath }
    return $key
}

function Complete-PackUnit {
    Assert-PackUnit
    $u = $script:Pack.Unit
    $frag = "$u-gen"
    if ($script:Pack.Blocks.Count) { Write-PackJson (Join-Path $script:RP "blocks.d/$frag.json") $script:Pack.Blocks }
    if ($script:Pack.Terrain.Count) { Write-PackJson (Join-Path $script:RP "terrain_texture.d/$frag.json") $script:Pack.Terrain }
    if ($script:Pack.Items.Count) { Write-PackJson (Join-Path $script:RP "item_texture.d/$frag.json") $script:Pack.Items }
    foreach ($loc in @(@('en_US', $script:Pack.TextsEn), @('zh_CN', $script:Pack.TextsZh))) {
        if (-not $loc[1].Count) { continue }
        $lines = foreach ($kv in $loc[1].GetEnumerator()) { "$($kv.Key)=$($kv.Value)" }
        Write-PackText (Join-Path $script:RP "texts.d/$($loc[0])/$frag.lang") ($lines -join "`n")
    }
    $list = Join-Path $PSScriptRoot "generated/$u.txt"
    Write-PackText -Path $list -Text (@($script:Pack.Files) -join "`n") -Shared
    Write-Host "[pack] $u`: $($script:Pack.Files.Count) files"
    $script:Pack = $null
}

# ------------------------------------------------------------------------------------------------
# Molang and geometry helpers
# ------------------------------------------------------------------------------------------------

# q.block_state('<state>') == <value>   (strings quoted, bools as true/false)
function Get-StateCond([string]$State, $Value) {
    $v = if ($Value -is [bool]) { if ($Value) { 'true' } else { 'false' } } elseif ($Value -is [string]) { "'$Value'" } else { "$Value" }
    return "q.block_state('$State') == $v"
}

function Join-Or([string[]]$Terms) {
    $t = @($Terms | Where-Object { $_ })
    if (-not $t.Count) { return 'false' }
    if ($t.Count -eq 1) { return $t[0] }
    return '(' + ($t -join ' || ') + ')'
}
function Join-And([string[]]$Terms) {
    $t = @($Terms | Where-Object { $_ })
    if (-not $t.Count) { return 'true' }
    if ($t.Count -eq 1) { return $t[0] }
    return '(' + ($t -join ' && ') + ')'
}

# A box in block pixel coordinates (0..16; x east, y up, z south).
function New-Box([double]$X0, [double]$Y0, [double]$Z0, [double]$X1, [double]$Y1, [double]$Z1) {
    return [pscustomobject]@{ X0 = $X0; Y0 = $Y0; Z0 = $Z0; X1 = $X1; Y1 = $Y1; Z1 = $Z1 }
}

function Get-Num([double]$v) { if ($v -eq [Math]::Floor($v)) { return [int]$v } return $v }

# Geometry cube for a box, with world-aligned ("uv locked") face UVs and per-face material
# instances named after the face (up/down/north/south/east/west).
function New-GeoCube($Box) {
    $x0 = $Box.X0; $y0 = $Box.Y0; $z0 = $Box.Z0; $x1 = $Box.X1; $y1 = $Box.Y1; $z1 = $Box.Z1
    $w = $x1 - $x0; $h = $y1 - $y0; $d = $z1 - $z0
    $ox = if ($script:GeoMirrorX) { 8 - $x1 } else { $x0 - 8 }
    $face = { param($u, $v, $uw, $vh, $mi) [ordered]@{ uv = @((Get-Num $u), (Get-Num $v)); uv_size = @((Get-Num $uw), (Get-Num $vh)); material_instance = $mi } }
    $faces = [ordered]@{
        north = & $face (16 - $x1) (16 - $y1) $w $h 'north'
        east  = & $face (16 - $z1) (16 - $y1) $d $h 'east'
        south = & $face $x0 (16 - $y1) $w $h 'south'
        west  = & $face $z0 (16 - $y1) $d $h 'west'
        up    = & $face $x0 $z0 $w $d 'up'
        down  = & $face $x0 (16 - $z1) $w $d 'down'
    }
    if ($script:GeoMirrorX) {
        # the model's east face is the world's west face
        $e = $faces.east; $faces.east = $faces.west; $faces.west = $e
        $faces.east.material_instance = 'east'; $faces.west.material_instance = 'west'
    }
    return [ordered]@{
        origin = @((Get-Num $ox), (Get-Num $y0), (Get-Num ($z0 - 8)))
        size   = @((Get-Num $w), (Get-Num $h), (Get-Num $d))
        uv     = $faces
    }
}

# Collision / selection box JSON (world aligned, origin relative to the block's bottom centre).
function ConvertTo-BoxJson($Box) {
    return [ordered]@{
        origin = @((Get-Num ($Box.X0 - 8)), (Get-Num $Box.Y0), (Get-Num ($Box.Z0 - 8)))
        size   = @((Get-Num ($Box.X1 - $Box.X0)), (Get-Num ($Box.Y1 - $Box.Y0)), (Get-Num ($Box.Z1 - $Box.Z0)))
    }
}

function Get-BoundingBox([object[]]$Boxes) {
    $b = $Boxes[0]
    $r = New-Box $b.X0 $b.Y0 $b.Z0 $b.X1 $b.Y1 $b.Z1
    foreach ($x in $Boxes) {
        $r.X0 = [Math]::Min($r.X0, $x.X0); $r.Y0 = [Math]::Min($r.Y0, $x.Y0); $r.Z0 = [Math]::Min($r.Z0, $x.Z0)
        $r.X1 = [Math]::Max($r.X1, $x.X1); $r.Y1 = [Math]::Max($r.Y1, $x.Y1); $r.Z1 = [Math]::Max($r.Z1, $x.Z1)
    }
    return $r
}

# minecraft:collision_box value for a list of boxes: false (no boxes), one box, or an array.
function Get-CollisionJson([object[]]$Boxes) {
    $list = @($Boxes | Where-Object { $_ })
    if (-not $list.Count) { return $false }
    if ($list.Count -eq 1) { return ConvertTo-BoxJson $list[0] }
    return , @($list | ForEach-Object { ConvertTo-BoxJson $_ })
}

# Writes the shared geometry file models/blocks/lepton_<name>.geo.json (identifier
# geometry.lepton.<name>). $Bones: ordered dictionary bone name -> list of boxes.
function Write-Geometry([string]$Name, $Bones) {
    $bonesJson = foreach ($kv in $Bones.GetEnumerator()) {
        [ordered]@{ name = $kv.Key; pivot = @(0, 0, 0); cubes = @($kv.Value | ForEach-Object { New-GeoCube $_ }) }
    }
    $geo = [ordered]@{
        format_version       = '1.12.0'
        'minecraft:geometry' = @(
            [ordered]@{
                description = [ordered]@{
                    identifier            = "geometry.lepton.$Name"
                    texture_width         = 16
                    texture_height        = 16
                    visible_bounds_width  = 2
                    visible_bounds_height = 2.5
                    visible_bounds_offset = @(0, 0.75, 0)
                }
                bones       = @($bonesJson)
            }
        )
    }
    Write-PackJson -Path (Join-Path $script:RP "models/blocks/lepton_$Name.geo.json") -Object $geo -Shared
    return "geometry.lepton.$Name"
}

# Horizontal directions: name -> (dx, dz) and helpers.
$script:Dirs = [ordered]@{ north = @(0, -1); south = @(0, 1); west = @(-1, 0); east = @(1, 0) }
function Get-Ccw([string]$d) { switch ($d) { 'north' { 'west' } 'west' { 'south' } 'south' { 'east' } 'east' { 'north' } } }
function Get-Opposite([string]$d) { switch ($d) { 'north' { 'south' } 'south' { 'north' } 'west' { 'east' } 'east' { 'west' } 'up' { 'down' } 'down' { 'up' } } }

# ------------------------------------------------------------------------------------------------
# Shape models (boxes per state). Used for geometry bones and collision boxes alike.
# ------------------------------------------------------------------------------------------------

$script:StairShapes = @('straight', 'inner_left', 'inner_right', 'outer_left', 'outer_right')
$script:Quadrants = [ordered]@{ nw = @(-1, -1); ne = @(1, -1); sw = @(-1, 1); se = @(1, 1) }

function Test-StairQuadrant([string]$Quadrant, [string]$Facing, [string]$Shape) {
    $q = $script:Quadrants[$Quadrant]; $f = $script:Dirs[$Facing]; $l = $script:Dirs[(Get-Ccw $Facing)]
    $front = ($q[0] * $f[0] + $q[1] * $f[1]) -gt 0
    $left = ($q[0] * $l[0] + $q[1] * $l[1]) -gt 0
    switch ($Shape) {
        'straight' { return $front }
        'outer_left' { return $front -and $left }
        'outer_right' { return $front -and -not $left }
        'inner_left' { return $front -or $left }
        'inner_right' { return $front -or -not $left }
    }
}

function Get-QuadrantBox([string]$Quadrant, [bool]$Upper) {
    $q = $script:Quadrants[$Quadrant]
    $x0 = if ($q[0] -lt 0) { 0 } else { 8 }; $z0 = if ($q[1] -lt 0) { 0 } else { 8 }
    $y0 = if ($Upper) { 8 } else { 0 }
    return New-Box $x0 $y0 $z0 ($x0 + 8) ($y0 + 8) ($z0 + 8)
}

function Get-StairBoxes([string]$Facing, [string]$Half, [string]$Shape) {
    $boxes = @(if ($Half -eq 'bottom') { New-Box 0 0 0 16 8 16 } else { New-Box 0 8 0 16 16 16 })
    foreach ($q in $script:Quadrants.Keys) {
        if (Test-StairQuadrant $q $Facing $Shape) { $boxes += Get-QuadrantBox $q ($Half -eq 'bottom') }
    }
    return , $boxes
}

$script:VSlabBoxes = [ordered]@{
    # Quark's VerticalSlabType: the type's direction is the open side; the slab fills the other half.
    north  = New-Box 0 0 8 16 16 16
    south  = New-Box 0 0 0 16 16 8
    west   = New-Box 8 0 0 16 16 16
    east   = New-Box 0 0 0 8 16 16
    double = New-Box 0 0 0 16 16 16
}

function Get-WallArm([string]$Dir, [bool]$Tall) {
    $h = if ($Tall) { 16 } else { 14 }
    switch ($Dir) {
        'north' { New-Box 5 0 0 11 $h 8 } 'south' { New-Box 5 0 8 11 $h 16 }
        'west' { New-Box 0 0 5 8 $h 11 } 'east' { New-Box 8 0 5 16 $h 11 }
    }
}
function Get-FenceArm([string]$Dir) {
    switch ($Dir) {
        'north' { @((New-Box 7 12 0 9 15 6), (New-Box 7 6 0 9 9 6)) }
        'south' { @((New-Box 7 12 10 9 15 16), (New-Box 7 6 10 9 9 16)) }
        'west' { @((New-Box 0 12 7 6 15 9), (New-Box 0 6 7 6 9 9)) }
        'east' { @((New-Box 10 12 7 16 15 9), (New-Box 10 6 7 16 9 9)) }
    }
}
function Get-FenceArmCollision([string]$Dir) {
    switch ($Dir) {
        'north' { New-Box 6 0 0 10 16 6 } 'south' { New-Box 6 0 10 10 16 16 }
        'west' { New-Box 0 0 6 6 16 10 } 'east' { New-Box 10 0 6 16 16 10 }
    }
}
function Get-PaneArm([string]$Dir) {
    switch ($Dir) {
        'north' { New-Box 7 0 0 9 16 7 } 'south' { New-Box 7 0 9 9 16 16 }
        'west' { New-Box 0 0 7 7 16 9 } 'east' { New-Box 9 0 7 16 16 9 }
    }
}

# Swaps x and z of a box (a model built along x becomes one along z).
function Switch-BoxAxes($b) { return New-Box $b.Z0 $b.Y0 $b.X0 $b.Z1 $b.Y1 $b.X1 }
function Move-Box($b, [double]$dy) { return New-Box $b.X0 ($b.Y0 + $dy) $b.Z0 $b.X1 ($b.Y1 + $dy) $b.Z1 }
# Mirrors a box on z (z -> 16 - z) or x.
function Get-MirrorZ($b) { return New-Box $b.X0 $b.Y0 (16 - $b.Z1) $b.X1 $b.Y1 (16 - $b.Z0) }
function Get-MirrorX($b) { return New-Box (16 - $b.X1) $b.Y0 $b.Z0 (16 - $b.X0) $b.Y1 $b.Z1 }

# Fence gate model. Closed gates span the axis across the facing; open gates swing towards the facing.
function Get-GateBoxes([string]$Facing, [bool]$Open, [bool]$InWall) {
    $posts = @((New-Box 0 5 7 2 16 9), (New-Box 14 5 7 16 16 9))
    if (-not $Open) {
        $boxes = $posts + @(
            (New-Box 6 6 7 8 15 9), (New-Box 8 6 7 10 15 9),
            (New-Box 2 6 7 6 9 9), (New-Box 2 12 7 6 15 9), (New-Box 10 6 7 14 9 9), (New-Box 10 12 7 14 15 9))
    } else {
        # facing south: the two halves lie along the posts, pointing to +z
        $boxes = $posts + @(
            (New-Box 0 6 13 2 15 15), (New-Box 0 6 9 2 9 13), (New-Box 0 12 9 2 15 13),
            (New-Box 14 6 13 16 15 15), (New-Box 14 6 9 16 9 13), (New-Box 14 12 9 16 15 13))
        if ($Facing -eq 'north' -or $Facing -eq 'west') { $boxes = @($boxes | ForEach-Object { Get-MirrorZ $_ }) }
    }
    if ($Facing -eq 'east' -or $Facing -eq 'west') { $boxes = @($boxes | ForEach-Object { Switch-BoxAxes $_ }) }
    if ($InWall) { $boxes = @($boxes | ForEach-Object { Move-Box $_ -3 }) }
    return , $boxes
}

$script:DoorPanels = [ordered]@{
    n = New-Box 0 0 0 16 16 3; s = New-Box 0 0 13 16 16 16; w = New-Box 0 0 0 3 16 16; e = New-Box 13 0 0 16 16 16
}
# The door panel (n/s/w/e) for a door state, as in Java's DoorBlock#getShape.
function Get-DoorPanel([string]$Facing, [bool]$Open, [bool]$HingeRight) {
    switch ($Facing) {
        'east' { if (-not $Open) { 'w' } elseif ($HingeRight) { 's' } else { 'n' } }
        'south' { if (-not $Open) { 'n' } elseif ($HingeRight) { 'w' } else { 'e' } }
        'west' { if (-not $Open) { 'e' } elseif ($HingeRight) { 'n' } else { 's' } }
        'north' { if (-not $Open) { 's' } elseif ($HingeRight) { 'e' } else { 'w' } }
    }
}
# Trapdoor panel: closed bottom/top, open against the side opposite the facing.
function Get-TrapdoorPanel([string]$Facing, [string]$Half, [bool]$Open) {
    if (-not $Open) { if ($Half -eq 'top') { return 'top' } else { return 'bottom' } }
    switch ($Facing) { 'north' { 's' } 'south' { 'n' } 'west' { 'e' } 'east' { 'w' } }
}
$script:TrapdoorPanels = [ordered]@{
    bottom = New-Box 0 0 0 16 3 16; top = New-Box 0 13 0 16 16 16
    n = New-Box 0 0 0 16 16 3; s = New-Box 0 0 13 16 16 16; w = New-Box 0 0 0 3 16 16; e = New-Box 13 0 0 16 16 16
}

function Get-ButtonBox([string]$Face, [bool]$Pressed) {
    $d = if ($Pressed) { 1 } else { 2 }
    switch ($Face) {
        'up' { New-Box 5 0 6 11 $d 10 }
        'down' { New-Box 5 (16 - $d) 6 11 16 10 }
        'north' { New-Box 5 6 (16 - $d) 11 10 16 }
        'south' { New-Box 5 6 0 11 10 $d }
        'west' { New-Box (16 - $d) 6 5 16 10 11 }
        'east' { New-Box 0 6 5 $d 10 11 }
    }
}

# ------------------------------------------------------------------------------------------------
# Shared geometries (written once, identical for every unit)
# ------------------------------------------------------------------------------------------------

function Write-SharedGeometries {
    $g = [ordered]@{}
    # stairs: a base slab per half + 8 quadrants
    $stairs = [ordered]@{ base_bottom = @(New-Box 0 0 0 16 8 16); base_top = @(New-Box 0 8 0 16 16 16) }
    foreach ($q in $script:Quadrants.Keys) { $stairs["upper_$q"] = @(Get-QuadrantBox $q $true); $stairs["lower_$q"] = @(Get-QuadrantBox $q $false) }
    $g.stairs = Write-Geometry 'stairs' $stairs
    $g.slab = Write-Geometry 'slab' ([ordered]@{ bottom = @(New-Box 0 0 0 16 8 16); top = @(New-Box 0 8 0 16 16 16) })
    $g.vertical_slab = Write-Geometry 'vertical_slab' ([ordered]@{
            half_n = @(New-Box 0 0 0 16 16 8); half_s = @(New-Box 0 0 8 16 16 16)
            half_w = @(New-Box 0 0 0 8 16 16); half_e = @(New-Box 8 0 0 16 16 16) })
    $wall = [ordered]@{ post = @(New-Box 4 0 4 12 16 12) }
    foreach ($d in $script:Dirs.Keys) { $wall["${d}_short"] = @(Get-WallArm $d $false); $wall["${d}_tall"] = @(Get-WallArm $d $true) }
    $g.wall = Write-Geometry 'wall' $wall
    $fence = [ordered]@{ post = @(New-Box 6 0 6 10 16 10) }
    foreach ($d in $script:Dirs.Keys) { $fence[$d] = @(Get-FenceArm $d) }
    $g.fence = Write-Geometry 'fence' $fence
    $pane = [ordered]@{ post = @(New-Box 7 0 7 9 16 9) }
    foreach ($d in $script:Dirs.Keys) { $pane[$d] = @(Get-PaneArm $d) }
    $g.pane = Write-Geometry 'pane' $pane
    $gate = [ordered]@{}
    foreach ($w in $false, $true) {
        $sfx = if ($w) { '_wall' } else { '' }
        $gate["closed_x$sfx"] = Get-GateBoxes 'north' $false $w
        $gate["closed_z$sfx"] = Get-GateBoxes 'east' $false $w
        foreach ($d in $script:Dirs.Keys) { $gate["open_$d$sfx"] = Get-GateBoxes $d $true $w }
    }
    $g.fence_gate = Write-Geometry 'fence_gate' $gate
    $door = [ordered]@{}
    foreach ($k in $script:DoorPanels.Keys) { $door["panel_$k"] = @($script:DoorPanels[$k]) }
    $g.door = Write-Geometry 'door' $door
    $trap = [ordered]@{}
    foreach ($k in $script:TrapdoorPanels.Keys) { $trap["panel_$k"] = @($script:TrapdoorPanels[$k]) }
    $g.trapdoor = Write-Geometry 'trapdoor' $trap
    $button = [ordered]@{}
    foreach ($f in 'up', 'down', 'north', 'south', 'west', 'east') {
        $button[$f] = @(Get-ButtonBox $f $false); $button["${f}_pressed"] = @(Get-ButtonBox $f $true)
    }
    $g.button = Write-Geometry 'button' $button
    $g.pressure_plate = Write-Geometry 'pressure_plate' ([ordered]@{ up = @(New-Box 1 0 1 15 1 15); down = @(New-Box 1 0 1 15 0.5 15) })
    return $g
}

# ------------------------------------------------------------------------------------------------
# Block JSON
# ------------------------------------------------------------------------------------------------

# Texture spec: a texture path for all faces, or @{ side; top; bottom } (missing faces use side,
# bottom defaults to top). Returns material instances for every face.
function Get-MaterialInstances($Textures, [string]$RenderMethod = 'opaque') {
    $t = if ($Textures -is [string]) { @{ side = $Textures } } else { $Textures }
    $side = $t.side; if (-not $side) { $side = $t.all }
    $top = if ($t.top) { $t.top } else { $side }
    $bottom = if ($t.bottom) { $t.bottom } else { $top }
    $mk = { param($p) [ordered]@{ texture = (Add-TerrainTexture $p); render_method = $RenderMethod } }
    $s = & $mk $side
    $mi = [ordered]@{ '*' = $s }
    foreach ($f in 'north', 'south', 'east', 'west') {
        $mi[$f] = if ($t[$f]) { & $mk $t[$f] } else { & $mk $side }
    }
    $mi.up = & $mk $top
    $mi.down = & $mk $bottom
    return $mi
}

$script:ToolTags = @{
    pickaxe = 'minecraft:is_pickaxe_item_destructible'; axe = 'minecraft:is_axe_item_destructible'
    shovel = 'minecraft:is_shovel_item_destructible'; hoe = 'minecraft:is_hoe_item_destructible'
}

# Components every generated block shares (mining, explosions, map colour, fire, tags).
function Get-BaseComponents($Spec) {
    $c = [ordered]@{
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = $Spec.Hardness }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = $Spec.Resistance }
    }
    if ($Spec.MapColor) { $c['minecraft:map_color'] = $Spec.MapColor }
    if ($Spec.Flammable) { $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = 5; destroy_chance_modifier = 20 } }
    $tags = @($Spec.Tags)
    if ($Spec.Tool -and $script:ToolTags[$Spec.Tool]) { $tags += $script:ToolTags[$Spec.Tool] }
    if ($Spec.Tool -eq 'pickaxe') { $tags += 'stone' }
    if ($Spec.Flammable -or $Spec.Tool -eq 'axe') { $tags += 'wood' }
    foreach ($tag in ($tags | Where-Object { $_ } | Sort-Object -Unique)) { $c["tag:$tag"] = [ordered]@{} }
    return $c
}

function Get-MenuCategory($Spec, [string]$Group) {
    if ($Spec.Hidden) { return [ordered]@{ category = 'none'; is_hidden_in_commands = $true } }
    $m = [ordered]@{ category = $Spec.Category }
    if ($Group) { $m.group = $Group }
    return $m
}

# Writes bp/blocks/<path>.json. $Description extras: states, traits.
function Write-BlockJson {
    param([string]$Id, $Menu, $States, $Traits, $Components, $Permutations)
    $desc = [ordered]@{ identifier = $Id; menu_category = $Menu }
    if ($States -and $States.Count) { $desc.states = $States }
    if ($Traits -and $Traits.Count) { $desc.traits = $Traits }
    $block = [ordered]@{ description = $desc; components = $Components }
    if ($Permutations -and @($Permutations).Count) { $block.permutations = @($Permutations) }
    $path = ($Id -split ':')[1]
    Write-PackJson (Join-Path $script:BP "blocks/$path.json") ([ordered]@{ format_version = $script:FormatVersion; 'minecraft:block' = $block })
}

function Write-LootTable([string]$Name, [string]$Item, [int]$Count = 1) {
    $entry = [ordered]@{ type = 'item'; name = $Item; weight = 1 }
    if ($Count -ne 1) { $entry.functions = @([ordered]@{ function = 'set_count'; count = $Count }) }
    Write-PackJson (Join-Path $script:BP "loot_tables/blocks/$Name.json") ([ordered]@{ pools = @([ordered]@{ rolls = 1; entries = @($entry) }) })
    return "loot_tables/blocks/$Name.json"
}

function Write-ShapedRecipe([string]$Name, [string[]]$Pattern, [hashtable]$Key, [string]$Result, [int]$Count) {
    $k = [ordered]@{}
    foreach ($ch in ($Key.Keys | Sort-Object)) { $k[$ch] = [ordered]@{ item = $Key[$ch] } }
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version          = $script:RecipeFormat
            'minecraft:recipe_shaped' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @('crafting_table')
                pattern     = @($Pattern)
                key         = $k
                unlock      = @(@($Key.Values | Sort-Object -Unique) | ForEach-Object { [ordered]@{ item = $_ } })
                result      = [ordered]@{ item = $Result; count = $Count }
            }
        })
}

function Write-ShapelessRecipe([string]$Name, [string[]]$Ingredients, [string]$Result, [int]$Count, [string]$Tag = 'crafting_table') {
    Write-PackJson (Join-Path $script:BP "recipes/$Name.json") ([ordered]@{
            format_version             = $script:RecipeFormat
            'minecraft:recipe_shapeless' = [ordered]@{
                description = [ordered]@{ identifier = "lepton:$Name" }
                tags        = @($Tag)
                ingredients = @($Ingredients | ForEach-Object { [ordered]@{ item = $_ } })
                unlock      = @(@($Ingredients | Sort-Object -Unique) | ForEach-Object { [ordered]@{ item = $_ } })
                result      = [ordered]@{ item = $Result; count = $Count }
            }
        })
}

function Get-Path([string]$Id) { return ($Id -split ':')[-1] }

function Register-BlockClient([string]$Id, [string]$Sound, [hashtable]$Name) {
    $script:Pack.Blocks[$Id] = [ordered]@{ sound = $Sound }
    Add-PackText "tile.$Id.name" $Name
}

# ------------------------------------------------------------------------------------------------
# Public generators
# ------------------------------------------------------------------------------------------------

# A full cube block (also the base block of a shape set). -Axis makes it a pillar (logs): the
# minecraft:block_face trait records the clicked face and the model is rotated so the texture's
# "up" faces lie along that axis.
function New-FullBlock {
    param(
        [Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)]$Textures, [Parameter(Mandatory)][hashtable]$Name,
        [string]$Sound = 'stone', [double]$Hardness = 1.5, [double]$Resistance = 6, [string]$MapColor,
        [switch]$Flammable, [string]$Tool, [string[]]$Tags = @(), [string]$Category = 'construction', [string]$Group,
        [switch]$Hidden, [string]$RenderMethod = 'opaque', [switch]$Axis, [string]$Loot
    )
    Assert-PackUnit
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $c = Get-BaseComponents $spec
    $c['minecraft:geometry'] = 'minecraft:geometry.full_block'
    $c['minecraft:material_instances'] = Get-MaterialInstances $Textures $RenderMethod
    $c['minecraft:loot'] = if ($Loot) { $Loot } else { Write-LootTable (Get-Path $Id) $Id }
    $traits = $null; $perms = $null
    if ($Axis) {
        $c['tag:lepton:pillar'] = [ordered]@{}
        $traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
        $perms = @(
            [ordered]@{ condition = "q.block_state('minecraft:block_face') == 'north' || q.block_state('minecraft:block_face') == 'south'"
                components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(90, 0, 0) } } },
            [ordered]@{ condition = "q.block_state('minecraft:block_face') == 'east' || q.block_state('minecraft:block_face') == 'west'"
                components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, 0, 90) } } }
        )
    }
    Write-BlockJson -Id $Id -Menu (Get-MenuCategory $spec $Group) -Traits $traits -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
}

# One material -> its shapes. See the header of this file and tools/pack/README.md.
function New-ShapeSet {
    param(
        [Parameter(Mandatory)][string]$Material,          # path of the full block ("polished_calcite")
        [string]$Prefix,                                   # shape id prefix (default: Material); "stone_brick" for stone_bricks
        [ValidateSet('block', 'stairs', 'slab', 'vertical_slab', 'wall', 'fence', 'fence_gate', 'pane')][string[]]$Shapes = @('block', 'stairs', 'slab', 'vertical_slab'),
        [Parameter(Mandatory)]$Textures,                   # 'textures/blocks/x' or @{ side; top; bottom }
        [Parameter(Mandatory)][hashtable]$Name,            # @{ en = 'Polished Calcite'; zh = '...' } (the full block's name)
        [hashtable]$Names = @{},                           # per-shape name overrides: @{ stairs = @{ en; zh } }
        [hashtable]$Ids = @{},                             # per-shape id overrides: @{ vertical_slab = 'lepton:x' }
        [string]$BaseBlock,                                # full block id (default lepton:<Material>; a vanilla id = shapes only)
        [string]$Slab,                                     # slab item for the vertical slab recipes (default: the generated slab)
        [string]$Sound = 'stone',
        [double]$Hardness = 1.5, [double]$Resistance = 6, [string]$MapColor, [switch]$Flammable,
        [ValidateSet('', 'pickaxe', 'axe', 'shovel', 'hoe')][string]$Tool = '',
        [string[]]$Tags = @(),                             # extra block tags on every generated block
        [switch]$Stonecutter,                              # stonecutter recipes from the base block
        [switch]$NoCrafting,                               # no crafting recipes
        [string]$FenceStick = 'minecraft:stick',           # fence/gate side ingredient (nether brick fences: minecraft:netherbrick)
        [string]$RenderMethod = 'opaque',
        [string]$Category = 'construction',
        [switch]$Hidden                                    # test blocks: not in the creative inventory / commands
    )
    Assert-PackUnit
    $null = Write-SharedGeometries
    if (-not $Prefix) { $Prefix = $Material }
    $base = if ($BaseBlock) { $BaseBlock } else { "lepton:$Material" }
    $id = @{}
    foreach ($s in 'stairs', 'slab', 'vertical_slab', 'wall', 'fence', 'fence_gate', 'pane') { $id[$s] = if ($Ids[$s]) { $Ids[$s] } else { "lepton:${Prefix}_$s" } }
    $id.block = $base
    $suffix = @{
        stairs = @(' Stairs', '楼梯'); slab = @(' Slab', '台阶'); vertical_slab = @(' Vertical Slab', '竖台阶'); wall = @(' Wall', '墙')
        fence = @(' Fence', '栅栏'); fence_gate = @(' Fence Gate', '栅栏门'); pane = @(' Pane', '板')
    }
    $nameOf = { param($s) if ($Names[$s]) { $Names[$s] } else { @{ en = $Name.en + $suffix[$s][0]; zh = $Name.zh + $suffix[$s][1] } } }
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $slabId = if ($Slab) { $Slab } elseif ($Shapes -contains 'slab') { $id.slab } else { $null }

    if ($Shapes -contains 'block') {
        if ($BaseBlock -and $BaseBlock -notlike 'lepton:*') { throw "New-ShapeSet: -Shapes block with a vanilla -BaseBlock ($BaseBlock)" }
        New-FullBlock -Id $base -Textures $Textures -Name $Name -Sound $Sound -Hardness $Hardness -Resistance $Resistance -MapColor $MapColor `
            -Flammable:$Flammable -Tool $Tool -Tags $Tags -Category $Category -Hidden:$Hidden -RenderMethod $RenderMethod
    }

    $partial = {
        param($shape, $extraTags)
        $c = Get-BaseComponents $spec
        foreach ($t in $extraTags) { $c["tag:$t"] = [ordered]@{} }
        $c['minecraft:light_dampening'] = 0
        $c['minecraft:material_instances'] = Get-MaterialInstances $Textures $RenderMethod
        return $c
    }
    $dirCond = { param($d) Get-StateCond 'minecraft:cardinal_direction' $d }
    $halfCond = { param($h) Get-StateCond 'minecraft:vertical_half' $h }

    # ---- stairs ----
    if ($Shapes -contains 'stairs') {
        $sid = $id.stairs
        $c = & $partial 'stairs' @('lepton:stairs')
        $vis = [ordered]@{ base_bottom = (& $halfCond 'bottom'); base_top = (& $halfCond 'top') }
        foreach ($q in $script:Quadrants.Keys) {
            foreach ($upper in $true, $false) {
                $terms = foreach ($d in $script:Dirs.Keys) {
                    $filled = @($script:StairShapes | Where-Object { Test-StairQuadrant $q $d $_ })
                    if ($filled.Count) { Join-And @((& $dirCond $d), (Join-Or @($filled | ForEach-Object { Get-StateCond 'lepton:shape' $_ }))) }
                }
                $half = if ($upper) { 'bottom' } else { 'top' }
                $vis["$(if ($upper) { 'upper' } else { 'lower' })_$q"] = Join-And @((& $halfCond $half), (Join-Or @($terms)))
            }
        }
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.stairs'; bone_visibility = $vis }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $perms = foreach ($d in $script:Dirs.Keys) {
            foreach ($h in 'bottom', 'top') {
                foreach ($s in $script:StairShapes) {
                    $boxes = Get-StairBoxes $d $h $s
                    [ordered]@{
                        condition  = Join-And @((& $dirCond $d), (& $halfCond $h), (Get-StateCond 'lepton:shape' $s))
                        components = [ordered]@{ 'minecraft:collision_box' = (Get-CollisionJson $boxes) }
                    }
                }
            }
        }
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.stairs') `
            -States ([ordered]@{ 'lepton:shape' = $script:StairShapes }) `
            -Traits ([ordered]@{
                'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 }
                'minecraft:placement_position'  = [ordered]@{ enabled_states = @('minecraft:vertical_half') }
            }) -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf 'stairs')
        if (-not $NoCrafting) { Write-ShapedRecipe (Get-Path $sid) @('#  ', '## ', '###') @{ '#' = $base } $sid 4 }
        if ($Stonecutter) { Write-ShapelessRecipe "$(Get-Path $sid)_from_$(Get-Path $base)_stonecutting" @($base) $sid 1 'stonecutter' }
    }

    # ---- slab ----
    if ($Shapes -contains 'slab') {
        $sid = $id.slab
        $c = & $partial 'slab' @('lepton:slab')
        $double = Get-StateCond 'lepton:double' $true
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.slab'; bone_visibility = [ordered]@{
                bottom = Join-Or @((& $halfCond 'bottom'), $double); top = Join-Or @((& $halfCond 'top'), $double) } }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $c['minecraft:collision_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 8 16)
        $c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 0 0 0 16 8 16)
        $perms = @(
            [ordered]@{ condition = Join-And @((& $halfCond 'top'), (Get-StateCond 'lepton:double' $false)); components = [ordered]@{
                    'minecraft:collision_box' = ConvertTo-BoxJson (New-Box 0 8 0 16 16 16); 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 0 8 0 16 16 16) } },
            [ordered]@{ condition = $double; components = [ordered]@{
                    'minecraft:collision_box' = ConvertTo-BoxJson (New-Box 0 0 0 16 16 16); 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 0 0 0 16 16 16)
                    'minecraft:light_dampening' = 15
                    'minecraft:loot' = Write-LootTable "$(Get-Path $sid)_double" $sid 2 } }
        )
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.slab') `
            -States ([ordered]@{ 'lepton:double' = @($false, $true) }) `
            -Traits ([ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:vertical_half') } }) `
            -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf 'slab')
        if (-not $NoCrafting) { Write-ShapedRecipe (Get-Path $sid) @('###') @{ '#' = $base } $sid 6 }
        if ($Stonecutter) { Write-ShapelessRecipe "$(Get-Path $sid)_from_$(Get-Path $base)_stonecutting" @($base) $sid 2 'stonecutter' }
    }

    # ---- vertical slab ----
    if ($Shapes -contains 'vertical_slab') {
        $sid = $id.vertical_slab
        $c = & $partial 'vertical_slab' @('lepton:vertical_slab')
        $t = { param($v) Get-StateCond 'lepton:type' $v }
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.vertical_slab'; bone_visibility = [ordered]@{
                half_n = Join-Or @((& $t 'south'), (& $t 'double')); half_s = Join-Or @((& $t 'north'), (& $t 'double'))
                half_w = & $t 'east'; half_e = & $t 'west' } }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $perms = foreach ($v in $script:VSlabBoxes.Keys) {
            $comp = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $script:VSlabBoxes[$v]; 'minecraft:selection_box' = ConvertTo-BoxJson $script:VSlabBoxes[$v] }
            if ($v -eq 'double') { $comp['minecraft:light_dampening'] = 15; $comp['minecraft:loot'] = Write-LootTable "$(Get-Path $sid)_double" $sid 2 }
            [ordered]@{ condition = (& $t $v); components = $comp }
        }
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.slab') `
            -States ([ordered]@{ 'lepton:type' = @('north', 'south', 'west', 'east', 'double') }) -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf 'vertical_slab')
        if ($slabId -and -not $NoCrafting) {
            Write-ShapedRecipe (Get-Path $sid) @('#', '#', '#') @{ '#' = $slabId } $sid 3
            Write-ShapelessRecipe "$(Get-Path $sid)_revert" @($sid) $slabId 1
        }
        if ($Stonecutter) { Write-ShapelessRecipe "$(Get-Path $sid)_from_$(Get-Path $base)_stonecutting" @($base) $sid 2 'stonecutter' }
    }

    # ---- wall ----
    if ($Shapes -contains 'wall') {
        $sid = $id.wall
        $c = & $partial 'wall' @('lepton:wall')
        $c['minecraft:connection_rule'] = [ordered]@{ accepts_connections_from = 'all' }   # vanilla walls/panes/bars connect to it
        $vis = [ordered]@{ post = Get-StateCond 'lepton:post' $true }
        foreach ($d in $script:Dirs.Keys) { $vis["${d}_short"] = Get-StateCond "lepton:$d" 'short'; $vis["${d}_tall"] = Get-StateCond "lepton:$d" 'tall' }
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.wall'; bone_visibility = $vis }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $perms = Get-ConnectionPermutations { param($d) (Get-StateCond "lepton:$d" 'none') } { param($d) Get-WallArm $d $true } (New-Box 4 0 4 12 16 12)
        $states = [ordered]@{ 'lepton:post' = @($true, $false) }
        foreach ($d in $script:Dirs.Keys) { $states["lepton:$d"] = @('none', 'short', 'tall') }
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.walls') -States $states -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf 'wall')
        if (-not $NoCrafting) { Write-ShapedRecipe (Get-Path $sid) @('###', '###') @{ '#' = $base } $sid 6 }
        if ($Stonecutter) { Write-ShapelessRecipe "$(Get-Path $sid)_from_$(Get-Path $base)_stonecutting" @($base) $sid 1 'stonecutter' }
    }

    # ---- fence / pane ----
    foreach ($kind in 'fence', 'pane') {
        if ($Shapes -notcontains $kind) { continue }
        $sid = $id[$kind]
        $c = & $partial $kind @("lepton:$kind")
        $c['minecraft:connection_rule'] = [ordered]@{ accepts_connections_from = $(if ($kind -eq 'fence') { 'only_fences' } else { 'all' }) }
        $vis = [ordered]@{ post = 'true' }
        foreach ($d in $script:Dirs.Keys) { $vis[$d] = Get-StateCond "lepton:$d" $true }
        $c['minecraft:geometry'] = [ordered]@{ identifier = "geometry.lepton.$kind"; bone_visibility = $vis }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $post = if ($kind -eq 'fence') { New-Box 6 0 6 10 16 10 } else { New-Box 7 0 7 9 16 9 }
        $arm = if ($kind -eq 'fence') { { param($d) Get-FenceArmCollision $d } } else { { param($d) Get-PaneArm $d } }
        $perms = Get-ConnectionPermutations { param($d) (Get-StateCond "lepton:$d" $false) } $arm $post
        $states = [ordered]@{}
        foreach ($d in $script:Dirs.Keys) { $states["lepton:$d"] = @($false, $true) }
        $group = if ($kind -eq 'fence') { 'minecraft:itemGroup.name.fence' } else { 'minecraft:itemGroup.name.glassPane' }
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec $group) -States $states -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf $kind)
        if (-not $NoCrafting) {
            if ($kind -eq 'fence') { Write-ShapedRecipe (Get-Path $sid) @('#S#', '#S#') @{ '#' = $base; 'S' = $FenceStick } $sid 3 }
            else { Write-ShapedRecipe (Get-Path $sid) @('###', '###') @{ '#' = $base } $sid 16 }
        }
    }

    # ---- fence gate ----
    if ($Shapes -contains 'fence_gate') {
        $sid = $id.fence_gate
        $c = & $partial 'fence_gate' @('lepton:fence_gate')
        $c['minecraft:connection_rule'] = [ordered]@{ accepts_connections_from = 'only_fences' }
        $open = Get-StateCond 'lepton:open' $true; $closed = Get-StateCond 'lepton:open' $false
        $vis = [ordered]@{}
        foreach ($w in $false, $true) {
            $sfx = if ($w) { '_wall' } else { '' }
            $wc = Get-StateCond 'lepton:in_wall' $w
            $vis["closed_x$sfx"] = Join-And @($closed, $wc, (Join-Or @((& $dirCond 'north'), (& $dirCond 'south'))))
            $vis["closed_z$sfx"] = Join-And @($closed, $wc, (Join-Or @((& $dirCond 'east'), (& $dirCond 'west'))))
            foreach ($d in $script:Dirs.Keys) { $vis["open_$d$sfx"] = Join-And @($open, $wc, (& $dirCond $d)) }
        }
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.fence_gate'; bone_visibility = $vis }
        $c['minecraft:loot'] = Write-LootTable (Get-Path $sid) $sid
        $perms = @(
            [ordered]@{ condition = Join-And @($closed, (Join-Or @((& $dirCond 'north'), (& $dirCond 'south')))); components = [ordered]@{
                    'minecraft:collision_box' = ConvertTo-BoxJson (New-Box 0 0 6 16 16 10); 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 0 0 6 16 16 10) } },
            [ordered]@{ condition = Join-And @($closed, (Join-Or @((& $dirCond 'east'), (& $dirCond 'west')))); components = [ordered]@{
                    'minecraft:collision_box' = ConvertTo-BoxJson (New-Box 6 0 0 10 16 16); 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 6 0 0 10 16 16) } },
            [ordered]@{ condition = Join-And @($open, (Join-Or @((& $dirCond 'north'), (& $dirCond 'south')))); components = [ordered]@{
                    'minecraft:collision_box' = $false; 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 0 0 6 16 16 10) } },
            [ordered]@{ condition = Join-And @($open, (Join-Or @((& $dirCond 'east'), (& $dirCond 'west')))); components = [ordered]@{
                    'minecraft:collision_box' = $false; 'minecraft:selection_box' = ConvertTo-BoxJson (New-Box 6 0 0 10 16 16) } }
        )
        Write-BlockJson -Id $sid -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.fenceGate') `
            -States ([ordered]@{ 'lepton:open' = @($false, $true); 'lepton:in_wall' = @($false, $true) }) `
            -Traits ([ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 } }) `
            -Components $c -Permutations $perms
        Register-BlockClient $sid $Sound (& $nameOf 'fence_gate')
        if (-not $NoCrafting) { Write-ShapedRecipe (Get-Path $sid) @('S#S', 'S#S') @{ '#' = $base; 'S' = $FenceStick } $sid 1 }
    }
    return $id
}

# Collision permutations for 4-way connection blocks (fence, pane, wall): 16 combinations of
# "connected or not". $IsNone: scriptblock dir -> molang "not connected"; $Arm: dir -> box.
function Get-ConnectionPermutations([scriptblock]$IsNone, [scriptblock]$Arm, $Post) {
    $dirs = @($script:Dirs.Keys)
    $out = for ($mask = 0; $mask -lt 16; $mask++) {
        $terms = @(); $boxes = @($Post)
        for ($i = 0; $i -lt 4; $i++) {
            $d = $dirs[$i]
            if ($mask -band (1 -shl $i)) { $boxes += & $Arm $d; $terms += "!($(& $IsNone $d))" } else { $terms += (& $IsNone $d) }
        }
        [ordered]@{
            condition  = Join-And $terms
            components = [ordered]@{
                'minecraft:collision_box' = Get-CollisionJson $boxes
                'minecraft:selection_box' = ConvertTo-BoxJson (Get-BoundingBox $boxes)
            }
        }
    }
    return , @($out)
}

# A door (two blocks: lower + upper half), placed by an item with the same id.
function New-DoorBlock {
    param(
        [Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$TopTexture, [Parameter(Mandatory)][string]$BottomTexture,
        [Parameter(Mandatory)][string]$ItemTexture, [Parameter(Mandatory)][hashtable]$Name,
        [string]$Sound = 'wood', [double]$Hardness = 3, [double]$Resistance = 3, [string]$MapColor, [switch]$Flammable,
        [string]$Tool = 'axe', [switch]$RedstoneOnly, [string[]]$Tags = @(), [string]$Category = 'construction', [switch]$Hidden
    )
    Assert-PackUnit
    $null = Write-SharedGeometries
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $c = Get-BaseComponents $spec
    $c['tag:lepton:door'] = [ordered]@{}
    if ($RedstoneOnly) { $c['tag:lepton:redstone_only'] = [ordered]@{} }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:material_instances'] = Get-MaterialInstances $BottomTexture 'alpha_test'
    $dc = { param($d) Get-StateCond 'minecraft:cardinal_direction' $d }
    $vis = [ordered]@{}
    foreach ($p in $script:DoorPanels.Keys) {
        $terms = foreach ($d in $script:Dirs.Keys) { foreach ($o in $false, $true) { foreach ($hr in $false, $true) {
                    if ((Get-DoorPanel $d $o $hr) -eq $p) { Join-And @((& $dc $d), (Get-StateCond 'lepton:open' $o), (Get-StateCond 'lepton:hinge_right' $hr)) } } } }
        $vis["panel_$p"] = Join-Or @($terms)
    }
    $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.door'; bone_visibility = $vis }
    $path = Get-Path $Id
    $c['minecraft:loot'] = Write-LootTable $path $Id
    $perms = @()
    foreach ($d in $script:Dirs.Keys) { foreach ($o in $false, $true) { foreach ($hr in $false, $true) {
                $box = $script:DoorPanels[(Get-DoorPanel $d $o $hr)]
                $perms += [ordered]@{ condition = Join-And @((& $dc $d), (Get-StateCond 'lepton:open' $o), (Get-StateCond 'lepton:hinge_right' $hr))
                    components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $box; 'minecraft:selection_box' = ConvertTo-BoxJson $box } } } } }
    # the upper half shows the top texture; both halves drop the door (the other half is removed
    # without drops by src/game/Shapes.cpp)
    $perms += [ordered]@{ condition = Get-StateCond 'lepton:upper' $true; components = [ordered]@{
            'minecraft:material_instances' = Get-MaterialInstances $TopTexture 'alpha_test' } }
    Write-BlockJson -Id $Id -Menu ([ordered]@{ category = 'none' }) `
        -States ([ordered]@{ 'lepton:open' = @($false, $true); 'lepton:upper' = @($false, $true); 'lepton:hinge_right' = @($false, $true) }) `
        -Traits ([ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 } }) `
        -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
    Add-PackText "item.$Id.name" $Name
    $icon = Add-ItemTexture $ItemTexture
    $menu = if ($Hidden) { [ordered]@{ category = 'none'; is_hidden_in_commands = $true } } else { [ordered]@{ category = $Category; group = 'minecraft:itemGroup.name.door' } }
    Write-PackJson (Join-Path $script:BP "items/$path.json") ([ordered]@{
            format_version   = $script:ItemFormat
            'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = $Id; menu_category = $menu }
                components  = [ordered]@{
                    'minecraft:icon'         = $icon
                    'minecraft:block_placer' = [ordered]@{ block = $Id; replace_block_item = $true }
                    'minecraft:max_stack_size' = 64
                }
            }
        })
}

function New-TrapdoorBlock {
    param(
        [Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [string]$Sound = 'wood', [double]$Hardness = 3, [double]$Resistance = 3, [string]$MapColor, [switch]$Flammable,
        [string]$Tool = 'axe', [switch]$RedstoneOnly, [string[]]$Tags = @(), [string]$Category = 'construction', [switch]$Hidden
    )
    Assert-PackUnit
    $null = Write-SharedGeometries
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $c = Get-BaseComponents $spec
    $c['tag:lepton:trapdoor'] = [ordered]@{}
    if ($RedstoneOnly) { $c['tag:lepton:redstone_only'] = [ordered]@{} }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:material_instances'] = Get-MaterialInstances $Texture 'alpha_test'
    $dc = { param($d) Get-StateCond 'minecraft:cardinal_direction' $d }
    $combos = foreach ($d in $script:Dirs.Keys) { foreach ($h in 'bottom', 'top') { foreach ($o in $false, $true) {
                [pscustomobject]@{ Cond = Join-And @((& $dc $d), (Get-StateCond 'minecraft:vertical_half' $h), (Get-StateCond 'lepton:open' $o)); Panel = Get-TrapdoorPanel $d $h $o } } } }
    $vis = [ordered]@{}
    foreach ($p in $script:TrapdoorPanels.Keys) { $vis["panel_$p"] = Join-Or @($combos | Where-Object { $_.Panel -eq $p } | ForEach-Object { $_.Cond }) }
    $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.trapdoor'; bone_visibility = $vis }
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    $perms = foreach ($x in $combos) {
        $box = $script:TrapdoorPanels[$x.Panel]
        [ordered]@{ condition = $x.Cond; components = [ordered]@{ 'minecraft:collision_box' = ConvertTo-BoxJson $box; 'minecraft:selection_box' = ConvertTo-BoxJson $box } }
    }
    Write-BlockJson -Id $Id -Menu (Get-MenuCategory $spec 'minecraft:itemGroup.name.trapdoor') `
        -States ([ordered]@{ 'lepton:open' = @($false, $true) }) `
        -Traits ([ordered]@{
            'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 }
            'minecraft:placement_position'  = [ordered]@{ enabled_states = @('minecraft:vertical_half') }
        }) -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
}

# Buttons (-Kind button) and pressure plates (-Kind pressure_plate). Pressed states emit a
# redstone signal through minecraft:redstone_producer; src/game/Shapes.cpp presses and releases them.
function New-RedstoneInputBlock {
    param(
        [Parameter(Mandatory)][ValidateSet('button', 'pressure_plate')][string]$Kind,
        [Parameter(Mandatory)][string]$Id, [Parameter(Mandatory)][string]$Texture, [Parameter(Mandatory)][hashtable]$Name,
        [string]$Sound = 'wood', [double]$Hardness = 0.5, [double]$Resistance = 0.5, [string]$MapColor, [switch]$Flammable,
        [string]$Tool = 'axe', [int]$PressTicks = 30, [switch]$MobsOnly, [string[]]$Tags = @(), [string]$Category = 'construction', [switch]$Hidden
    )
    Assert-PackUnit
    $null = Write-SharedGeometries
    $spec = @{ Hardness = $Hardness; Resistance = $Resistance; MapColor = $MapColor; Flammable = $Flammable; Tool = $Tool; Tags = $Tags; Category = $Category; Hidden = $Hidden }
    $c = Get-BaseComponents $spec
    $c["tag:lepton:$Kind"] = [ordered]@{}
    $c["tag:lepton:press_ticks_$PressTicks"] = [ordered]@{}
    if ($MobsOnly) { $c['tag:lepton:mobs_only'] = [ordered]@{} }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:collision_box'] = $false
    $c['minecraft:material_instances'] = Get-MaterialInstances $Texture 'opaque'
    $c['minecraft:loot'] = Write-LootTable (Get-Path $Id) $Id
    $pressed = Get-StateCond 'lepton:pressed' $true
    $perms = @()
    if ($Kind -eq 'button') {
        $vis = [ordered]@{}
        foreach ($f in 'up', 'down', 'north', 'south', 'west', 'east') {
            $fc = Get-StateCond 'minecraft:block_face' $f
            $vis[$f] = Join-And @($fc, (Get-StateCond 'lepton:pressed' $false))
            $vis["${f}_pressed"] = Join-And @($fc, $pressed)
            $perms += [ordered]@{ condition = $fc; components = [ordered]@{ 'minecraft:selection_box' = ConvertTo-BoxJson (Get-ButtonBox $f $false) } }
            $perms += [ordered]@{ condition = Join-And @($fc, $pressed); components = [ordered]@{
                    'minecraft:selection_box'     = ConvertTo-BoxJson (Get-ButtonBox $f $true)
                    'minecraft:redstone_producer' = [ordered]@{ power = 15; strongly_powered_face = (Get-Opposite $f) } } }
        }
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.button'; bone_visibility = $vis }
        $traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
        $group = 'minecraft:itemGroup.name.buttons'
    } else {
        $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.lepton.pressure_plate'; bone_visibility = [ordered]@{
                up = Get-StateCond 'lepton:pressed' $false; down = $pressed } }
        $c['minecraft:selection_box'] = ConvertTo-BoxJson (New-Box 1 0 1 15 1 15)
        $perms += [ordered]@{ condition = $pressed; components = [ordered]@{
                'minecraft:selection_box'     = ConvertTo-BoxJson (New-Box 1 0 1 15 0.5 15)
                'minecraft:redstone_producer' = [ordered]@{ power = 15; strongly_powered_face = 'down' } } }
        $traits = $null
        $group = 'minecraft:itemGroup.name.pressurePlate'
    }
    Write-BlockJson -Id $Id -Menu (Get-MenuCategory $spec $group) -States ([ordered]@{ 'lepton:pressed' = @($false, $true) }) `
        -Traits $traits -Components $c -Permutations $perms
    Register-BlockClient $Id $Sound $Name
}

# A wood set. -Textures keys: log_side, log_top, stripped_log_side, stripped_log_top, planks,
# door_top, door_bottom, door_item, trapdoor (texture paths). -Parts selects what to generate.
# Returns a hashtable part -> id. Signs, hanging signs and boats are not generated (see README).
function New-WoodSet {
    param(
        [Parameter(Mandatory)][string]$Wood,                 # "azalea" -> lepton:azalea_log, ...
        [Parameter(Mandatory)][hashtable]$Name,              # @{ en = 'Azalea'; zh = '杜鹃' }
        [Parameter(Mandatory)][hashtable]$Textures,
        [ValidateSet('log', 'wood', 'stripped_log', 'stripped_wood', 'planks', 'stairs', 'slab', 'vertical_slab', 'fence', 'fence_gate', 'door', 'trapdoor', 'button', 'pressure_plate')]
        [string[]]$Parts = @('log', 'wood', 'stripped_log', 'stripped_wood', 'planks', 'stairs', 'slab', 'vertical_slab', 'fence', 'fence_gate', 'door', 'trapdoor', 'button', 'pressure_plate'),
        [string]$LogWord = 'log', [string]$WoodWord = 'wood',  # "stem"/"hyphae" for nether-like woods
        [string]$MapColor, [string]$Sound = 'wood', [switch]$NonFlammable, [string[]]$Tags = @(),
        [hashtable]$Names = @{},                             # per-part name overrides
        [switch]$Hidden
    )
    Assert-PackUnit
    $fl = -not $NonFlammable
    $id = [ordered]@{}
    $w = $Wood
    $id.log = "lepton:${w}_$LogWord"; $id.wood = "lepton:${w}_$WoodWord"
    $id.stripped_log = "lepton:stripped_${w}_$LogWord"; $id.stripped_wood = "lepton:stripped_${w}_$WoodWord"
    $id.planks = "lepton:${w}_planks"
    foreach ($p in 'door', 'trapdoor', 'button', 'pressure_plate') { $id[$p] = "lepton:${w}_$p" }
    $ln = @{ log = @('Log', '原木'); wood = @('Wood', '木头') }
    if ($LogWord -eq 'stem') { $ln.log = @('Stem', '菌柄'); $ln.wood = @('Hyphae', '菌核') }
    $nm = {
        param($part, $en, $zh)
        if ($Names[$part]) { return $Names[$part] }
        return @{ en = $en; zh = $zh }
    }
    $common_args = @{ Sound = $Sound; MapColor = $MapColor; Hidden = $Hidden }
    if ($Parts -contains 'log') {
        New-FullBlock -Id $id.log -Textures @{ side = $Textures.log_side; top = $Textures.log_top } -Name (& $nm 'log' "$($Name.en) $($ln.log[0])" "$($Name.zh)$($ln.log[1])") `
            -Hardness 2 -Resistance 2 -Flammable:$fl -Tool axe -Axis -Group 'minecraft:itemGroup.name.log' -Tags ($Tags + @('lepton:strippable', 'log')) @common_args
    }
    if ($Parts -contains 'wood') {
        New-FullBlock -Id $id.wood -Textures $Textures.log_side -Name (& $nm 'wood' "$($Name.en) $($ln.wood[0])" "$($Name.zh)$($ln.wood[1])") `
            -Hardness 2 -Resistance 2 -Flammable:$fl -Tool axe -Axis -Group 'minecraft:itemGroup.name.wood' -Tags ($Tags + @('lepton:strippable', 'log')) @common_args
    }
    if ($Parts -contains 'stripped_log') {
        New-FullBlock -Id $id.stripped_log -Textures @{ side = $Textures.stripped_log_side; top = $Textures.stripped_log_top } `
            -Name (& $nm 'stripped_log' "Stripped $($Name.en) $($ln.log[0])" "去皮$($Name.zh)$($ln.log[1])") `
            -Hardness 2 -Resistance 2 -Flammable:$fl -Tool axe -Axis -Group 'minecraft:itemGroup.name.log' -Tags ($Tags + @('log')) @common_args
    }
    if ($Parts -contains 'stripped_wood') {
        New-FullBlock -Id $id.stripped_wood -Textures $Textures.stripped_log_side `
            -Name (& $nm 'stripped_wood' "Stripped $($Name.en) $($ln.wood[0])" "去皮$($Name.zh)$($ln.wood[1])") `
            -Hardness 2 -Resistance 2 -Flammable:$fl -Tool axe -Axis -Group 'minecraft:itemGroup.name.wood' -Tags ($Tags + @('log')) @common_args
    }
    $planksName = & $nm 'planks' "$($Name.en) Planks" "$($Name.zh)木板"
    $shapeParts = @('stairs', 'slab', 'vertical_slab', 'fence', 'fence_gate') | Where-Object { $Parts -contains $_ }
    $shapes = @(); if ($Parts -contains 'planks') { $shapes += 'block' }; $shapes += $shapeParts
    if ($shapes.Count) {
        # vanilla naming: "Oak Stairs", not "Oak Planks Stairs"
        $shapeNames = @{
            stairs = & $nm 'stairs' "$($Name.en) Stairs" "$($Name.zh)木楼梯"
            slab = & $nm 'slab' "$($Name.en) Slab" "$($Name.zh)木台阶"
            vertical_slab = & $nm 'vertical_slab' "$($Name.en) Vertical Slab" "$($Name.zh)木竖台阶"
            fence = & $nm 'fence' "$($Name.en) Fence" "$($Name.zh)木栅栏"
            fence_gate = & $nm 'fence_gate' "$($Name.en) Fence Gate" "$($Name.zh)木栅栏门"
        }
        $sid = New-ShapeSet -Material "${w}_planks" -Prefix $w -Shapes $shapes -Textures $Textures.planks -Name $planksName -Names $shapeNames `
            -Sound $Sound -Hardness 2 -Resistance 3 -MapColor $MapColor -Flammable:$fl -Tool axe -Tags ($Tags + @('lepton:wooden')) -Hidden:$Hidden
        foreach ($s in $shapeParts) { $id[$s] = $sid[$s] }
    }
    if ($Parts -contains 'door') {
        New-DoorBlock -Id $id.door -TopTexture $Textures.door_top -BottomTexture $Textures.door_bottom -ItemTexture $Textures.door_item `
            -Name (& $nm 'door' "$($Name.en) Door" "$($Name.zh)木门") -Sound $Sound -MapColor $MapColor -Flammable:$fl -Tags ($Tags + @('lepton:wooden')) -Hidden:$Hidden
    }
    if ($Parts -contains 'trapdoor') {
        New-TrapdoorBlock -Id $id.trapdoor -Texture $Textures.trapdoor -Name (& $nm 'trapdoor' "$($Name.en) Trapdoor" "$($Name.zh)活板门") `
            -Sound $Sound -MapColor $MapColor -Flammable:$fl -Tags ($Tags + @('lepton:wooden')) -Hidden:$Hidden
    }
    if ($Parts -contains 'button') {
        New-RedstoneInputBlock -Kind button -Id $id.button -Texture $Textures.planks -Name (& $nm 'button' "$($Name.en) Button" "$($Name.zh)按钮") `
            -Sound $Sound -MapColor $MapColor -PressTicks 30 -Tags ($Tags + @('lepton:wooden')) -Hidden:$Hidden
    }
    if ($Parts -contains 'pressure_plate') {
        New-RedstoneInputBlock -Kind pressure_plate -Id $id.pressure_plate -Texture $Textures.planks -Name (& $nm 'pressure_plate' "$($Name.en) Pressure Plate" "$($Name.zh)压力板") `
            -Sound $Sound -MapColor $MapColor -PressTicks 20 -Tags ($Tags + @('lepton:wooden')) -Hidden:$Hidden
    }

    # recipes
    if ($Parts -contains 'planks') {
        foreach ($src in 'log', 'wood', 'stripped_log', 'stripped_wood') {
            if ($Parts -contains $src) { Write-ShapelessRecipe "$(Get-Path $id.planks)_from_$(Get-Path $id[$src])" @($id[$src]) $id.planks 4 }
        }
    }
    if ($Parts -contains 'wood' -and $Parts -contains 'log') { Write-ShapedRecipe (Get-Path $id.wood) @('##', '##') @{ '#' = $id.log } $id.wood 3 }
    if ($Parts -contains 'stripped_wood' -and $Parts -contains 'stripped_log') { Write-ShapedRecipe (Get-Path $id.stripped_wood) @('##', '##') @{ '#' = $id.stripped_log } $id.stripped_wood 3 }
    if ($Parts -contains 'door') { Write-ShapedRecipe (Get-Path $id.door) @('##', '##', '##') @{ '#' = $id.planks } $id.door 3 }
    if ($Parts -contains 'trapdoor') { Write-ShapedRecipe (Get-Path $id.trapdoor) @('###', '###') @{ '#' = $id.planks } $id.trapdoor 2 }
    if ($Parts -contains 'button') { Write-ShapelessRecipe (Get-Path $id.button) @($id.planks) $id.button 1 }
    if ($Parts -contains 'pressure_plate') { Write-ShapedRecipe (Get-Path $id.pressure_plate) @('##') @{ '#' = $id.planks } $id.pressure_plate 1 }
    return $id
}
