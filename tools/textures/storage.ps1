# U8 storage textures (original pixel art, drawn procedurally / from rows; nothing traced).
#
#   . tools/textures/storage.ps1
#   $paths = Write-StorageTextures -BlockDir <rp>/textures/blocks/storage -ItemDir <rp>/textures/items/storage
#
# Returns every PNG written (the pack script records them for Start-PackUnit). Deterministic: the
# same input gives byte-identical files.
. "$PSScriptRoot/lib.ps1"

# ---- a tiny canvas ------------------------------------------------------------------------------

function New-Canvas([int]$W = 16, [int]$H = 16) {
    $c = [System.Drawing.Color[, ]]::new($W, $H)
    for ($y = 0; $y -lt $H; $y++) { for ($x = 0; $x -lt $W; $x++) { $c[$x, $y] = [System.Drawing.Color]::Transparent } }
    return , $c
}
function Set-Px($C, [int]$X, [int]$Y, $Color) {
    if ($X -lt 0 -or $Y -lt 0 -or $X -ge $C.GetLength(0) -or $Y -ge $C.GetLength(1)) { return }
    $C[$X, $Y] = ConvertTo-Color $Color
}
function Fill-Rect($C, [int]$X0, [int]$Y0, [int]$X1, [int]$Y1, $Color) {
    for ($y = $Y0; $y -le $Y1; $y++) { for ($x = $X0; $x -le $X1; $x++) { Set-Px $C $x $y $Color } }
}
# Draws rows (palette keys, '.' = keep) with the top-left corner at (X, Y).
function Draw-Rows($C, [string[]]$Rows, [hashtable]$Palette, [int]$X = 0, [int]$Y = 0) {
    for ($j = 0; $j -lt $Rows.Count; $j++) {
        for ($i = 0; $i -lt $Rows[$j].Length; $i++) {
            $k = [string]$Rows[$j][$i]
            if ($k -eq '.') { continue }
            if (-not $Palette.ContainsKey($k)) { throw "Draw-Rows: no palette entry for '$k'" }
            Set-Px $C ($X + $i) ($Y + $j) $Palette[$k]
        }
    }
}
function Save-Canvas($C, [string]$Path) {
    $w = $C.GetLength(0); $h = $C.GetLength(1)
    $bmp = [System.Drawing.Bitmap]::new($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $bmp.SetPixel($x, $y, $C[$x, $y]) } }
        $dir = Split-Path -Parent $Path
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
    return $Path
}
# Noise fill of a rectangle with a ramp (index from noise).
function Fill-Noise($C, [int]$X0, [int]$Y0, [int]$X1, [int]$Y1, [object[]]$Ramp, [int]$Seed, [int]$Cell = 4) {
    $n = New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves 2
    $g = ConvertTo-IndexGrid $n $Ramp.Count
    for ($y = $Y0; $y -le $Y1; $y++) { for ($x = $X0; $x -le $X1; $x++) { Set-Px $C $x $y $Ramp[$g[($x % 16), ($y % 16)]] } }
}

# ---- shared palettes ----------------------------------------------------------------------------

# Dye colours (Java DyeColor texture colours), in creative order.
$script:StorageDyes = [ordered]@{
    white = '#f9fffe'; light_gray = '#9d9d97'; gray = '#474f52'; black = '#1d1d21'; brown = '#835432'; red = '#b02e26'
    orange = '#f9801d'; yellow = '#fed83d'; lime = '#80c71f'; green = '#5e7c16'; cyan = '#169c9c'; light_blue = '#3ab3da'
    blue = '#3c44aa'; purple = '#8932b8'; magenta = '#c74ebd'; pink = '#f38baa'
}
$script:CrateWood = @{ d = '#4a3219'; m = '#7a5530'; l = '#9e7443'; h = '#b98c55' }

# ---- crates -------------------------------------------------------------------------------------

# 4x4 food sprites for crate tops / side gaps ('.' shows the crate floor).
$script:FoodSprites = @{
    apple         = @{ rows = @('.sl.', 'rrhr', 'rrrr', '.rr.'); pal = @{ s = '#5a3a1a'; l = '#4f9a2a'; r = '#c4212a'; h = '#f27a72' } }
    golden_apple  = @{ rows = @('.sl.', 'gghg', 'gggd', '.gd.'); pal = @{ s = '#5a3a1a'; l = '#4f9a2a'; g = '#f2c230'; h = '#fff3a0'; d = '#c4901a' } }
    potato        = @{ rows = @('.pp.', 'phpp', 'ppdp', '.pd.'); pal = @{ p = '#c8a24e'; h = '#e2c47a'; d = '#957234' } }
    carrot        = @{ rows = @('..ll', '.oho', 'ood.', 'od..'); pal = @{ l = '#4f9a2a'; o = '#f08a1e'; h = '#ffb45a'; d = '#c0601a' } }
    golden_carrot = @{ rows = @('..ll', '.gyg', 'ggd.', 'gd..'); pal = @{ l = '#4f9a2a'; g = '#f2c230'; y = '#fff3a0'; d = '#c4901a' } }
    beetroot      = @{ rows = @('.ll.', 'bbhb', 'bbbd', '.bd.'); pal = @{ l = '#4f9a2a'; b = '#8e1f2e'; h = '#c7465a'; d = '#5a1020' } }
}

function Write-CrateTextures([string]$Dir, [string]$Name, [int]$Seed) {
    $sp = $script:FoodSprites[$Name]
    $w = $script:CrateWood
    $floor = '#2e1f10'
    $out = @()
    # top: frame + 3x3 sprites
    $c = New-Canvas
    Fill-Rect $c 0 0 15 15 $w.m
    Fill-Rect $c 1 1 14 14 $floor
    for ($i = 0; $i -lt 16; $i++) { Set-Px $c $i 0 $w.l; Set-Px $c 0 $i $w.l; Set-Px $c $i 15 $w.d; Set-Px $c 15 $i $w.d }
    $rng = [System.Random]::new($Seed)
    foreach ($gy in 0, 1, 2) { foreach ($gx in 0, 1, 2) {
            $ox = 2 + $gx * 4 + $rng.Next(0, 2) - ($gx -eq 2 ? 1 : 0); $oy = 2 + $gy * 4 + $rng.Next(0, 2) - ($gy -eq 2 ? 1 : 0)
            Draw-Rows $c $sp.rows $sp.pal $ox $oy } }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_crate_top.png")
    # side: frame, two slats, food visible through the gaps
    $c = New-Canvas
    Fill-Rect $c 0 0 15 15 $floor
    foreach ($gx in 0, 1, 2, 3) { Draw-Rows $c $sp.rows $sp.pal ($gx * 4) 2; Draw-Rows $c $sp.rows $sp.pal ($gx * 4 + 2) 7; Draw-Rows $c $sp.rows $sp.pal ($gx * 4) 11 }
    foreach ($row in 0, 1, 6, 10, 14, 15) { for ($x = 0; $x -lt 16; $x++) { Set-Px $c $x $row ($row -in 0, 6, 10 ? $w.l : $w.m) } }
    foreach ($row in 1, 15) { for ($x = 0; $x -lt 16; $x++) { Set-Px $c $x $row $w.d } }
    for ($y = 0; $y -lt 16; $y++) { Set-Px $c 0 $y $w.m; Set-Px $c 1 $y $w.d; Set-Px $c 14 $y $w.l; Set-Px $c 15 $y $w.m }
    for ($y = 0; $y -lt 16; $y++) { if ($y -in 0, 6, 10) { Set-Px $c 0 $y $w.h; Set-Px $c 15 $y $w.h } }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_crate.png")
    # bottom: plain boards
    $c = New-Canvas
    $rng2 = [System.Random]::new($Seed + 100)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) {
            $v = if ($y % 4 -eq 3) { $w.d } elseif ($rng2.NextDouble() -lt 0.2) { $w.l } else { $w.m }
            Set-Px $c $x $y $v } }
    for ($i = 0; $i -lt 16; $i++) { Set-Px $c $i 0 $w.l; Set-Px $c 0 $i $w.l; Set-Px $c $i 15 $w.d; Set-Px $c 15 $i $w.d }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_crate_bottom.png")
    return $out
}

# ---- sacks --------------------------------------------------------------------------------------

$script:SackContents = @{
    cocoa_beans = @('#4a2a14', '#6b3d1e', '#8a5530', '#a8713f')
    nether_wart = @('#5a0c12', '#7d1620', '#a3232c', '#c53a3a')
    gunpowder   = @('#3a3a3a', '#545454', '#707070', '#8e8e8e')
    berry       = @('#5a0f1e', '#9c1a2c', '#d0283c', '#ff6a6a')
    glowberry   = @('#a85a10', '#e08a20', '#ffb43a', '#fff07a')
}
$script:Burlap = @('#6e5634', '#8a6d43', '#a4865a', '#bb9f70')

function Write-SackTextures([string]$Dir, [string]$Name, [int]$Seed) {
    $ramp = $script:SackContents[$Name]; $b = $script:Burlap
    $out = @()
    $weave = {
        param($c, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [int]$s)
        $rng = [System.Random]::new($s)
        for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) {
                $i = if ((($x + $y) % 2) -eq 0) { 2 } else { 1 }
                if ($rng.NextDouble() -lt 0.15) { $i += ($rng.Next(0, 2) * 2 - 1) }
                Set-Px $c $x $y $b[[Math]::Max(0, [Math]::Min(3, $i))] } }
    }
    # side: burlap with a rolled rim at the top and the contents bulging out of the opening
    $c = New-Canvas
    & $weave $c 0 0 15 15 $Seed
    Fill-Noise $c 0 0 15 1 $ramp ($Seed + 7) 2
    for ($x = 0; $x -lt 16; $x++) { Set-Px $c $x 2 $b[3]; Set-Px $c $x 3 $b[0] }
    for ($y = 4; $y -lt 16; $y++) { Set-Px $c 0 $y $b[0]; Set-Px $c 15 $y $b[0] }
    for ($x = 0; $x -lt 16; $x++) { Set-Px $c $x 15 $b[0] }
    foreach ($x in 3, 4, 11, 12) { Set-Px $c $x 9 $b[3] }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_sack.png")
    # top: a burlap ring around the contents
    $c = New-Canvas
    Fill-Noise $c 0 0 15 15 $ramp ($Seed + 3) 2
    for ($i = 0; $i -lt 16; $i++) {
        foreach ($p in @(@($i, 0), @($i, 1), @($i, 14), @($i, 15), @(0, $i), @(1, $i), @(14, $i), @(15, $i))) { Set-Px $c $p[0] $p[1] $b[2] }
    }
    for ($i = 1; $i -lt 15; $i++) { Set-Px $c $i 1 $b[3]; Set-Px $c 1 $i $b[3]; Set-Px $c $i 14 $b[0]; Set-Px $c 14 $i $b[0] }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_sack_top.png")
    # bottom: burlap with a seam
    $c = New-Canvas
    & $weave $c 0 0 15 15 ($Seed + 11)
    for ($i = 0; $i -lt 16; $i++) { Set-Px $c $i 7 $b[0]; if ($i % 2) { Set-Px $c $i 8 $b[3] } }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_sack_bottom.png")
    return $out
}

# ---- pillars (bundles) --------------------------------------------------------------------------

function Write-PillarTextures([string]$Dir, [string]$Name, [object[]]$Ramp, [string]$Band, [int]$Seed, [string]$Style) {
    $out = @()
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    # side: vertical stalks (3 px wide with a dark gap), nodes / spines per style, a tying band
    $c = New-Canvas
    for ($s = 0; $s -lt 4; $s++) {
        $x0 = $s * 4; $node = $rng.Next(0, 5)
        for ($y = 0; $y -lt 16; $y++) {
            for ($dx = 0; $dx -lt 4; $dx++) {
                $x = $x0 + $dx
                $v = switch ($dx) { 0 { 1 } 1 { $n - 1 } 2 { $n - 2 } 3 { 0 } }
                if ($Style -eq 'cane' -and (($y + $node) % 6 -eq 0) -and $dx -lt 3) { $v = 0 }
                if ($Style -eq 'cactus' -and (($y + $node) % 4 -eq 0) -and $dx -eq 1) { $v = 0 }
                if ($Style -eq 'cactus' -and (($y + $node) % 4 -eq 2) -and $dx -eq 2) { Set-Px $c $x $y '#e9f2c0'; continue }
                if ($Style -eq 'chorus' -and $rng.NextDouble() -lt 0.25) { $v = $rng.Next(0, $n) }
                if ($Style -eq 'stick' -and $dx -lt 3 -and $rng.NextDouble() -lt 0.12) { $v = [Math]::Max(0, $v - 1) }
                Set-Px $c $x $y $Ramp[[Math]::Max(0, [Math]::Min($n - 1, $v))]
            }
        }
    }
    if ($Band) {
        foreach ($y in 3, 12) { for ($x = 0; $x -lt 16; $x++) { Set-Px $c $x $y $Band; Set-Px $c $x ($y + 1) (Shade-Color $Band 0.7) } }
    }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_block.png")
    # top: the cut ends of the stalks (2x2 blocks of rings)
    $c = New-Canvas
    Fill-Rect $c 0 0 15 15 $Ramp[0]
    foreach ($gy in 0, 1, 2, 3) { foreach ($gx in 0, 1, 2, 3) {
            $ox = $gx * 4; $oy = $gy * 4
            $rows = if ($Style -eq 'chorus') { @('.aa.', 'abba', 'abba', '.aa.') } else { @('.aa.', 'abca', 'acba', '.aa.') }
            $pal = @{ a = $Ramp[$n - 2]; b = $Ramp[$n - 1]; c = $Ramp[1] }
            if ($Style -eq 'stick') { $pal = @{ a = $Ramp[1]; b = '#c9a36a'; c = '#a8834f' } }
            Draw-Rows $c $rows $pal $ox $oy } }
    $out += Save-Canvas $c (Join-Path $Dir "${Name}_block_top.png")
    return $out
}

# ---- single blocks ------------------------------------------------------------------------------

function Write-CharcoalBlock([string]$Dir) {
    $ramp = @('#121010', '#1c1917', '#272320', '#332d29', '#433a33')
    $c = New-Canvas
    $rng = [System.Random]::new(801)
    for ($x = 0; $x -lt 16; $x++) {
        $v = $rng.Next(1, 4)
        for ($y = 0; $y -lt 16; $y++) {
            if ($rng.NextDouble() -lt 0.3) { $v = [Math]::Max(0, [Math]::Min(3, $v + $rng.Next(-1, 2))) }
            Set-Px $c $x $y $ramp[$v]
        }
    }
    foreach ($p in @(@(3, 4), @(9, 2), @(12, 10), @(5, 12), @(14, 6), @(7, 8))) { Set-Px $c $p[0] $p[1] $ramp[4] }
    # board-like seams of the charred wood
    foreach ($y in 5, 11) { for ($x = 0; $x -lt 16; $x++) { if ($rng.NextDouble() -lt 0.7) { Set-Px $c $x $y $ramp[0] } } }
    return Save-Canvas $c (Join-Path $Dir 'charcoal_block.png')
}

function Write-BlazeLantern([string]$Dir) {
    $c = New-Canvas
    $glow = @('#ff8a10', '#ffa82a', '#ffc94a', '#ffe680', '#fff6c0')
    Fill-Noise $c 0 0 15 15 $glow 802 4
    $rod = @{ d = '#a8520c'; m = '#e08a1c'; l = '#ffc63a'; h = '#fff09a' }
    for ($i = 0; $i -lt 16; $i++) {
        Set-Px $c $i 0 $rod.l; Set-Px $c $i 1 $rod.m; Set-Px $c $i 15 $rod.d; Set-Px $c $i 14 $rod.m
        Set-Px $c 0 $i $rod.l; Set-Px $c 1 $i $rod.m; Set-Px $c 15 $i $rod.d; Set-Px $c 14 $i $rod.m
    }
    foreach ($p in @(@(0, 0), @(15, 0), @(0, 15), @(15, 15))) { Set-Px $c $p[0] $p[1] $rod.h }
    # bright core
    Draw-Rows $c @('..ab..', '.abba.', 'abccba', 'abccba', '.abba.', '..ab..') @{ a = $glow[2]; b = $glow[3]; c = $glow[4] } 5 5
    return Save-Canvas $c (Join-Path $Dir 'blaze_lantern.png')
}

function Write-BondedTexture([string]$Dir, [string]$Name, [object[]]$Ramp, [string]$Stitch, [string]$Spot, [int]$Seed) {
    $c = New-Canvas
    Fill-Noise $c 0 0 15 15 $Ramp $Seed 4
    # patch seams with stitches
    for ($i = 0; $i -lt 16; $i++) {
        Set-Px $c $i 7 $Ramp[0]; Set-Px $c 7 $i $Ramp[0]
        if ($i % 3 -eq 1) { Set-Px $c $i 6 $Stitch; Set-Px $c $i 8 $Stitch; Set-Px $c 6 $i $Stitch; Set-Px $c 8 $i $Stitch }
    }
    if ($Spot) {
        $rng = [System.Random]::new($Seed + 1)
        for ($k = 0; $k -lt 7; $k++) { $x = $rng.Next(0, 16); $y = $rng.Next(0, 16); if ($x -ne 7 -and $y -ne 7) { Set-Px $c $x $y $Spot; Set-Px $c ($x + 1) $y $Spot } }
    }
    return Save-Canvas $c (Join-Path $Dir "$Name.png")
}

# ---- stools -------------------------------------------------------------------------------------
# Atlas (matches the UVs of storage_stool geometry):
#   (0,0)-(8,4)   cushion side       (8,0)-(16,8)  cushion top       (8,8)-(16,16) underside (wood)
#   (0,8)-(4,16)  leg side           (4,8)-(8,12)  leg bottom

function Write-StoolTexture([string]$Dir, [string]$Color, [string]$Base, [int]$Seed) {
    $c = New-Canvas
    $hi = Shade-Color $Base 1.18; $mid = ConvertTo-Color $Base; $lo = Shade-Color $Base 0.78; $dk = Shade-Color $Base 0.6
    if ($Color -eq 'black') { $hi = '#3a3a42'; $lo = '#141418'; $dk = '#0a0a0c' }
    if ($Color -eq 'white') { $mid = '#e9eeee'; $lo = '#c9d0d0'; $dk = '#aab2b2'; $hi = '#ffffff' }
    $wood = @{ d = '#4f3820'; m = '#6e5030'; l = '#8a683f' }
    # cushion side
    for ($x = 0; $x -lt 8; $x++) { Set-Px $c $x 0 $hi; Set-Px $c $x 1 $mid; Set-Px $c $x 2 (($x % 2) ? $mid : $lo); Set-Px $c $x 3 $wood.m }
    # cushion top: quilted, with a button
    $rng = [System.Random]::new($Seed)
    for ($y = 0; $y -lt 8; $y++) { for ($x = 8; $x -lt 16; $x++) { Set-Px $c $x $y ($rng.NextDouble() -lt 0.2 ? $lo : $mid) } }
    for ($i = 8; $i -lt 16; $i++) { Set-Px $c $i 0 $hi; Set-Px $c 8 ($i - 8) $hi; Set-Px $c $i 7 $lo; Set-Px $c 15 ($i - 8) $lo }
    Fill-Rect $c 11 3 12 4 $dk
    # underside
    for ($y = 8; $y -lt 16; $y++) { for ($x = 8; $x -lt 16; $x++) { Set-Px $c $x $y (($y % 3) -eq 0 ? $wood.d : $wood.m) } }
    # legs
    for ($y = 8; $y -lt 16; $y++) { Set-Px $c 0 $y $wood.d; Set-Px $c 1 $y $wood.l; Set-Px $c 2 $y $wood.m; Set-Px $c 3 $y $wood.d }
    Fill-Rect $c 4 8 7 11 $wood.m; Set-Px $c 4 8 $wood.l
    return Save-Canvas $c (Join-Path $Dir "${Color}_stool.png")
}

# ---- rope ---------------------------------------------------------------------------------------

function Write-RopeTextures([string]$BlockDir, [string]$ItemDir) {
    $out = @()
    $p = @{ d = '#6b4f2a'; m = '#9c7a45'; l = '#c4a46a'; h = '#dcc391' }
    $c = New-Canvas
    # the column (x 6..9): twisted strands
    for ($y = 0; $y -lt 16; $y++) {
        for ($i = 0; $i -lt 4; $i++) {
            $phase = ($i + $y) % 4
            $col = switch ($phase) { 0 { $p.l } 1 { $p.m } 2 { $p.d } 3 { $p.h } }
            Set-Px $c (6 + $i) $y $col
        }
    }
    # the end of the rope (0..3, 0..3)
    Draw-Rows $c @('dmmd', 'mlhm', 'mhlm', 'dmmd') $p 0 0
    $out += Save-Canvas $c (Join-Path $BlockDir 'rope.png')
    # item: a coil
    $rows = @(
        '................',
        '.....dddddd.....',
        '...ddmmllmmdd...',
        '..dmlhhmmhhlmd..',
        '.dmlmdddddmlmd..',
        '.dlmd.....dmld..',
        'dmld.......dlmd.',
        'dlmd..dmd..dmld.',
        'dmld..dld..dlmd.',
        'dlmd...d...dmld.',
        '.dmld.....dlmd..',
        '.dlmdddddddmld..',
        '..dmlhhmmhhlmd..',
        '...ddmmllmmdd...',
        '.....dddddd.....',
        '................')
    $c = New-Canvas
    Draw-Rows $c $rows $p
    $out += Save-Canvas $c (Join-Path $ItemDir 'rope.png')
    return $out
}

# ---- glass shards / dirty glass -----------------------------------------------------------------

function Write-ShardTexture([string]$Dir, [string]$Name, [string]$Base, [int]$Alpha = 0xd0) {
    $m = ConvertTo-Color $Base
    $mid = [System.Drawing.Color]::FromArgb($Alpha, $m.R, $m.G, $m.B)
    $l = Shade-Color $Base 1.45; $lig = [System.Drawing.Color]::FromArgb([Math]::Min(255, $Alpha + 30), $l.R, $l.G, $l.B)
    $d = Shade-Color $Base 0.55; $dk = [System.Drawing.Color]::FromArgb(255, $d.R, $d.G, $d.B)
    $rows = @(
        '................',
        '................',
        '..........o.....',
        '.........olo....',
        '........olmo....',
        '.......olmmo....',
        '......olmmo..o..',
        '.....olmmmo.olo.',
        '....olmmmo..omo.',
        '...olmmmmo.olmo.',
        '..olmmmmo..ommo.',
        '..ommmmo...oo...',
        '..oommo.........',
        '....oo..........',
        '................',
        '................')
    $c = New-Canvas
    Draw-Rows $c $rows @{ o = $dk; l = $lig; m = $mid }
    return Save-Canvas $c (Join-Path $Dir "$Name.png")
}

function Write-DirtyGlass([string]$Dir) {
    $c = New-Canvas
    $frame = '#5c4a30'; $haze = '#4a7a5a32'; $smudge = '#80604a28'; $streak = '#c8d8d0a0'
    Fill-Rect $c 0 0 15 15 $haze
    $rng = [System.Random]::new(803)
    for ($k = 0; $k -lt 26; $k++) { Set-Px $c $rng.Next(1, 15) $rng.Next(1, 15) '#90705a70' }
    for ($k = 0; $k -lt 10; $k++) { $x = $rng.Next(1, 14); $y = $rng.Next(1, 14); Fill-Rect $c $x $y ($x + 1) ($y + 1) $smudge }
    for ($i = 0; $i -lt 16; $i++) { Set-Px $c $i 0 $frame; Set-Px $c 0 $i $frame; Set-Px $c $i 15 $frame; Set-Px $c 15 $i $frame }
    foreach ($p in @(@(3, 3), @(4, 4), @(5, 5), @(3, 5), @(10, 9), @(11, 10))) { Set-Px $c $p[0] $p[1] $streak }
    return Save-Canvas $c (Join-Path $Dir 'dirty_glass.png')
}

# ---- potted plant sprites (16x16, drawn onto the pot's cross planes) ----------------------------

$script:PottedSprites = [ordered]@{
    chorus         = @{ pal = @{ s = '#5e3a63'; m = '#8a5f90'; f = '#c9a3cf'; h = '#efd9f0' }; rows = @(
            '................', '.....fh..hf.....', '.....ff..ff.....', '......m..m......', '...hf.m..m.fh...',
            '...ff.mmmm.ff...', '....m..mm..m....', '....mmm.mmmm....', '.......mm.......', '.......sm.......',
            '.......ms.......', '.......sm.......', '.......ms.......', '.......sm.......', '.......ms.......', '.......sm.......') }
    cocoa_bean     = @{ pal = @{ s = '#4a6a20'; g = '#6a9a2a'; o = '#b8742a'; d = '#7a4a1a'; h = '#d8a050' }; rows = @(
            '................', '................', '.......gg.......', '......gssg......', '.......s........', '......ss........',
            '.....ohs.oh.....', '....oohs.ooh....', '....oods.ood....', '....ddds.ddd....', '.....dd.s.dd....', '........s.......',
            '.......ss.......', '.......s........', '.......s........', '.......s........') }
    melon          = @{ pal = @{ s = '#7a9a2a'; l = '#4f8a2a'; d = '#3a6a1a'; y = '#a8b84a' }; rows = @(
            '................', '................', '.........ll.....', '........llld....', '........ddl.....', '.......s........',
            '......ys........', '.....ys..ll.....', '.....s..llld....', '.....s...ds.....', '.....ss..s......', '......s.ss......',
            '.......ss.......', '.......s........', '.......s........', '.......s........') }
    pumpkin        = @{ pal = @{ s = '#8a9a2a'; l = '#4f8a2a'; d = '#3a6a1a'; o = '#c88a2a' }; rows = @(
            '................', '................', '.....ll.........', '....dlll........', '.....ldd........', '........s.......',
            '........so......', '.....ll..so.....', '....dlll..s.....', '.....ds...s.....', '......s..ss.....', '......ss.s......',
            '.......ss.......', '.......s........', '.......s........', '.......s........') }
    sea_pickle     = @{ pal = @{ g = '#5a7a2a'; m = '#7a9a3a'; l = '#a8c45a'; t = '#e8f8a0' }; rows = @(
            '................', '................', '................', '................', '......t.........', '.....lml........',
            '.....mgm...t....', '.....mgm..lml...', '..t..mgm..mgm...', '.lml.mgm..mgm...', '.mgm.mgm..mgm...', '.mgm.mgm..mgm...',
            '.mgm.mgm..mgm...', '.mgm.mgm..mgm...', '.mgm.mgm..mgm...', '.mgm.mgm..mgm...') }
    sugar_cane     = @{ pal = @{ d = '#5a8a2a'; m = '#82b83a'; l = '#a8d860'; n = '#3e6a1a'; f = '#6aa83a' }; rows = @(
            '.......f........', '......ff..f.....', '..f..ml..ff.....', '..ffml...ml.....', '...ml....ml..f..', '...ml....nn.ff..',
            '...nn....ml.ml..', '...ml....ml.ml..', '...ml....ml.ml..', '...ml....ml.nn..', '...ml....nn.ml..', '...nn....ml.ml..',
            '...ml....ml.ml..', '...ml....ml.ml..', '...ml....ml.ml..', '...ml....ml.ml..') }
    sunflower      = @{ pal = @{ y = '#f8d020'; o = '#e8a010'; b = '#5a3a14'; c = '#7a5020'; s = '#4f8a2a'; l = '#6aa83a' }; rows = @(
            '.....yyyyy......', '....yoyyyoy.....', '...yybbccyyy....', '...yobcbbcoy....', '...yybcbbcyy....', '...yobbccboy....',
            '....yoyyyoy.....', '.....yyyyy......', '.......s........', '...ll..s........', '..llll.s..ll....', '...llls..llll...',
            '.......sslll....', '.......s........', '.......s........', '.......s........') }
    twisting_vines = @{ pal = @{ d = '#0a6a6a'; m = '#14a0a0'; l = '#3ad0c0' }; rows = @(
            '........l.......', '.......lm..l....', '...l...m...ml...', '...ml..md...m...', '....m..m....d...', '....md.ml...m...',
            '.....m..m..mm...', '.....dm.m..m....', '......m.m.md....', '......mmm.m.....', '.......mmmm.....', '.......md.......',
            '.......mm.......', '.......dm.......', '.......mm.......', '.......md.......') }
    weeping_vines  = @{ pal = @{ d = '#6a0a0a'; m = '#a01a1a'; l = '#d03a2a' }; rows = @(
            '................', '................', '.....l....l.....', '....mm..lmm.....', '...md.mmm..m....', '...m...m...md...',
            '...m...m....m...', '...l...md...l...', '.......m........','.......m........', '.......mm.......', '.......dm.......',
            '.......mm.......', '.......md.......', '.......mm.......', '.......dm.......') }
    cave_vines     = @{ pal = @{ d = '#3a6a1a'; m = '#5a8a2a'; l = '#7aaa3a'; b = '#f8a020'; y = '#fff070' }; rows = @(
            '................', '.......l........', '......lm..l.....', '...yb.m..lm.....', '...bb.m..m......', '....m.m..m.yb...',
            '....mmd..m.bb...', '.....m...m.m....', '.....m..mm.m....', '..yb.mm.m.mm....', '..bb..mmm.m.....', '...m...mmm......',
            '...mmm..m.......', '......mmm.......', '.......md.......', '.......mm.......') }
    pitcher_plant  = @{ pal = @{ t = '#2a6a8a'; m = '#3a8aaa'; l = '#6ac0d8'; p = '#9a5ac8'; s = '#4f7a3a'; g = '#6a9a4a' }; rows = @(
            '................', '.....pp..pp.....', '....plp..plp....', '....mllmmllm....', '....tmmllmmt....', '.....tmmmmt.....',
            '.....tmmmmt.....', '......tmmt......', '..gg..tmmt..gg..', '.gggs..tt..sggg.', '..ggss.ss.ssgg..', '....sss..sss....',
            '......ssss......', '.......ss.......', '.......ss.......', '.......ss.......') }
    nether_sprouts = @{ pal = @{ d = '#0a5a52'; m = '#14887a'; l = '#2ab8a0' }; rows = @(
            '................', '................', '................', '................', '................', '................',
            '................', '..l.....l...l...', '..m..l..m...m.l.', '.l.m.m.lm..lm.m.', '.m.m.m.m.m.m..m.', '..mm.mm..mm..mm.',
            '...m..m...m..m..', '...md.md..md.md.', '...dm.dm..dm.dm.', '...md.md..md.md.') }
    vine           = @{ pal = @{ w = '#6b4f2a'; x = '#8a6a3a'; d = '#2a5a14'; m = '#3e7a20'; l = '#5a9a30' }; rows = @(
            '.......wx.......', '......mwxl......', '.....lmwxml.....', '......dwx.......', '.......wxm......', '.....mmwxml.....',
            '....lmdwx.......', '.......wxdm.....', '.......wxmml....', '.....mlwx.......', '....mmdwx.......', '.......wxm......',
            '.......wxmml....', '......mwx.......', '.......wx.......', '.......wx.......') }
}

function Write-PottedSprites([string]$Dir) {
    $out = @()
    foreach ($kv in $script:PottedSprites.GetEnumerator()) {
        $rows = @($kv.Value.rows | ForEach-Object { $_.PadRight(16, '.').Substring(0, 16) })
        if ($rows.Count -ne 16) { throw "potted sprite $($kv.Key): $($rows.Count) rows" }
        $c = New-Canvas
        Draw-Rows $c $rows $kv.Value.pal
        $out += Save-Canvas $c (Join-Path $Dir "potted_$($kv.Key).png")
    }
    return $out
}

# ---- everything ---------------------------------------------------------------------------------

function Write-StorageTextures {
    param([Parameter(Mandatory)][string]$BlockDir, [Parameter(Mandatory)][string]$ItemDir)
    $out = @()
    $seed = 810
    foreach ($n in 'apple', 'golden_apple', 'potato', 'carrot', 'golden_carrot', 'beetroot') { $out += Write-CrateTextures $BlockDir $n ($seed++) }
    foreach ($n in 'cocoa_beans', 'nether_wart', 'gunpowder', 'berry', 'glowberry') { $out += Write-SackTextures $BlockDir $n ($seed++) }
    $out += Write-PillarTextures $BlockDir 'sugar_cane' @('#3e6a1a', '#5a8a2a', '#82b83a', '#a8d860') '#8a6a3a' 830 'cane'
    $out += Write-PillarTextures $BlockDir 'cactus' @('#1e4a14', '#2e6a1e', '#3e8a28', '#5aa83a') '' 831 'cactus'
    $out += Write-PillarTextures $BlockDir 'chorus_fruit' @('#4a2a4e', '#6e4a72', '#8f6a93', '#b48fb8', '#d2b4d4') '' 832 'chorus'
    $out += Write-PillarTextures $BlockDir 'stick' @('#3a2812', '#5a4022', '#7a5a32', '#9a7444') '#c9b48a' 833 'stick'
    $out += Write-CharcoalBlock $BlockDir
    $out += Write-BlazeLantern $BlockDir
    $out += Write-BondedTexture $BlockDir 'bonded_leather' @('#6a3a1e', '#84492a', '#9a5a32', '#b06a3a') '#e0c090' '' 840
    $out += Write-BondedTexture $BlockDir 'bonded_rabbit_hide' @('#b8a080', '#cdb898', '#ddc9aa', '#efe2cc') '#8a6a4a' '#9a7a52' 841
    $s = 850
    foreach ($kv in $script:StorageDyes.GetEnumerator()) { $out += Write-StoolTexture $BlockDir $kv.Key $kv.Value ($s++) }
    $out += Write-RopeTextures $BlockDir $ItemDir
    $out += Write-ShardTexture $ItemDir 'clear_shard' '#d8f0f0' 0xb0
    $out += Write-ShardTexture $ItemDir 'dirty_shard' '#8a7050' 0xc8
    foreach ($kv in $script:StorageDyes.GetEnumerator()) { $out += Write-ShardTexture $ItemDir "$($kv.Key)_shard" $kv.Value }
    $out += Write-DirtyGlass $BlockDir
    $out += Write-PottedSprites $BlockDir
    return $out
}
