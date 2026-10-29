# U21 mobsdeep: entity models (box-UV cubes) and their original pixel-art textures.
#
#   pwsh -NoProfile -File tools/textures/mobsdeep.ps1          # draws every texture
#
# tools/pack/mobsdeep.ps1 dot-sources this file with $MobsdeepDefineOnly = $true to reuse the model
# definitions (Get-MobsdeepModels) when it writes the geometry JSON, then calls
# Invoke-MobsdeepTextures itself. Every texture is painted procedurally from the same cube list
# the geometry uses: seeded noise per face, shading per face direction, plus hand-placed details
# (eyes, mouths, cracks). Nothing is traced from Quark or vanilla art.

param([switch]$NoDraw)

. "$PSScriptRoot/lib.ps1"

$script:MdRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$script:MdRP = Join-Path $script:MdRoot 'assets/pack/rp'

# ------------------------------------------------------------------------------------------------
# Models. A cube: name (paint part), origin [x,y,z], size [w,h,d], uv [u,v], optional inflate.
# A bone: name, parent, pivot, optional rotation, cubes. Units are model pixels; -z is the front.
# ------------------------------------------------------------------------------------------------

function C([string]$Part, [double[]]$Origin, [int[]]$Size, [int[]]$Uv, [double]$Inflate = 0) {
    return [pscustomobject]@{ Part = $Part; Origin = $Origin; Size = $Size; Uv = $Uv; Inflate = $Inflate }
}
function B([string]$Name, [string]$Parent, [double[]]$Pivot, $Cubes = @(), [double[]]$Rotation = $null) {
    return [pscustomobject]@{ Name = $Name; Parent = $Parent; Pivot = $Pivot; Rotation = $Rotation; Cubes = @($Cubes) }
}

function Get-MobsdeepModels {
    $m = [ordered]@{}

    # Stoneling: a round stone head on a small body, stubby arms and legs. 64x32.
    $m.stoneling = [pscustomobject]@{
        Width = 64; Height = 32; Bounds = @(1, 1.5, 0.75)
        Bones = @(
            (B 'root' $null @(0, 0, 0))
            (B 'body' 'root' @(0, 3, 0) @((C 'body' @(-3, 3, -2.5) @(6, 5, 5) @(0, 0))))
            (B 'head' 'body' @(0, 8, 0) @((C 'head' @(-3.5, 8, -3.5) @(7, 6, 7) @(0, 10))))
            (B 'leg0' 'root' @(-1.5, 3, 0) @((C 'limb' @(-2.5, 0, -1) @(2, 3, 2) @(28, 0))))
            (B 'leg1' 'root' @(1.5, 3, 0) @((C 'limb' @(0.5, 0, -1) @(2, 3, 2) @(36, 0))))
            (B 'arm0' 'body' @(-4, 7.5, 0) @((C 'limb' @(-5, 3, -1) @(2, 5, 2) @(44, 0))))
            (B 'arm1' 'body' @(4, 7.5, 0) @((C 'limb' @(3, 3, -1) @(2, 5, 2) @(52, 0))))
            (B 'rightItem' 'body' @(0, 10, 3.5) @() @(-20, 0, 0))
        )
    }
    # Lichen overlay: same cubes, slightly inflated (drawn with a second render controller).
    $m.stoneling_lichen = [pscustomobject]@{
        Width = 64; Height = 32; Bounds = @(1, 1.5, 0.75)
        Bones = @(
            (B 'root' $null @(0, 0, 0))
            (B 'body' 'root' @(0, 3, 0) @((C 'body' @(-3, 3, -2.5) @(6, 5, 5) @(0, 0) 0.15)))
            (B 'head' 'body' @(0, 8, 0) @((C 'head' @(-3.5, 8, -3.5) @(7, 6, 7) @(0, 10) 0.15)))
        )
    }

    # Foxhound: a slim fire-wolf with a pointed snout and a long tail. 64x64.
    $m.foxhound = [pscustomobject]@{
        Width = 64; Height = 64; Bounds = @(2, 1.5, 0.75)
        Bones = @(
            (B 'root' $null @(0, 0, 0))
            (B 'body' 'root' @(0, 9, 0) @((C 'body' @(-3, 6, -5) @(6, 6, 11) @(0, 0))))
            (B 'head' 'body' @(0, 11, -5) @(
                    (C 'head' @(-3.5, 8, -11) @(7, 6, 6) @(0, 17))
                    (C 'snout' @(-1.5, 8, -14) @(3, 3, 3) @(26, 17))
                    (C 'ear' @(-3, 14, -9) @(2, 3, 1) @(38, 17))
                    (C 'ear' @(1, 14, -9) @(2, 3, 1) @(44, 17))))
            (B 'leg0' 'root' @(-2, 6, -3) @((C 'leg' @(-3, 0, -4) @(2, 6, 2) @(0, 29))))
            (B 'leg1' 'root' @(2, 6, -3) @((C 'leg' @(1, 0, -4) @(2, 6, 2) @(8, 29))))
            (B 'leg2' 'root' @(-2, 6, 4) @((C 'leg' @(-3, 0, 3) @(2, 6, 2) @(16, 29))))
            (B 'leg3' 'root' @(2, 6, 4) @((C 'leg' @(1, 0, 3) @(2, 6, 2) @(24, 29))))
            (B 'tail' 'body' @(0, 11, 6) @((C 'tail' @(-1.5, 8, 6) @(3, 3, 8) @(34, 0))) @(20, 0, 0))
        )
    }

    # Wraith: a hooded, floating robe with reaching arms. 64x64.
    $m.wraith = [pscustomobject]@{
        Width = 64; Height = 64; Bounds = @(1.5, 2.5, 1.25)
        Bones = @(
            (B 'root' $null @(0, 0, 0))
            (B 'body' 'root' @(0, 24, 0) @(
                    (C 'robe' @(-4, 10, -2.5) @(8, 14, 5) @(32, 0))
                    (C 'hem' @(-4.5, 2, -3) @(9, 8, 6) @(0, 19))))
            (B 'head' 'body' @(0, 24, 0) @((C 'hood' @(-4, 24, -4) @(8, 8, 8) @(0, 0))))
            (B 'rightArm' 'body' @(-5.5, 22, 0) @((C 'sleeve' @(-7, 12, -1.5) @(3, 12, 3) @(32, 19))) @(-80, 0, 0))
            (B 'leftArm' 'body' @(5.5, 22, 0) @((C 'sleeve' @(4, 12, -1.5) @(3, 12, 3) @(44, 19))) @(-80, 0, 0))
        )
    }

    # Forgotten: a skeleton with a wide-brimmed hat; sword sheathed on its back (leftItem). 64x64.
    $m.forgotten = [pscustomobject]@{
        Width = 64; Height = 64; Bounds = @(1.5, 3, 1.25)
        Bones = @(
            (B 'root' $null @(0, 0, 0))
            (B 'body' 'root' @(0, 24, 0) @((C 'body' @(-4, 12, -2) @(8, 12, 4) @(16, 16))))
            (B 'head' 'body' @(0, 24, 0) @((C 'skull' @(-4, 24, -4) @(8, 8, 8) @(0, 0))))
            (B 'hat' 'head' @(0, 24, 0) @(
                    (C 'brim' @(-7, 31, -7) @(14, 1, 14) @(0, 32))
                    (C 'crown' @(-4.5, 32, -4.5) @(9, 4, 9) @(0, 47))))
            (B 'rightArm' 'body' @(-5, 22, 0) @((C 'bone' @(-6, 12, -1) @(2, 12, 2) @(40, 16))))
            (B 'leftArm' 'body' @(5, 22, 0) @((C 'bone' @(4, 12, -1) @(2, 12, 2) @(48, 16))))
            (B 'rightLeg' 'root' @(-2, 12, 0) @((C 'bone' @(-3, 0, -1) @(2, 12, 2) @(0, 16))))
            (B 'leftLeg' 'root' @(2, 12, 0) @((C 'bone' @(1, 0, -1) @(2, 12, 2) @(8, 16))))
            (B 'rightItem' 'rightArm' @(-6, 13, 1))
            (B 'leftItem' 'body' @(0, 21, 3) @() @(0, 0, 145))
        )
    }

    # The Forgotten Hat as worn by players (attachable; the "head" bone follows the player's head).
    $m.forgotten_hat = [pscustomobject]@{
        Width = 64; Height = 32; Bounds = @(1.5, 1.5, 1.5)
        Bones = @(
            (B 'head' $null @(0, 24, 0))
            (B 'hat' 'head' @(0, 24, 0) @(
                    (C 'brim' @(-7, 31, -7) @(14, 1, 14) @(0, 0) 0.1)
                    (C 'crown' @(-4.5, 32, -4.5) @(9, 4, 9) @(0, 15) 0.1)))
        )
    }
    return $m
}

# ------------------------------------------------------------------------------------------------
# Canvas painting
# ------------------------------------------------------------------------------------------------

function New-Canvas([int]$W, [int]$H) { return , ([System.Drawing.Color[, ]]::new($W, $H)) }

function Save-Canvas($Canvas, [string]$Path) {
    $w = $Canvas.GetLength(0); $h = $Canvas.GetLength(1)
    $bmp = [System.Drawing.Bitmap]::new($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $bmp.SetPixel($x, $y, $Canvas[$x, $y]) } }
        $dir = Split-Path -Parent $Path
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
}

# Face rectangles of a box-UV cube: name -> @(x, y, w, h).
function Get-CubeFaces($Cube) {
    $u = $Cube.Uv[0]; $v = $Cube.Uv[1]; $w = $Cube.Size[0]; $h = $Cube.Size[1]; $d = $Cube.Size[2]
    return [ordered]@{
        up    = @(($u + $d), $v, $w, $d)
        down  = @(($u + $d + $w), $v, $w, $d)
        right = @($u, ($v + $d), $d, $h)
        front = @(($u + $d), ($v + $d), $w, $h)
        left  = @(($u + $d + $w), ($v + $d), $d, $h)
        back  = @(($u + 2 * $d + $w), ($v + $d), $w, $h)
    }
}

$script:FaceLight = @{ up = 1.12; down = 0.72; front = 1.0; back = 0.86; left = 0.92; right = 0.92 }

# Fills every face of the cube with seeded noise from $Ramp (darker toward the bottom of side faces).
function Paint-Cube($Canvas, $Cube, [object[]]$Ramp, [int]$Seed, [double]$Grain = 1.0, [double]$Alpha = 1.0) {
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    $faces = Get-CubeFaces $Cube
    foreach ($f in $faces.Keys) {
        $r = $faces[$f]
        for ($y = 0; $y -lt $r[3]; $y++) {
            for ($x = 0; $x -lt $r[2]; $x++) {
                $t = 0.55 + ($rng.NextDouble() - 0.5) * $Grain
                if ($f -in 'front', 'back', 'left', 'right' -and $r[3] -gt 1) { $t -= 0.25 * ($y / ($r[3] - 1)) - 0.1 }
                $c = ConvertTo-Color (Get-RampColor $Ramp ([Math]::Max(0, [Math]::Min(1, $t))))
                $c = Shade-Color $c $script:FaceLight[$f]
                if ($Alpha -lt 1.0) { $c = [System.Drawing.Color]::FromArgb((Clamp255 (255 * $Alpha)), $c.R, $c.G, $c.B) }
                $Canvas[($r[0] + $x), ($r[1] + $y)] = $c
            }
        }
    }
}

# Stamps rows of palette keys onto one face (x/y offsets inside the face; '.' keeps the pixel).
function Stamp-Face($Canvas, $Cube, [string]$Face, [string[]]$Rows, [hashtable]$Palette, [int]$Dx = 0, [int]$Dy = 0) {
    $r = (Get-CubeFaces $Cube)[$Face]
    for ($y = 0; $y -lt $Rows.Count; $y++) {
        for ($x = 0; $x -lt $Rows[$y].Length; $x++) {
            $ch = [string]$Rows[$y][$x]
            if ($ch -eq '.') { continue }
            $px = $r[0] + $Dx + $x; $py = $r[1] + $Dy + $y
            if ($px -ge $r[0] + $r[2] -or $py -ge $r[1] + $r[3]) { continue }
            $Canvas[$px, $py] = if ($ch -eq '_') { [System.Drawing.Color]::Transparent } else { ConvertTo-Color $Palette[$ch] }
        }
    }
}

# Scatters single pixels of $Color over every face of the cube with probability $P.
function Speckle-Cube($Canvas, $Cube, $Color, [double]$P, [int]$Seed) {
    $rng = [System.Random]::new($Seed)
    $faces = Get-CubeFaces $Cube
    foreach ($f in $faces.Keys) {
        $r = $faces[$f]
        for ($y = 0; $y -lt $r[3]; $y++) {
            for ($x = 0; $x -lt $r[2]; $x++) {
                if ($rng.NextDouble() -lt $P) { $Canvas[($r[0] + $x), ($r[1] + $y)] = (Shade-Color $Color $script:FaceLight[$f]) }
            }
        }
    }
}

function Get-Cubes($Model, [string]$Part) { return @($Model.Bones | ForEach-Object { $_.Cubes } | Where-Object { $_.Part -eq $Part }) }

# ------------------------------------------------------------------------------------------------
# Textures
# ------------------------------------------------------------------------------------------------

# Stoneling variants in Quark's order (index = lepton:variant).
$script:StonelingVariants = [ordered]@{
    stone     = @('#5c5c5c', '#8a8a8a', '#a9a9a9')
    andesite  = @('#5f6260', '#8b8e8c', '#b0b3b1')
    diorite   = @('#8f8f8f', '#c9c9c9', '#f0f0f0')
    granite   = @('#6b4235', '#9c6a57', '#c08d78')
    limestone = @('#8c8670', '#b5ae92', '#d4ccb0')
    calcite   = @('#b7b9b3', '#dddfd9', '#f7f7f2')
    shale     = @('#3f4549', '#636b70', '#868f94')
    jasper    = @('#6b2a22', '#9b4a3a', '#c06a52')
    deepslate = @('#2c2c31', '#4b4b52', '#6a6a72')
    tuff      = @('#53554c', '#727468', '#93958a')
    dripstone = @('#6d5442', '#987660', '#b9967d')
}

function Draw-Stoneling($Models, [string]$OutDir) {
    $model = $Models.stoneling
    $i = 0
    foreach ($name in $script:StonelingVariants.Keys) {
        $stops = $script:StonelingVariants[$name]
        $ramp = New-Ramp $stops 6
        $cv = New-Canvas $model.Width $model.Height
        $seed = 2100 + 17 * $i
        foreach ($c in (Get-Cubes $model 'head')) { Paint-Cube $cv $c $ramp ($seed + 1) 0.9 }
        foreach ($c in (Get-Cubes $model 'body')) { Paint-Cube $cv $c (New-Ramp @((Shade-Color $stops[0] 0.9), $stops[1]) 5) ($seed + 2) 0.8 }
        $k = 3
        foreach ($c in (Get-Cubes $model 'limb')) { Paint-Cube $cv $c (New-Ramp @((Shade-Color $stops[0] 0.75), (Shade-Color $stops[1] 0.9)) 4) ($seed + $k) 0.6; $k++ }
        # face: two glowing gem eyes and a small crack of a mouth (7 x 6 front face)
        $head = (Get-Cubes $model 'head')[0]
        $pal = @{ e = '#1a1a1a'; g = '#66e0d6'; m = (ConvertTo-Hex (Shade-Color $stops[0] 0.6)) }
        Stamp-Face $cv $head 'front' @(
            '.......',
            '.......',
            '.g...g.',
            '.e...e.',
            '.......',
            '..mmm..') $pal
        # cracks on the back and the top
        Stamp-Face $cv $head 'up' @('.......', '..m....', '...m...', '...mm..', '.....m.', '.......', '.......') $pal
        Stamp-Face $cv $head 'back' @('.......', '....m..', '...m...', '...m...', '..m....', '.......') $pal
        Save-Canvas $cv (Join-Path $OutDir "stoneling_$name.png")
        $i++
    }
    # Glimmering Weald lichen: sparse glowing moss on top of the head and shoulders, the rest clear.
    $lich = $Models.stoneling_lichen
    $cv = New-Canvas $lich.Width $lich.Height
    $rng = [System.Random]::new(2199)
    foreach ($c in $lich.Bones.Cubes) {
        $faces = Get-CubeFaces $c
        foreach ($f in $faces.Keys) {
            $r = $faces[$f]
            for ($y = 0; $y -lt $r[3]; $y++) {
                for ($x = 0; $x -lt $r[2]; $x++) {
                    $p = if ($f -eq 'up') { 0.55 } elseif ($f -eq 'down') { 0.0 } else { 0.35 * (1 - $y / [Math]::Max(1, $r[3] - 1)) }
                    if ($rng.NextDouble() -lt $p) {
                        $col = if ($rng.NextDouble() -lt 0.2) { '#9df2e6' } elseif ($rng.NextDouble() -lt 0.5) { '#3f8f6a' } else { '#58b07f' }
                        $cv[($r[0] + $x), ($r[1] + $y)] = ConvertTo-Color $col
                    }
                }
            }
        }
    }
    Save-Canvas $cv (Join-Path $OutDir 'stoneling_lichen.png')
}

# Foxhound coats: red (default), blue (soul sand valley), black (basalt, 1 in 256); x idle / hostile / sleeping.
$script:FoxhoundCoats = [ordered]@{
    red   = @{ fur = @('#5a1208', '#8f2410', '#c4461c'); belly = @('#b8641e', '#e6a04a'); ember = '#ffcf4a'; dark = '#2a0a06' }
    blue  = @{ fur = @('#0e2a4a', '#1d5a86', '#3b8fb8'); belly = @('#3aa3b8', '#8ee0e8'); ember = '#9ff5ff'; dark = '#06131f' }
    black = @{ fur = @('#141416', '#2b2b30', '#45454c'); belly = @('#5a2a12', '#b4501e'); ember = '#ff8a2a'; dark = '#050506' }
}

function Draw-Foxhound($Models, [string]$OutDir) {
    $model = $Models.foxhound
    $ci = 0
    foreach ($coat in $script:FoxhoundCoats.Keys) {
        $c = $script:FoxhoundCoats[$coat]
        foreach ($mood in 'idle', 'hostile', 'sleeping') {
            $cv = New-Canvas $model.Width $model.Height
            $seed = 2150 + 31 * $ci
            $fur = New-Ramp $c.fur 6
            foreach ($cube in (Get-Cubes $model 'body')) { Paint-Cube $cv $cube $fur ($seed + 1) 0.8 }
            foreach ($cube in (Get-Cubes $model 'head')) { Paint-Cube $cv $cube $fur ($seed + 2) 0.7 }
            foreach ($cube in (Get-Cubes $model 'ear')) { Paint-Cube $cv $cube (New-Ramp @($c.dark, $c.fur[1]) 4) ($seed + 3) 0.6 }
            foreach ($cube in (Get-Cubes $model 'snout')) { Paint-Cube $cv $cube (New-Ramp $c.belly 4) ($seed + 4) 0.5 }
            $k = 5
            foreach ($cube in (Get-Cubes $model 'leg')) { Paint-Cube $cv $cube (New-Ramp @($c.dark, $c.fur[0], $c.fur[1]) 5) ($seed + $k) 0.6; $k++ }
            foreach ($cube in (Get-Cubes $model 'tail')) {
                Paint-Cube $cv $cube $fur ($seed + 11) 0.8
                Speckle-Cube $cv $cube $c.ember 0.18 ($seed + 12)
            }
            # glowing embers along the back and belly patch
            $body = (Get-Cubes $model 'body')[0]
            Speckle-Cube $cv $body $c.ember 0.06 ($seed + 13)
            $belly = (Get-CubeFaces $body).down
            for ($y = 0; $y -lt $belly[3]; $y++) { for ($x = 1; $x -lt $belly[2] - 1; $x++) { $cv[($belly[0] + $x), ($belly[1] + $y)] = ConvertTo-Color (Get-RampColor $c.belly (($x + $y) % 3 / 3.0)) } }
            # eyes on the 7 x 6 face
            $head = (Get-Cubes $model 'head')[0]
            $eyes = switch ($mood) {
                'idle' { @('.......', '.......', '.e...e.', '.......', '.......', '.......') }
                'hostile' { @('.......', '.a...a.', '.ee.ee.', '.......', '.......', '.......') }
                default { @('.......', '.......', '.......', '.dd.dd.', '.......', '.......') }
            }
            $epal = @{ e = $c.ember; a = $c.dark; d = $c.dark }
            if ($mood -eq 'hostile') { $epal.e = '#fff27a' }
            Stamp-Face $cv $head 'front' $eyes $epal
            $snout = (Get-Cubes $model 'snout')[0]
            Stamp-Face $cv $snout 'front' @('.n.', '...', '...') @{ n = $c.dark }
            Save-Canvas $cv (Join-Path $OutDir "foxhound_${coat}_$mood.png")
        }
        $ci++
    }
}

function Draw-Wraith($Models, [string]$OutDir) {
    $model = $Models.wraith
    $cv = New-Canvas $model.Width $model.Height
    $cloth = New-Ramp @('#6f7a7c', '#b9c6c4', '#e9f1ef') 6
    foreach ($cube in (Get-Cubes $model 'hood')) { Paint-Cube $cv $cube $cloth 2181 0.7 }
    foreach ($cube in (Get-Cubes $model 'robe')) { Paint-Cube $cv $cube $cloth 2182 0.7 }
    foreach ($cube in (Get-Cubes $model 'sleeve')) { Paint-Cube $cv $cube $cloth 2183 0.6 }
    foreach ($cube in (Get-Cubes $model 'hem')) { Paint-Cube $cv $cube (New-Ramp @('#4d5759', '#9aa7a6', '#d0dbd9') 6) 2184 0.8 0.85 }
    # hood opening: dark void with two pale soul-blue eyes
    $hood = (Get-Cubes $model 'hood')[0]
    Stamp-Face $cv $hood 'front' @(
        '........',
        '.vvvvvv.',
        '.vvvvvv.',
        '.vsvvsv.',
        '.vvvvvv.',
        '.vvvvvv.',
        '..vvvv..',
        '........') @{ v = '#101418'; s = '#8ff7ff' }
    # ragged hem: transparent notches at the bottom of every side face
    $hem = (Get-Cubes $model 'hem')[0]
    $faces = Get-CubeFaces $hem
    $rng = [System.Random]::new(2185)
    foreach ($f in 'front', 'back', 'left', 'right') {
        $r = $faces[$f]
        for ($x = 0; $x -lt $r[2]; $x++) {
            $cut = $rng.Next(0, 4)
            for ($y = $r[3] - $cut; $y -lt $r[3]; $y++) { $cv[($r[0] + $x), ($r[1] + $y)] = [System.Drawing.Color]::Transparent }
        }
    }
    # bony hands at the end of the sleeves (the sleeve's bottom face)
    foreach ($cube in (Get-Cubes $model 'sleeve')) {
        $d = (Get-CubeFaces $cube).down
        for ($y = 0; $y -lt $d[3]; $y++) { for ($x = 0; $x -lt $d[2]; $x++) { $cv[($d[0] + $x), ($d[1] + $y)] = ConvertTo-Color '#d9d4c3' } }
    }
    Save-Canvas $cv (Join-Path $OutDir 'wraith.png')
}

function Paint-Hat($cv, $model) {
    foreach ($cube in (Get-Cubes $model 'brim')) { Paint-Cube $cv $cube (New-Ramp @('#2b2016', '#4a3826', '#6b5236') 5) 2191 0.7 }
    foreach ($cube in (Get-Cubes $model 'crown')) {
        Paint-Cube $cv $cube (New-Ramp @('#2b2016', '#4a3826', '#6b5236') 5) 2192 0.7
        # a faded red band around the crown
        $faces = Get-CubeFaces $cube
        foreach ($f in 'front', 'back', 'left', 'right') {
            $r = $faces[$f]
            for ($x = 0; $x -lt $r[2]; $x++) { $cv[($r[0] + $x), ($r[1] + $r[3] - 1)] = ConvertTo-Color $(if ($x % 3) { '#7a2320' } else { '#5e1a18' }) }
        }
    }
}

function Draw-Forgotten($Models, [string]$OutDir) {
    $model = $Models.forgotten
    $cv = New-Canvas $model.Width $model.Height
    $bone = New-Ramp @('#8f8a78', '#c4bfac', '#e8e3d0') 6
    foreach ($cube in (Get-Cubes $model 'skull')) { Paint-Cube $cv $cube $bone 2151 0.6 }
    foreach ($cube in (Get-Cubes $model 'bone')) { Paint-Cube $cv $cube $bone 2152 0.5 }
    # body: ribs in a tattered dark-green coat (Quark's Forgotten wears old explorer clothes)
    $body = (Get-Cubes $model 'body')[0]
    Paint-Cube $cv $body (New-Ramp @('#1f2a22', '#36493a', '#506a55') 5) 2153 0.7
    Stamp-Face $cv $body 'front' @(
        '...bb...',
        '..b..b..',
        '..bbbb..',
        '..b..b..',
        '..bbbb..',
        '..b..b..',
        '..bbbb..',
        '...bb...',
        '........',
        '.cc..cc.',
        '........',
        'c.c..c.c') @{ b = '#d6d1be'; c = '#141a15' }
    $skull = (Get-Cubes $model 'skull')[0]
    Stamp-Face $cv $skull 'front' @(
        '........',
        '........',
        '.ooo.ooo',
        '.oxo.oxo',
        '........',
        '...nn...',
        '.tttttt.',
        '..t..t..') @{ o = '#1c1a17'; x = '#e8f6ff'; n = '#3a362e'; t = '#6d6858' }
    Paint-Hat $cv $model
    Save-Canvas $cv (Join-Path $OutDir 'forgotten.png')

    $hm = $Models.forgotten_hat
    $hv = New-Canvas $hm.Width $hm.Height
    Paint-Hat $hv $hm
    Save-Canvas $hv (Join-Path $OutDir 'forgotten_hat_worn.png')
}

function Draw-MobsdeepItems([string]$OutDir) {
    # Heart of Diamond: a faceted diamond heart.
    Save-PixelArt -Path (Join-Path $OutDir 'diamond_heart.png') -Palette @{ o = '#0d3b3a'; d = '#1f8f86'; m = '#3fd1c4'; l = '#9ff5ec'; w = '#ffffff' } -Rows @(
        '................',
        '................',
        '...oooo..oooo...',
        '..odmmmooommdo..',
        '.odmllmmmmlmmdo.',
        '.odmlwmmmmmmmdo.',
        '.odmllmmmmmmmdo.',
        '.oddmmmmmmmmddo.',
        '..oddmmmmmmddo..',
        '...oddmmmmddo...',
        '....oddmmddo....',
        '.....oddddo.....',
        '......oddo......',
        '.......oo.......',
        '................',
        '................')
    # Forgotten Hat: a brimmed hat with a red band.
    Save-PixelArt -Path (Join-Path $OutDir 'forgotten_hat.png') -Palette @{ o = '#1a120b'; d = '#3b2c1d'; m = '#5a4330'; l = '#7a5d43'; r = '#7a2320'; s = '#5e1a18' } -Rows @(
        '................',
        '................',
        '................',
        '.....oooooo.....',
        '....odmmmlmo....',
        '....odmmmmlo....',
        '....odmmmmmo....',
        '....orrsrrro....',
        '..oooddmmmmooo..',
        '.odddmmmmmmllmo.',
        'odmmmmmmmmmmmllo',
        '.oodddddddddddo.',
        '...ooooooooooo..',
        '................',
        '................',
        '................')
    # Soul Bead: a brown glass bead with a trapped pale soul.
    Save-PixelArt -Path (Join-Path $OutDir 'soul_bead.png') -Palette @{ o = '#2a1a12'; d = '#4f3324'; m = '#76513a'; l = '#a37a5c'; s = '#9ff3ff'; w = '#e8ffff' } -Rows @(
        '................',
        '................',
        '................',
        '.....oooooo.....',
        '....odmmmmlo....',
        '...odmmlmmmlo...',
        '...odmssmmmmo...',
        '...odswsmmmmo...',
        '...odmssmmmmo...',
        '...oddmmmmmmo...',
        '...oddddmmmdo...',
        '....odddddddo...',
        '.....oooooo.....',
        '................',
        '................',
        '................')
}

function Draw-MobsdeepParticles([string]$OutDir) {
    # 8x8 sprites: a white spell swirl (Forgotten), a dust mote (soul bead), a pale mist wisp (Wraith).
    Save-PixelArt -Path (Join-Path $OutDir 'lepton_spell.png') -Palette @{ w = '#ffffff'; g = '#c8c8c8' } -Rows @(
        '........', '..wwg...', '.w...w..', '.w.gw.w.', '.w.w..w.', '..g..w..', '...ww...', '........')
    Save-PixelArt -Path (Join-Path $OutDir 'lepton_dust.png') -Palette @{ w = '#ffffff'; g = '#d0d0d0' } -Rows @(
        '........', '........', '...gg...', '..gwwg..', '..gwwg..', '...gg...', '........', '........')
    Save-PixelArt -Path (Join-Path $OutDir 'lepton_mist.png') -Palette @{ w = '#e8f4f2'; g = '#9fb3b0' } -Rows @(
        '........', '...g....', '..gwg...', '..wwwg..', '.gwwwg..', '..gwg...', '...g....', '........')
}

# Draws everything; $Track (scriptblock) is called with each written path (pack script: Add-PackFile).
function Invoke-MobsdeepTextures([scriptblock]$Track = { param($p) $p }) {
    $models = Get-MobsdeepModels
    $ent = Join-Path $script:MdRP 'textures/entity/mobsdeep'
    $items = Join-Path $script:MdRP 'textures/items'
    $part = Join-Path $script:MdRP 'textures/particle'
    Draw-Stoneling $models $ent
    Draw-Foxhound $models $ent
    Draw-Wraith $models $ent
    Draw-Forgotten $models $ent
    Draw-MobsdeepItems $items
    Draw-MobsdeepParticles $part
    foreach ($f in Get-ChildItem $ent -Filter '*.png') { [void](& $Track $f.FullName) }
    foreach ($n in 'diamond_heart', 'forgotten_hat', 'soul_bead') { [void](& $Track (Join-Path $items "$n.png")) }
    foreach ($n in 'lepton_spell', 'lepton_dust', 'lepton_mist') { [void](& $Track (Join-Path $part "$n.png")) }
}

if (-not $NoDraw -and -not $MobsdeepDefineOnly) { Invoke-MobsdeepTextures }
