# U4 bricks textures: original pixel art for MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori and
# DuskboundBlocks. Called by tools/pack/bricks.ps1 (which records every file it writes); it can also
# run on its own:  pwsh -NoProfile -File tools/textures/bricks.ps1
#
# Output: assets/pack/rp/textures/blocks/<name>.png, textures/items/moss_paste.png. Returns the paths.
# Everything is deterministic (seeded noise), drawn here; nothing is traced from game textures.
param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
. "$PSScriptRoot/lib.ps1"

$blocks = Join-Path $Root 'assets/pack/rp/textures/blocks'
$items = Join-Path $Root 'assets/pack/rp/textures/items'
$written = [System.Collections.Generic.List[string]]::new()
function B([string]$Name) { $p = Join-Path $blocks "$Name.png"; $written.Add($p); return $p }
function I([string]$Name) { $p = Join-Path $items "$Name.png"; $written.Add($p); return $p }

# Draws a 16x16 texture from a function ($x, $y, $n = noise 0..1) -> ramp index (-1 = transparent).
function New-FnTexture {
    param([string]$Path, [object[]]$Ramp, [scriptblock]$Fn, [int]$Seed = 1, [int]$Cell = 2)
    $noise = New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves 2
    $g = [int[, ]]::new(16, 16)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $g[$x, $y] = [int](& $Fn $x $y $noise[$x, $y]) } }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path $Path
}

function Clamp([int]$v, [int]$lo, [int]$hi) { return [Math]::Max($lo, [Math]::Min($hi, $v)) }

# Brick wall: $Rows rows of $W-wide bricks, offset every other row; mortar = index 0..1, bricks 2..
function New-Bricks {
    param([string]$Path, [object[]]$Ramp, [int]$Seed, [int]$Rows = 4, [int]$W = 8, [double]$Jitter = 0.7)
    $n = $Ramp.Count; $rowH = [int](16 / $Rows)
    $rng = [System.Random]::new($Seed)
    $shade = @{}; for ($i = 0; $i -lt 64; $i++) { $shade[$i] = $rng.Next(-1, 2) }
    New-FnTexture -Path $Path -Ramp $Ramp -Seed $Seed -Fn {
        param($x, $y, $v)
        $row = [Math]::Floor($y / $rowH); $off = if ($row % 2) { [int]($W / 2) } else { 0 }
        $col = [Math]::Floor((($x + $off) % 16) / $W)
        if (($y % $rowH) -eq ($rowH - 1)) { return [int]($v * 1.99) }
        if (((($x + $off) % $W) -eq ($W - 1))) { return [int]($v * 1.99) }
        $base = 2 + [int](($n - 3) * (0.25 + $v * $Jitter)) + $shade[[int]($row * 4 + $col)]
        if (($y % $rowH) -eq 0) { $base += 1 }                              # lit top edge
        if (((($x + $off) % $W) -eq 0)) { $base += 1 }                      # lit left edge
        return Clamp $base 2 ($n - 1)
    }
}

# --- MoreBrickTypes ------------------------------------------------------------------------------
$sand = New-Ramp '#a8915a', '#c9b47a', '#dccb94', '#ece0b0' 6
$redSand = New-Ramp '#7a3a12', '#a5521c', '#bf6a2a', '#d88a44' 6
$soulSand = New-Ramp '#3a2a1f', '#55402f', '#6c543f', '#86705a' 6
New-Bricks -Path (B 'sandstone_bricks') -Ramp $sand -Seed 401 -Rows 4 -W 8 -Jitter 0.4
New-Bricks -Path (B 'red_sandstone_bricks') -Ramp $redSand -Seed 402 -Rows 4 -W 8 -Jitter 0.4
New-Bricks -Path (B 'soul_sandstone_bricks') -Ramp $soulSand -Seed 403 -Rows 4 -W 8 -Jitter 0.4

$cobble = New-Ramp '#2a2a2a', '#474747', '#626262', '#7c7c7c', '#959595' 7
New-Bricks -Path (B 'cobblestone_bricks') -Ramp $cobble -Seed 404 -Rows 4 -W 8 -Jitter 1.1
# mossy: the same bricks with moss growing from the mortar
$mossBase = Join-Path $blocks 'mossy_cobblestone_bricks.png'
New-Bricks -Path (B 'mossy_cobblestone_bricks') -Ramp $cobble -Seed 404 -Rows 4 -W 8 -Jitter 1.1
$mossNoise = New-NoiseGrid -Seed 405 -Size 16 -Cell 4 -Octaves 2
$bmp = [System.Drawing.Bitmap]::new($mossBase)
$out = [System.Drawing.Bitmap]::new(16, 16, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$moss = @((ConvertTo-Color '#3f5a1e'), (ConvertTo-Color '#557a26'), (ConvertTo-Color '#6c9632'))
for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
        $c = $bmp.GetPixel($x, $y); $m = $mossNoise[$x, $y]
        $mortar = ($y % 4 -eq 3) -or (((($x + $(if ([Math]::Floor($y / 4) % 2) { 4 } else { 0 })) % 8) -eq 7))
        if ($m -gt 0.62 -or ($mortar -and $m -gt 0.42)) { $c = $moss[[Math]::Min(2, [int](($m - 0.4) * 6))] }
        $out.SetPixel($x, $y, $c)
    }
}
$bmp.Dispose(); $out.Save($mossBase, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()

New-Bricks -Path (B 'blue_nether_bricks') -Ramp (New-Ramp '#0c1a24', '#123447', '#1b4a5e', '#2a6a7a' 6) -Seed 406 -Rows 4 -W 8 -Jitter 0.5
New-Bricks -Path (B 'blackstone_bricks') -Ramp (New-Ramp '#0e0c10', '#1e1a22', '#2c2730', '#3f3844' 6) -Seed 407 -Rows 4 -W 8 -Jitter 0.9
New-Bricks -Path (B 'dirt_bricks') -Ramp (New-Ramp '#3d2817', '#5b3d24', '#7a5233', '#94683f' 6) -Seed 408 -Rows 4 -W 8 -Jitter 1.0
New-Bricks -Path (B 'netherrack_bricks') -Ramp (New-Ramp '#3a0f0f', '#5c1717', '#7a2222', '#963434' 6) -Seed 409 -Rows 4 -W 8 -Jitter 1.0

# --- SoulSandstone -------------------------------------------------------------------------------
# side: bright band at the top, layered strata, darker base band (like sandstone)
New-FnTexture -Path (B 'soul_sandstone') -Ramp $soulSand -Seed 410 -Fn {
    param($x, $y, $v)
    if ($y -le 2) { return 4 + [int]($v * 1.9) }
    if ($y -eq 3) { return 2 }
    if ($y -ge 12) { return 1 + [int]($v * 2.5) }
    return 2 + [int]($v * 2.6)
}
New-FnTexture -Path (B 'soul_sandstone_top') -Ramp $soulSand -Seed 411 -Fn { param($x, $y, $v) 2 + [int]($v * 3.9) }
New-FnTexture -Path (B 'soul_sandstone_bottom') -Ramp $soulSand -Seed 412 -Fn {
    param($x, $y, $v); if ((($x * 7 + $y * 3) % 11) -eq 0) { return 0 }; 1 + [int]($v * 3.5)
}
# cut: two large slabs separated by lines, framed edge
New-FnTexture -Path (B 'cut_soul_sandstone') -Ramp $soulSand -Seed 413 -Fn {
    param($x, $y, $v)
    if ($y -eq 0 -or $y -eq 8) { return 5 }
    if ($y -eq 7 -or $y -eq 15) { return 1 }
    return 3 + [int]($v * 1.6)
}
# chiseled: top/bottom bands with a carved emblem (a soul flame)
$chis = @(
    'ffffffffffffffff', 'eeeeeeeeeeeeeeee', 'bbbbbbbbbbbbbbbb', 'cccccccccccccccc',
    'ccccccdcdccccccc', 'cccccdcdcdcccccc', 'ccccdcbbbcdccccc', 'cccdcbaaabcdcccc',
    'cccdcbaaabcdcccc', 'ccccdcbbbcdccccc', 'cccccdcccdcccccc', 'ccccccdddccccccc',
    'cccccccccccccccc', 'bbbbbbbbbbbbbbbb', 'eeeeeeeeeeeeeeee', 'bbbbbbbbbbbbbbbb')
Save-PixelArt -Rows $chis -Path (B 'chiseled_soul_sandstone') -Palette @{
    a = '#5fd6d0'; b = $soulSand[1]; c = $soulSand[3]; d = $soulSand[2]; e = $soulSand[4]; f = $soulSand[5] }
New-FnTexture -Path (B 'smooth_soul_sandstone') -Ramp $soulSand -Seed 414 -Fn { param($x, $y, $v) 3 + [int]($v * 1.4) }

# --- MoreMudBlocks -------------------------------------------------------------------------------
$mud = New-Ramp '#5e4637', '#7a5c49', '#8d6c56', '#a5856c' 6
$carved = @(
    'aaaaaaaaaaaaaaaa', 'aeeeeeeeeeeeeeba', 'aeccccccccccccba', 'aecbbbbbbbbbdcba', 'aecbddddddddecba', 'aecbdcccccccecba',
    'aecbdcbbbbdcecba', 'aecbdcbeedbcecba', 'aecbdcbeedbcecba', 'aecbdcbddddcecba', 'aecbdcccccccecba', 'aecbeeeeeeeeecba',
    'aecdddddddddddba', 'aeccccccccccccba', 'abbbbbbbbbbbbbba', 'aaaaaaaaaaaaaaaa')
Save-PixelArt -Rows $carved -Path (B 'carved_mud_bricks') -Palette @{ a = $mud[0]; b = $mud[1]; c = $mud[3]; d = $mud[2]; e = $mud[5] }
New-FnTexture -Path (B 'mud_pillar') -Ramp $mud -Seed 415 -Fn {
    param($x, $y, $v)
    if ($x -eq 0 -or $x -eq 15) { return 0 }
    if ($x -eq 1 -or $x -eq 14) { return 5 }
    if ($x % 4 -eq 3) { return 1 }
    return 2 + [int]($v * 2.6)
}
New-FnTexture -Path (B 'mud_pillar_top') -Ramp $mud -Seed 416 -Fn {
    param($x, $y, $v)
    $e = [Math]::Min([Math]::Min($x, $y), [Math]::Min(15 - $x, 15 - $y))
    if ($e -eq 0) { return 0 }; if ($e -eq 1) { return 5 }; if ($e -eq 4) { return 1 }
    return 2 + [int]($v * 2.6)
}
# lattice: a mud-brick frame with diagonal criss-cross bars (transparent gaps, cutout)
New-FnTexture -Path (B 'mud_brick_lattice') -Ramp $mud -Seed 417 -Fn {
    param($x, $y, $v)
    if ($x -le 1 -or $y -le 1 -or $x -ge 14 -or $y -ge 14) {
        if ($x -eq 0 -or $y -eq 0) { return 5 }; if ($x -eq 15 -or $y -eq 15) { return 0 }; return 2 + [int]($v * 2.5)
    }
    $d1 = ($x - $y + 16) % 6; $d2 = ($x + $y) % 6
    if ($d1 -le 1 -or $d2 -le 1) { return $(if ($d1 -eq 0 -or $d2 -eq 0) { 4 } else { 1 + [int]($v * 2) }) }
    return -1
}

# --- Midori (green purpur) -----------------------------------------------------------------------
$green = New-Ramp '#3d6b2c', '#5a8f3c', '#77ab4e', '#98c76a' 6
function New-TileBlock([string]$Path, [object[]]$Ramp, [int]$Seed) {
    # four square tiles with raised rims, like a purpur block
    New-FnTexture -Path $Path -Ramp $Ramp -Seed $Seed -Fn {
        param($x, $y, $v)
        $lx = $x % 8; $ly = $y % 8
        if ($lx -eq 7 -or $ly -eq 7) { return 0 }
        if ($lx -eq 0 -or $ly -eq 0) { return 4 + [int]($v * 1.5) }
        if ($lx -eq 6 -or $ly -eq 6) { return 1 }
        return 2 + [int]($v * 1.9)
    }
}
New-TileBlock (B 'midori_block') $green 418
New-FnTexture -Path (B 'midori_pillar') -Ramp $green -Seed 419 -Fn {
    param($x, $y, $v)
    if ($x -eq 0 -or $x -eq 15) { return 0 }
    if ($x -eq 1 -or $x -eq 14 -or $x -eq 7) { return 5 }
    if ($x -eq 6 -or $x -eq 13) { return 1 }
    return 2 + [int]($v * 2)
}
New-FnTexture -Path (B 'midori_pillar_top') -Ramp $green -Seed 420 -Fn {
    param($x, $y, $v)
    $e = [Math]::Min([Math]::Min($x, $y), [Math]::Min(15 - $x, 15 - $y))
    if ($e -eq 0) { return 0 }; if ($e -eq 1 -or $e -eq 5) { return 5 }; if ($e -eq 4) { return 1 }
    return 2 + [int]($v * 2)
}
# moss paste item: a soft green blob
$paste = @(
    '................', '................', '................', '.......aa.......', '.....aabbaa.....', '....abbccbba....',
    '...abccddccba...', '...abcddddcba...', '..abccddddccba..', '..abccccccccba..', '...abbccccbba...', '....aabbbbaa....',
    '......aaaa......', '................', '................', '................')
Save-PixelArt -Rows $paste -Path (I 'moss_paste') -Palette @{ a = '#2f4f1c'; b = '#4f7a2a'; c = '#6f9c3a'; d = '#9cc95a' }

# --- DuskboundBlocks -----------------------------------------------------------------------------
$dusk = New-Ramp '#160f24', '#24183a', '#33244f', '#463468' 6
New-TileBlock (B 'duskbound_block') $dusk 421
$lantern = @(
    'aaaaaaaaaaaaaaaa', 'abbbbbbbbbbbbbba', 'abccccccccccccba', 'abcddddddddddcba', 'abcdeeeeeeeedcba', 'abcdeffffffedcba',
    'abcdefggggfedcba', 'abcdefghhgfedcba', 'abcdefghhgfedcba', 'abcdefggggfedcba', 'abcdeffffffedcba', 'abcdeeeeeeeedcba',
    'abcddddddddddcba', 'abccccccccccccba', 'abbbbbbbbbbbbbba', 'aaaaaaaaaaaaaaaa')
Save-PixelArt -Rows $lantern -Path (B 'duskbound_lantern') -Palette @{
    a = $dusk[0]; b = $dusk[3]; c = $dusk[1]; d = '#5b3f8a'; e = '#8a5cc4'; f = '#b98ae6'; g = '#dcbcff'; h = '#ffffff' }

$written
