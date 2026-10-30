# Pixel-art helpers for Lepton textures (PowerShell 7 + System.Drawing, no Python).
#
# Dot-source it:  . "$PSScriptRoot/lib.ps1"
#
# A texture is a list of equal-length strings ("rows"); each character is a palette key.
# '.' (or any key mapped to $null / 'transparent') is a transparent pixel.
# PowerShell hashtable literals are case-insensitive: don't use keys that differ only by case.
#
#   $rows = @(
#       '..oo..',
#       '.oMMo.'
#   )
#   $pal  = @{ o = '#3b2412'; M = '#8b5a2b' }
#   Save-PixelArt -Rows $rows -Palette $pal -Path 'assets/pack/rp/textures/items/foo.png'
#
# Helpers:
#   ConvertTo-Color '#rrggbb' | '#aarrggbb' | [Drawing.Color]
#   Shade-Color $color 0.8     -> darker (factor < 1) / lighter (factor > 1)
#   Mix-Color $a $b 0.5        -> linear blend
#   Save-PixelArt -Rows -Palette -Path [-Scale n]   (Scale > 1 writes an enlarged preview)
#   Merge-Rows $base $overlay  -> overlay's non-'.' chars replace base chars (same size)

Add-Type -AssemblyName System.Drawing

function ConvertTo-Color($value) {
    if ($null -eq $value) { return [System.Drawing.Color]::Transparent }
    if ($value -is [System.Drawing.Color]) { return $value }
    $s = "$value".Trim()
    if ($s -eq 'transparent') { return [System.Drawing.Color]::Transparent }
    if ($s.StartsWith('#')) { $s = $s.Substring(1) }
    if ($s.Length -eq 6) { $s = 'FF' + $s }
    if ($s.Length -ne 8) { throw "Bad color '$value' (use #rrggbb or #aarrggbb)" }
    return [System.Drawing.Color]::FromArgb([Convert]::ToInt32($s, 16))
}

function Clamp255([double]$v) { return [int][Math]::Max(0, [Math]::Min(255, [Math]::Round($v))) }

function Shade-Color($color, [double]$factor) {
    $c = ConvertTo-Color $color
    if ($factor -le 1) {
        return [System.Drawing.Color]::FromArgb($c.A, (Clamp255 ($c.R * $factor)), (Clamp255 ($c.G * $factor)), (Clamp255 ($c.B * $factor)))
    }
    $t = $factor - 1
    return [System.Drawing.Color]::FromArgb($c.A, (Clamp255 ($c.R + (255 - $c.R) * $t)), (Clamp255 ($c.G + (255 - $c.G) * $t)), (Clamp255 ($c.B + (255 - $c.B) * $t)))
}

function Mix-Color($a, $b, [double]$t) {
    $x = ConvertTo-Color $a; $y = ConvertTo-Color $b
    return [System.Drawing.Color]::FromArgb(
        (Clamp255 ($x.A + ($y.A - $x.A) * $t)), (Clamp255 ($x.R + ($y.R - $x.R) * $t)),
        (Clamp255 ($x.G + ($y.G - $x.G) * $t)), (Clamp255 ($x.B + ($y.B - $x.B) * $t)))
}

function Merge-Rows([string[]]$Base, [string[]]$Overlay) {
    if ($Base.Count -ne $Overlay.Count) { throw "Merge-Rows: row count differs" }
    $out = for ($y = 0; $y -lt $Base.Count; $y++) {
        $b = $Base[$y].ToCharArray(); $o = $Overlay[$y]
        for ($x = 0; $x -lt $b.Length; $x++) { if ($x -lt $o.Length -and $o[$x] -ne '.') { $b[$x] = $o[$x] } }
        -join $b
    }
    return , $out
}

function Save-PixelArt {
    param(
        [Parameter(Mandatory)][string[]]$Rows,
        [Parameter(Mandatory)][hashtable]$Palette,
        [Parameter(Mandatory)][string]$Path,
        [int]$Scale = 1
    )
    $h = $Rows.Count
    $w = ($Rows | Measure-Object -Property Length -Maximum).Maximum
    foreach ($r in $Rows) { if ($r.Length -ne $w) { throw "Save-PixelArt: all rows must have length $w ('$r')" } }
    $colors = [System.Collections.Generic.Dictionary[string, System.Drawing.Color]]::new([StringComparer]::Ordinal)
    foreach ($k in $Palette.Keys) { $colors[[string]$k] = ConvertTo-Color $Palette[$k] }
    $bmp = New-Object System.Drawing.Bitmap ($w * $Scale), ($h * $Scale), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $ch = [string]$Rows[$y][$x]
                if ($ch -eq '.') { $c = [System.Drawing.Color]::Transparent }
                elseif ($colors.ContainsKey($ch)) { $c = $colors[$ch] }
                else { throw "Save-PixelArt: no palette entry for '$ch' (row $y)" }
                for ($sy = 0; $sy -lt $Scale; $sy++) { for ($sx = 0; $sx -lt $Scale; $sx++) { $bmp.SetPixel($x * $Scale + $sx, $y * $Scale + $sy, $c) } }
            }
        }
        $dir = Split-Path -Parent $Path
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
}

# ---------------------------------------------------------------------------------------------
# Procedural helpers (deterministic: every generator takes a -Seed, same seed = same pixels).
#
#   New-Ramp '#2b2b2b','#8a8a8a' 6           -> 6 colours from dark to light ('#rrggbb' strings)
#   New-Ramp '#3b2412','#8b5a2b','#d9a066' 8 -> multi-stop ramp
#   Get-RampColor $ramp 0.4                  -> the ramp colour at t (0..1)
#   ConvertTo-Hex $color                     -> '#rrggbb' (or '#aarrggbb' when not opaque)
#   ConvertTo-Hsl $color / ConvertFrom-Hsl h s l [a]   (h 0..360, s/l 0..1)
#   Shift-Hue $color 30 [-Saturation 1.0] [-Lightness 1.0]  -> recoloured colour
#   Convert-PngColors -Source a.png -Path b.png -Map { param($c) Shift-Hue $c 120 }
#   Save-IndexGrid -Grid $g -Ramp $ramp -Path p.png   ($g: int[,] of ramp indices, -1 = transparent)
#   New-NoiseGrid -Seed 1 [-Size 16] [-Cell 4] [-Octaves 2]  -> double[,] in 0..1 (tileable value noise)
#   New-StoneTexture   -Path -Ramp [-Seed] [-Size 16]
#   New-BrickTexture   -Path -BrickRamp -MortarRamp [-Seed] [-Rows 4] [-BrickWidth 8]
#   New-PlankTexture   -Path -Ramp [-Seed] [-Boards 4]
#   New-LogSideTexture -Path -Ramp [-Seed]
#   New-LogTopTexture  -Path -BarkRamp -WoodRamp [-Seed]
# The patterns come from seeded noise; they are original art, never traced from game textures.
# ---------------------------------------------------------------------------------------------

function ConvertTo-Hex($color) {
    $c = ConvertTo-Color $color
    if ($c.A -eq 255) { return ('#{0:x2}{1:x2}{2:x2}' -f $c.R, $c.G, $c.B) }
    return ('#{0:x2}{1:x2}{2:x2}{3:x2}' -f $c.A, $c.R, $c.G, $c.B)
}

function Get-RampColor([object[]]$Ramp, [double]$t) {
    $t = [Math]::Max(0.0, [Math]::Min(1.0, $t))
    if ($Ramp.Count -eq 1) { return ConvertTo-Hex $Ramp[0] }
    $pos = $t * ($Ramp.Count - 1)
    $i = [Math]::Min([int][Math]::Floor($pos), $Ramp.Count - 2)
    return ConvertTo-Hex (Mix-Color $Ramp[$i] $Ramp[$i + 1] ($pos - $i))
}

function New-Ramp {
    param([Parameter(Mandatory, Position = 0)][object[]]$Stops, [Parameter(Position = 1)][int]$Count = 6)
    if ($Count -lt 2) { return , @(ConvertTo-Hex $Stops[0]) }
    $out = for ($i = 0; $i -lt $Count; $i++) { Get-RampColor $Stops ($i / ($Count - 1)) }
    return , @($out)
}

function ConvertTo-Hsl($color) {
    $c = ConvertTo-Color $color
    $r = $c.R / 255.0; $g = $c.G / 255.0; $b = $c.B / 255.0
    $max = [Math]::Max($r, [Math]::Max($g, $b)); $min = [Math]::Min($r, [Math]::Min($g, $b))
    $l = ($max + $min) / 2; $h = 0.0; $s = 0.0
    if ($max -ne $min) {
        $d = $max - $min
        $s = if ($l -gt 0.5) { $d / (2 - $max - $min) } else { $d / ($max + $min) }
        if ($max -eq $r) { $h = (($g - $b) / $d) + $(if ($g -lt $b) { 6 } else { 0 }) }
        elseif ($max -eq $g) { $h = (($b - $r) / $d) + 2 }
        else { $h = (($r - $g) / $d) + 4 }
        $h *= 60
    }
    return [pscustomobject]@{ H = $h; S = $s; L = $l; A = $c.A }
}

function ConvertFrom-Hsl([double]$H, [double]$S, [double]$L, [int]$A = 255) {
    $H = (($H % 360) + 360) % 360; $S = [Math]::Max(0.0, [Math]::Min(1.0, $S)); $L = [Math]::Max(0.0, [Math]::Min(1.0, $L))
    $c = (1 - [Math]::Abs(2 * $L - 1)) * $S
    $x = $c * (1 - [Math]::Abs((($H / 60) % 2) - 1))
    $m = $L - $c / 2
    $rgb = switch ([int][Math]::Floor($H / 60)) {
        0 { $c, $x, 0 } 1 { $x, $c, 0 } 2 { 0, $c, $x } 3 { 0, $x, $c } 4 { $x, 0, $c } default { $c, 0, $x }
    }
    return [System.Drawing.Color]::FromArgb($A, (Clamp255 (($rgb[0] + $m) * 255)), (Clamp255 (($rgb[1] + $m) * 255)), (Clamp255 (($rgb[2] + $m) * 255)))
}

function Shift-Hue($color, [double]$Degrees, [double]$Saturation = 1.0, [double]$Lightness = 1.0) {
    $h = ConvertTo-Hsl $color
    return ConvertFrom-Hsl ($h.H + $Degrees) ($h.S * $Saturation) ($h.L * $Lightness) $h.A
}

function Convert-PngColors {
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][scriptblock]$Map)
    $src = [System.Drawing.Bitmap]::new($Source)
    $bmp = [System.Drawing.Bitmap]::new($src.Width, $src.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $src.Height; $y++) {
            for ($x = 0; $x -lt $src.Width; $x++) {
                $c = $src.GetPixel($x, $y)
                if ($c.A -eq 0) { $bmp.SetPixel($x, $y, $c); continue }
                $n = ConvertTo-Color (& $Map $c)
                $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c.A, $n.R, $n.G, $n.B))
            }
        }
        $src.Dispose(); $src = $null   # the source may be the destination
        $dir = Split-Path -Parent $Path
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bmp.Dispose()
        if ($src) { $src.Dispose() }
    }
}

function Save-IndexGrid {
    param([Parameter(Mandatory)][int[, ]]$Grid, [Parameter(Mandatory)][object[]]$Ramp, [Parameter(Mandatory)][string]$Path)
    $w = $Grid.GetLength(0); $h = $Grid.GetLength(1)
    $cols = @($Ramp | ForEach-Object { ConvertTo-Color $_ })
    $bmp = [System.Drawing.Bitmap]::new($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $i = $Grid[$x, $y]
                $c = if ($i -lt 0) { [System.Drawing.Color]::Transparent } else { $cols[[Math]::Min($i, $cols.Count - 1)] }
                $bmp.SetPixel($x, $y, $c)
            }
        }
        $dir = Split-Path -Parent $Path
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
}

# Tileable value noise in 0..1. -Cell is the lattice spacing in pixels (should divide -Size).
function New-NoiseGrid([int]$Seed = 1, [int]$Size = 16, [int]$Cell = 4, [int]$Octaves = 2) {
    $rng = [System.Random]::new($Seed)
    $out = [double[, ]]::new($Size, $Size)
    $amp = 1.0; $total = 0.0
    for ($o = 0; $o -lt $Octaves; $o++) {
        $cell = [Math]::Max(1, [int]($Cell / [Math]::Pow(2, $o)))
        $n = [Math]::Max(1, [int]($Size / $cell))
        $lat = [double[, ]]::new($n, $n)
        for ($i = 0; $i -lt $n; $i++) { for ($j = 0; $j -lt $n; $j++) { $lat[$i, $j] = $rng.NextDouble() } }
        for ($y = 0; $y -lt $Size; $y++) {
            for ($x = 0; $x -lt $Size; $x++) {
                $fx = $x / $cell; $fy = $y / $cell
                $x0 = [int][Math]::Floor($fx) % $n; $y0 = [int][Math]::Floor($fy) % $n
                $x1 = ($x0 + 1) % $n; $y1 = ($y0 + 1) % $n
                $tx = $fx - [Math]::Floor($fx); $ty = $fy - [Math]::Floor($fy)
                $tx = $tx * $tx * (3 - 2 * $tx); $ty = $ty * $ty * (3 - 2 * $ty)
                $a = $lat[$x0, $y0] + ($lat[$x1, $y0] - $lat[$x0, $y0]) * $tx
                $b = $lat[$x0, $y1] + ($lat[$x1, $y1] - $lat[$x0, $y1]) * $tx
                $out[$x, $y] += ($a + ($b - $a) * $ty) * $amp
            }
        }
        $total += $amp; $amp *= 0.5
    }
    for ($y = 0; $y -lt $Size; $y++) { for ($x = 0; $x -lt $Size; $x++) { $out[$x, $y] /= $total } }
    return , $out
}

# Maps 0..1 noise to ramp indices, stretching the contrast so every ramp step appears.
function ConvertTo-IndexGrid([double[, ]]$Noise, [int]$Levels) {
    $w = $Noise.GetLength(0); $h = $Noise.GetLength(1)
    $min = 1.0; $max = 0.0
    foreach ($v in $Noise) { $min = [Math]::Min($min, $v); $max = [Math]::Max($max, $v) }
    $g = [int[, ]]::new($w, $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $t = ($Noise[$x, $y] - $min) / [Math]::Max(1e-6, $max - $min)
            $g[$x, $y] = [Math]::Min($Levels - 1, [int][Math]::Floor($t * $Levels))
        }
    }
    return , $g
}

function New-StoneTexture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$Ramp, [int]$Seed = 1, [int]$Size = 16)
    $noise = New-NoiseGrid -Seed $Seed -Size $Size -Cell 4 -Octaves 3
    Save-IndexGrid -Grid (ConvertTo-IndexGrid $noise $Ramp.Count) -Ramp $Ramp -Path $Path
}

function New-BrickTexture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$BrickRamp, [Parameter(Mandatory)][object[]]$MortarRamp,
        [int]$Seed = 1, [int]$Rows = 4, [int]$BrickWidth = 8, [int]$Size = 16)
    $ramp = @($MortarRamp) + @($BrickRamp)
    $m = $MortarRamp.Count; $b = $BrickRamp.Count
    $g = [int[, ]]::new($Size, $Size)
    $rowH = [int]($Size / $Rows)
    $noise = New-NoiseGrid -Seed $Seed -Size $Size -Cell 2 -Octaves 1
    for ($y = 0; $y -lt $Size; $y++) {
        $row = [int][Math]::Floor($y / $rowH)
        $offset = if ($row % 2) { [int]($BrickWidth / 2) } else { 0 }
        for ($x = 0; $x -lt $Size; $x++) {
            $mortar = ($y % $rowH -eq $rowH - 1) -or ((($x + $offset) % $BrickWidth) -eq $BrickWidth - 1)
            if ($mortar) { $g[$x, $y] = [Math]::Min($m - 1, [int]($noise[$x, $y] * $m)); continue }
            $top = if ($y % $rowH -eq 0) { 0.35 } else { 0.0 }   # lit top edge of each brick
            $g[$x, $y] = $m + [Math]::Min($b - 1, [int](($noise[$x, $y] * 0.65 + $top) * $b))
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

function New-PlankTexture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$Ramp, [int]$Seed = 1, [int]$Boards = 4, [int]$Size = 16)
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    $g = [int[, ]]::new($Size, $Size)
    $boardH = [int]($Size / $Boards)
    for ($board = 0; $board -lt $Boards; $board++) {
        $seam = $rng.Next(2, $Size - 2)
        $base = 1 + $rng.Next(0, [Math]::Max(1, $n - 2))
        for ($dy = 0; $dy -lt $boardH; $dy++) {
            $y = $board * $boardH + $dy
            for ($x = 0; $x -lt $Size; $x++) {
                $v = $base
                if ($dy -eq $boardH - 1) { $v = 0 }                                            # gap between boards
                elseif ($rng.NextDouble() -lt 0.18) { $v = [Math]::Max(1, $v - 1) }              # grain streaks
                elseif ($dy -eq 0 -and $rng.NextDouble() -lt 0.5) { $v = [Math]::Min($n - 1, $v + 1) }
                if ($x -eq $seam -and ($board % 2)) { $v = 0 }                                  # board end
                $g[$x, $y] = $v
            }
        }
    }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path $Path
}

function New-LogSideTexture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$Ramp, [int]$Seed = 1, [int]$Size = 16)
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    $g = [int[, ]]::new($Size, $Size)
    for ($x = 0; $x -lt $Size; $x++) {
        $col = $rng.Next(0, $n)
        for ($y = 0; $y -lt $Size; $y++) {
            if ($rng.NextDouble() -lt 0.25) { $col = [Math]::Max(0, [Math]::Min($n - 1, $col + $rng.Next(-1, 2))) }
            $g[$x, $y] = $col
        }
    }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path $Path
}

function New-LogTopTexture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$BarkRamp, [Parameter(Mandatory)][object[]]$WoodRamp,
        [int]$Seed = 1, [int]$Size = 16)
    $rng = [System.Random]::new($Seed)
    $ramp = @($BarkRamp) + @($WoodRamp)
    $nb = $BarkRamp.Count; $nw = $WoodRamp.Count
    $g = [int[, ]]::new($Size, $Size)
    $c = ($Size - 1) / 2.0
    for ($y = 0; $y -lt $Size; $y++) {
        for ($x = 0; $x -lt $Size; $x++) {
            $edge = [Math]::Min([Math]::Min($x, $y), [Math]::Min($Size - 1 - $x, $Size - 1 - $y))
            if ($edge -eq 0) { $g[$x, $y] = $rng.Next(0, $nb); continue }                     # bark rim
            $r = [Math]::Max([Math]::Abs($x - $c), [Math]::Abs($y - $c))
            $ring = [int][Math]::Floor($r) % 2                                                # growth rings
            $v = [int](($nw - 1) * (0.35 + 0.65 * (1 - $ring * 0.6)))
            if ($rng.NextDouble() -lt 0.1) { $v = [Math]::Max(0, $v - 1) }
            $g[$x, $y] = $nb + [Math]::Min($nw - 1, $v)
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}
