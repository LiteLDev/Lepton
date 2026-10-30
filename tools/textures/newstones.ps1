# U3 newstones textures: original 16x16 pixel art (seeded noise + hand-made patterns) for limestone,
# jasper, shale, myalite and permafrost: raw stone, polished stone, bricks, chiseled bricks, pillar
# side/top (permafrost: raw + bricks). Myalite is drawn in grey and written once per tint step,
# multiplied by that step's colour (Quark tints it by position; see docs/spec/newstones.md §3).
#
# Dot-sourced by tools/pack/newstones.ps1, which calls Write-NewStonesTextures and records the files.
# Standalone:  pwsh -NoProfile -File tools/textures/newstones.ps1   (writes into assets/pack/rp/textures/blocks/newstones)

. "$PSScriptRoot/lib.ps1"

# Number of myalite tint steps (lepton:tint 0..15) and the hue range they cover (Quark: h = noise*0.15
# - 0.25 with noise in [-1, 1]).
$script:MyaliteTints = 16
$script:MyaliteHueMin = -0.4
$script:MyaliteHueSpan = 0.3

# Palettes (dark -> light).
$script:NewStoneRamps = [ordered]@{
    limestone  = @('#6b6556', '#7f796a', '#938d7d', '#a6a190', '#b8b3a2', '#c9c5b5', '#d8d5c7')
    jasper     = @('#4f1d16', '#66281e', '#7c3326', '#913f30', '#a44d3b', '#b55f4b', '#c4735e')
    shale      = @('#363c46', '#444b57', '#535b68', '#626b79', '#727b89', '#828b99', '#939ba8')
    myalite    = @('#8c8c8c', '#a0a0a0', '#b2b2b2', '#c3c3c3', '#d3d3d3', '#e2e2e2', '#f0f0f0')
    permafrost = @('#4f6275', '#5f7488', '#70879b', '#8299ad', '#95acbf', '#aac0d1', '#c2d5e3')
}

function New-Grid { return , ([int[, ]]::new(16, 16)) }
function Clamp-Index([int]$v, [int]$lo, [int]$hi) { return [Math]::Max($lo, [Math]::Min($hi, $v)) }

# Noise stretched to 0..1 (tileable).
function Get-Noise01([int]$Seed, [int]$Cell = 4, [int]$Octaves = 2) {
    $n = New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves $Octaves
    $min = 1.0; $max = 0.0
    foreach ($v in $n) { $min = [Math]::Min($min, $v); $max = [Math]::Max($max, $v) }
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $n[$x, $y] = ($n[$x, $y] - $min) / [Math]::Max(1e-6, $max - $min) * 0.999 } }
    return , $n
}

# ---- raw stones ---------------------------------------------------------------------------------

# Sedimentary stone: horizontal layers of different shades that wave a little across the texture,
# broken up by noise. $Contrast: how far the layers differ; $Specks: chance of a light speck.
function New-LayeredGrid([int]$Seed, [int]$Levels, [double]$Contrast, [double]$Wave, [double]$Specks) {
    $rng = [System.Random]::new($Seed)
    $g = New-Grid; $n = Get-Noise01 ($Seed + 1) 4 2
    $band = [double[]]::new(16); $v = 0.5
    for ($y = 0; $y -lt 16; $y++) { $v = [Math]::Max(0.1, [Math]::Min(0.9, $v + ($rng.NextDouble() - 0.5) * 0.55)); $band[$y] = $v }
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $off = [int][Math]::Round([Math]::Sin(($x / 16.0) * 2 * [Math]::PI + $Seed) * $Wave)
            $yy = (($y + $off) % 16 + 16) % 16
            $t = $band[$yy] * $Contrast + $n[$x, $y] * (1 - $Contrast)
            $i = [int][Math]::Floor($t * ($Levels - 1) + 0.5)
            if ($rng.NextDouble() -lt $Specks) { $i += 2 }
            $g[$x, $y] = Clamp-Index $i 0 ($Levels - 1)
        }
    }
    return , $g
}

# Myalite: smooth swirling veins (period 16 in both axes so it tiles).
function New-SwirlGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-Noise01 $Seed 8 2
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $a = 2 * [Math]::PI / 16
            $s = [Math]::Sin(($x + $y + 3 * [Math]::Sin(($y - $x) * $a)) * $a + $n[$x, $y] * 2.5)
            $t = 0.5 + 0.38 * $s + ($n[$x, $y] - 0.5) * 0.35
            $g[$x, $y] = Clamp-Index ([int][Math]::Floor($t * $Levels)) 0 ($Levels - 1)
        }
    }
    return , $g
}

# Permafrost: speckled stone with pale icy flecks and a few dark grains.
function New-FrostGrid([int]$Seed, [int]$Levels) {
    $rng = [System.Random]::new($Seed)
    $g = New-Grid; $n = Get-Noise01 $Seed 4 3
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $i = 1 + [int][Math]::Floor($n[$x, $y] * ($Levels - 3))
            $r = $rng.NextDouble()
            if ($r -lt 0.07) { $i = $Levels - 1 } elseif ($r -lt 0.11) { $i = 0 }
            $g[$x, $y] = Clamp-Index $i 0 ($Levels - 1)
        }
    }
    return , $g
}

# ---- worked stone -------------------------------------------------------------------------------

# Polished: a smooth face with a lit top/left rim and a shaded bottom/right rim.
function New-PolishedGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-Noise01 $Seed 8 2
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($y -eq 15 -or $x -eq 15) { $g[$x, $y] = 0 }
            elseif ($y -eq 0 -or $x -eq 0) { $g[$x, $y] = $Levels - 1 }
            elseif ($y -eq 14 -or $x -eq 14) { $g[$x, $y] = 1 }
            elseif ($y -eq 1 -or $x -eq 1) { $g[$x, $y] = $Levels - 2 }
            else { $g[$x, $y] = Clamp-Index (2 + [int][Math]::Floor($n[$x, $y] * ($Levels - 4))) 2 ($Levels - 3) }
        }
    }
    return , $g
}

# Bricks: courses of $Height px, bricks $Width px wide, staggered; dark mortar, lit brick tops.
function New-BrickGrid([int]$Seed, [int]$Levels, [int]$Height, [int]$Width) {
    $g = New-Grid; $n = Get-Noise01 $Seed 4 2
    for ($y = 0; $y -lt 16; $y++) {
        $row = [int][Math]::Floor($y / $Height); $dy = $y % $Height
        $off = if ($row % 2) { [int]($Width / 2) } else { 0 }
        for ($x = 0; $x -lt 16; $x++) {
            $dx = ($x + $off) % $Width
            if ($dy -eq $Height - 1 -or $dx -eq $Width - 1) { $g[$x, $y] = 0; continue }
            if ($dy -eq 0) { $g[$x, $y] = $Levels - 1; continue }
            if ($dx -eq 0) { $g[$x, $y] = $Levels - 2; continue }
            if ($dy -eq $Height - 2 -or $dx -eq $Width - 2) { $g[$x, $y] = 1; continue }
            $g[$x, $y] = Clamp-Index (2 + [int][Math]::Floor($n[$x, $y] * ($Levels - 4))) 2 ($Levels - 3)
        }
    }
    return , $g
}

# Chiseled bricks: two stacked carved blocks, each with a raised inner panel in a sunken groove.
function New-ChiseledGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-Noise01 $Seed 4 1
    for ($y = 0; $y -lt 16; $y++) {
        $by = $y % 8
        for ($x = 0; $x -lt 16; $x++) {
            $fill = Clamp-Index (2 + [int][Math]::Floor($n[$x, $y] * ($Levels - 4))) 2 ($Levels - 3)
            if ($by -eq 7 -or $x -eq 15) { $g[$x, $y] = 0; continue }                  # joint
            if ($by -eq 0 -or $x -eq 0) { $g[$x, $y] = $Levels - 1; continue }         # lit rim
            if ($by -eq 6 -or $x -eq 14) { $g[$x, $y] = 1; continue }                  # shaded rim
            $inX = $x -ge 3 -and $x -le 11; $inY = $by -ge 2 -and $by -le 4
            $onX = $x -eq 2 -or $x -eq 12; $onY = $by -eq 1 -or $by -eq 5
            if (($onX -and ($by -ge 1 -and $by -le 5)) -or ($onY -and ($x -ge 2 -and $x -le 12))) {
                $g[$x, $y] = if ($x -eq 2 -or $by -eq 1) { 0 } else { $Levels - 2 }    # groove
            } elseif ($inX -and $inY) {
                $g[$x, $y] = if ($by -eq 2 -or $x -eq 3) { $Levels - 2 } else { [Math]::Min($fill + 1, $Levels - 2) }
            } else { $g[$x, $y] = $fill }
        }
    }
    return , $g
}
# Pillar side: fluted column between a cap and a base band.
function New-PillarSideGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-Noise01 $Seed 4 2
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($y -eq 0 -or $y -eq 13) { $g[$x, $y] = $Levels - 1 }
            elseif ($y -eq 2 -or $y -eq 15) { $g[$x, $y] = 0 }
            elseif ($y -eq 1 -or $y -eq 14) { $g[$x, $y] = $Levels - 3 }
            elseif ($x -eq 0) { $g[$x, $y] = $Levels - 2 }
            elseif ($x -eq 15) { $g[$x, $y] = 1 }
            elseif (($x % 5) -eq 4) { $g[$x, $y] = 1 }
            elseif (($x % 5) -eq 0) { $g[$x, $y] = $Levels - 2 }
            else { $g[$x, $y] = Clamp-Index (2 + [int][Math]::Floor($n[$x, $y] * ($Levels - 5))) 2 ($Levels - 3) }
        }
    }
    return , $g
}

# Pillar top: a rim and two concentric rings.
function New-PillarTopGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-Noise01 $Seed 4 1
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $r = [int][Math]::Floor([Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5)))
            switch ($r) {
                7 { $g[$x, $y] = if ($x -eq 15 -or $y -eq 15) { 0 } else { $Levels - 1 } }
                5 { $g[$x, $y] = 1 }
                4 { $g[$x, $y] = $Levels - 2 }
                1 { $g[$x, $y] = $Levels - 2 }
                default { $g[$x, $y] = Clamp-Index (2 + [int][Math]::Floor($n[$x, $y] * ($Levels - 4))) 2 ($Levels - 3) }
            }
        }
    }
    return , $g
}

# ---- myalite tint -------------------------------------------------------------------------------

# java.awt.Color.HSBtoRGB, returns '#rrggbb'.
function ConvertFrom-Hsb([double]$Hue, [double]$Sat, [double]$Bri) {
    $h = ($Hue - [Math]::Floor($Hue)) * 6.0
    $f = $h - [Math]::Floor($h)
    $p = $Bri * (1 - $Sat); $q = $Bri * (1 - $Sat * $f); $t = $Bri * (1 - $Sat * (1 - $f))
    $rgb = switch ([int][Math]::Floor($h)) { 0 { $Bri, $t, $p } 1 { $q, $Bri, $p } 2 { $p, $Bri, $t } 3 { $p, $q, $Bri } 4 { $t, $p, $Bri } default { $Bri, $p, $q } }
    return '#{0:x2}{1:x2}{2:x2}' -f ([int]($rgb[0] * 255 + 0.5)), ([int]($rgb[1] * 255 + 0.5)), ([int]($rgb[2] * 255 + 0.5))
}

# The colour of tint step $Index (the centre of its hue range), saturation 0.7, brightness 0.8 as Quark.
function Get-MyaliteTint([int]$Index) {
    $h = $script:MyaliteHueMin + ($Index + 0.5) * $script:MyaliteHueSpan / $script:MyaliteTints
    return ConvertFrom-Hsb $h 0.7 0.8
}

function Get-TintedRamp([object[]]$Ramp, [string]$Tint) {
    $t = ConvertTo-Color $Tint
    return , @($Ramp | ForEach-Object {
            $c = ConvertTo-Color $_
            '#{0:x2}{1:x2}{2:x2}' -f [int]($c.R * $t.R / 255), [int]($c.G * $t.G / 255), [int]($c.B * $t.B / 255)
        })
}

# ---- output -------------------------------------------------------------------------------------

# Writes every texture into $Dir (assets/pack/rp/textures/blocks/newstones); returns the file paths.
# Myalite textures go to $Dir/myalite/<name>_t<i>.png.
function Write-NewStonesTextures([string]$Dir) {
    $files = [System.Collections.Generic.List[string]]::new()
    $seed = @{ limestone = 3100; jasper = 3200; shale = 3300; myalite = 3400; permafrost = 3500 }
    foreach ($s in 'limestone', 'jasper', 'shale', 'myalite') {
        $ramp = $script:NewStoneRamps[$s]; $L = $ramp.Count; $k = $seed[$s]
        $raw = switch ($s) {
            'limestone' { New-LayeredGrid $k $L 0.55 0.6 0.04 }
            'jasper' { New-LayeredGrid $k $L 0.7 1.6 0.02 }
            'shale' { New-LayeredGrid $k $L 0.85 0.4 0.0 }
            'myalite' { New-SwirlGrid $k $L }
        }
        $brick = switch ($s) { 'limestone' { @(4, 8) } 'jasper' { @(4, 8) } 'shale' { @(4, 16) } 'myalite' { @(8, 8) } }
        $grids = [ordered]@{
            $s                    = $raw
            "polished_$s"         = New-PolishedGrid ($k + 10) $L
            "${s}_bricks"         = New-BrickGrid ($k + 20) $L $brick[0] $brick[1]
            "chiseled_${s}_bricks" = New-ChiseledGrid ($k + 30) $L
            "${s}_pillar"         = New-PillarSideGrid ($k + 40) $L
            "${s}_pillar_top"     = New-PillarTopGrid ($k + 50) $L
        }
        foreach ($name in $grids.Keys) {
            if ($s -eq 'myalite') {
                for ($i = 0; $i -lt $script:MyaliteTints; $i++) {
                    $p = Join-Path $Dir "myalite/${name}_t$i.png"
                    Save-IndexGrid -Grid $grids[$name] -Ramp (Get-TintedRamp $ramp (Get-MyaliteTint $i)) -Path $p
                    $files.Add($p)
                }
            } else {
                $p = Join-Path $Dir "$name.png"
                Save-IndexGrid -Grid $grids[$name] -Ramp $ramp -Path $p
                $files.Add($p)
            }
        }
    }
    $ramp = $script:NewStoneRamps.permafrost; $L = $ramp.Count
    foreach ($kv in @(@('permafrost', (New-FrostGrid $seed.permafrost $L)), @('permafrost_bricks', (New-BrickGrid ($seed.permafrost + 20) $L 4 8)))) {
        $p = Join-Path $Dir "$($kv[0]).png"
        Save-IndexGrid -Grid $kv[1] -Ramp $ramp -Path $p
        $files.Add($p)
    }
    return , $files.ToArray()
}

if ($MyInvocation.InvocationName -ne '.') {
    $out = Join-Path $PSScriptRoot '../../assets/pack/rp/textures/blocks/newstones'
    $n = (Write-NewStonesTextures $out).Count
    Write-Host "[textures] newstones: $n files"
}
