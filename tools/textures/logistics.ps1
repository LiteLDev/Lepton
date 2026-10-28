# Textures of U11 logistics (chute, feeding trough, crate, copper pipes). Original pixel art drawn
# from seeded noise and simple shapes; run it again to regenerate byte-identical PNGs.
#
#   pwsh tools/textures/logistics.ps1
. "$PSScriptRoot/lib.ps1"
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/rp/textures/blocks/logistics'

# Paints a 16x16 index grid: $Fn returns a ramp index (or -1 for transparent) for (x, y, noise).
function Save-Painted([string]$Name, [object[]]$Ramp, [int]$Seed, [scriptblock]$Fn) {
    $noise = New-NoiseGrid -Seed $Seed -Size 16 -Cell 4 -Octaves 2
    $g = [int[, ]]::new(16, 16)
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $v = & $Fn $x $y $noise[$x, $y]
            $g[$x, $y] = [int]$v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path (Join-Path $out "$Name.png")
}

function Clamp([int]$v, [int]$lo, [int]$hi) { return [Math]::Max($lo, [Math]::Min($hi, $v)) }

# ---- wood (oak-like) -------------------------------------------------------------------------------
# 0 deep shadow, 1 shadow, 2..5 wood, 6 highlight, 7 iron dark, 8 iron light
$wood = @('#2e2214', '#4a3820', '#6b5232', '#82663e', '#9a7a4b', '#b08e5a', '#c8a56c', '#3d3f45', '#8d929b')
$plank = {
    param($x, $y, $n)
    $board = [Math]::Floor($y / 4)
    if ($y % 4 -eq 3) { return 1 }
    $v = 2 + [int]($n * 3.2) + ($board % 2)
    if ((($x * 7 + $board * 5) % 11) -eq 0) { $v -= 1 }
    return Clamp $v 2 6
}

# Chute: plank sides with a dark rim, a funnel on top, a spout below.
Save-Painted 'chute_side' $wood 1101 {
    param($x, $y, $n)
    if ($y -eq 0 -or $y -eq 15) { return 1 }
    if ($x -eq 0 -or $x -eq 15) { return 2 }
    return & $plank $x $y $n
}
Save-Painted 'chute_top' $wood 1102 {
    param($x, $y, $n)
    $edge = [Math]::Min([Math]::Min($x, 15 - $x), [Math]::Min($y, 15 - $y))
    if ($edge -eq 0) { return 1 }
    if ($edge -le 2) { return Clamp (4 + [int]($n * 2) - ($edge -eq 2)) 2 6 }
    return Clamp ($edge - 3) 0 2                     # the funnel darkens towards the middle
}
Save-Painted 'chute_bottom' $wood 1103 {
    param($x, $y, $n)
    if ($x -ge 6 -and $x -le 9 -and $y -ge 6 -and $y -le 9) { return 0 }
    if ($x -ge 5 -and $x -le 10 -and $y -ge 5 -and $y -le 10) { return 1 }
    return & $plank $x $y $n
}

# Feeding trough: planks with a darker inner face.
Save-Painted 'trough_side' $wood 1111 {
    param($x, $y, $n)
    if ($y -eq 0) { return 6 }
    if ($y -eq 15 -or $x -eq 0 -or $x -eq 15) { return 1 }
    return & $plank $x ($y + 1) $n
}
Save-Painted 'trough_side_inner' $wood 1112 {
    param($x, $y, $n)
    return Clamp ((& $plank $x $y $n) - 2) 0 4
}
Save-Painted 'trough_top' $wood 1113 {
    param($x, $y, $n)
    $v = 4 + [int]($n * 2.5)
    if (($x + $y) % 7 -eq 0) { $v -= 1 }
    return Clamp $v 3 6
}
Save-Painted 'trough_bottom' $wood 1114 {
    param($x, $y, $n)
    return & $plank $y $x $n                          # boards run the other way on the floor
}

# Storage crate: framed planks with a diagonal brace and iron corners; the open top shows the inside.
$crateFrame = {
    param($x, $y, $n)
    $corner = ($x -le 1 -or $x -ge 14) -and ($y -le 1 -or $y -ge 14)
    if ($corner) { if (($x + $y) % 2) { return 8 } else { return 7 } }
    if ($x -le 1 -or $x -ge 14 -or $y -le 1 -or $y -ge 14) {
        if ($x -eq 0 -or $x -eq 15 -or $y -eq 0 -or $y -eq 15) { return 1 }
        return Clamp (5 + [int]($n * 1.6)) 4 6
    }
    return -2
}
Save-Painted 'crate' $wood 1121 {
    param($x, $y, $n)
    $f = & $crateFrame $x $y $n
    if ($f -ne -2) { return $f }
    if ([Math]::Abs($x - $y) -le 1) { if ($x -eq $y) { return 6 } else { return 4 } }   # brace
    return Clamp ((& $plank $x $y $n) - 1) 1 5
}
Save-Painted 'crate_open' $wood 1122 {
    param($x, $y, $n)
    $f = & $crateFrame $x $y $n
    if ($f -ne -2) { return $f }
    if ($y -le 3) { return 0 }                          # shadow under the rim
    return Clamp (1 + [int]($n * 2)) 0 2
}

# ---- copper pipes ------------------------------------------------------------------------------------
# 0 outline, 1..4 copper, 5 highlight, 6 verdigris
$copper = @('#4a2416', '#7c3f27', '#a3573a', '#c0714f', '#d98e69', '#f2b893', '#5c9a86')
Save-Painted 'pipe_structure' $copper 1131 {
    param($x, $y, $n)
    if ($x -eq 5 -or $x -eq 10 -or $y -eq 5 -or $y -eq 10) {
        if (($x -ge 5 -and $x -le 10) -or ($y -ge 5 -and $y -le 10)) { return 1 }
    }
    $v = 2 + [int]($n * 2.6)
    if (($x * 3 + $y * 5) % 13 -eq 0) { $v = 5 }
    if ($n -gt 0.82 -and ($x + $y) % 3 -eq 0) { $v = 6 }
    return Clamp $v 1 6
}
Save-Painted 'pipe_connectors' $copper 1132 {
    param($x, $y, $n)
    $edge = [Math]::Min([Math]::Min($x, 15 - $x), [Math]::Min($y, 15 - $y))
    if ($edge -eq 0) { return 0 }
    if ($edge -eq 1) { return 4 }
    return Clamp (1 + [int]($n * 2.5)) 1 3
}

Write-Host "[textures] logistics: $out"
