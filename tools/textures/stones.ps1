# U2 stones textures: original 16x16 pixel art (seeded noise + hand-made patterns) for polished
# stones, bricks, chiseled bricks, pillars, raw metal bricks, sturdy stone and the celebratory lamps.
#
# Dot-sourced by tools/pack/stones.ps1, which calls Write-StonesTextures and records the files.
# Standalone:  pwsh -NoProfile -File tools/textures/stones.ps1   (writes into assets/pack/rp/textures/blocks)

. "$PSScriptRoot/lib.ps1"

# Palettes (dark -> light). Hues follow the vanilla materials so the blocks sit next to them.
$script:StoneRamps = [ordered]@{
    granite    = @('#4f3027', '#6f4637', '#8a5a47', '#a3705c', '#b9846f', '#cc9a85')
    diorite    = @('#7c7c80', '#9c9ca0', '#b5b5b8', '#cbcbcc', '#dfdfdf', '#f1f1ef')
    andesite   = @('#4e4f52', '#626366', '#77797c', '#8a8c90', '#9d9fa3', '#b0b2b6')
    calcite    = @('#a8a9a4', '#bfc0bb', '#d0d1cc', '#dddeda', '#e9eae6', '#f6f7f3')
    dripstone  = @('#4c392d', '#624a3c', '#79604f', '#8c7262', '#a08574', '#b49887')
    raw_iron   = @('#5a4136', '#7d5e4f', '#9e7c69', '#bd9a83', '#d6b49b', '#ead0bb')
    raw_gold   = @('#6b420c', '#946010', '#bf861a', '#dcaa2c', '#efc848', '#fbe27c')
    raw_copper = @('#5a2a1c', '#7f3e2a', '#a4563a', '#c27050', '#d98d68', '#eaaa86')
    stone      = @('#4f4f4f', '#626262', '#757575', '#888888', '#9a9a9a', '#ababab')
}

function New-Grid([int]$Size = 16) { return , ([int[, ]]::new($Size, $Size)) }

# Noise mapped to ramp indices lo..hi (inclusive).
function Get-NoiseIndex([double[, ]]$Noise, [int]$x, [int]$y, [int]$lo, [int]$hi) {
    $v = $Noise[$x, $y]
    return [Math]::Max($lo, [Math]::Min($hi, $lo + [int][Math]::Floor($v * ($hi - $lo + 1))))
}

function Get-StretchedNoise([int]$Seed, [int]$Cell = 4, [int]$Octaves = 2) {
    $n = New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves $Octaves
    $min = 1.0; $max = 0.0
    foreach ($v in $n) { $min = [Math]::Min($min, $v); $max = [Math]::Max($max, $v) }
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $n[$x, $y] = ($n[$x, $y] - $min) / [Math]::Max(1e-6, $max - $min) * 0.999 } }
    return , $n
}

# Polished stone: a smooth slab face with a lit top/left rim and a shaded bottom/right rim.
function New-PolishedGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-StretchedNoise $Seed 8 2
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($y -eq 15 -or $x -eq 15) { $g[$x, $y] = 0 }
            elseif ($y -eq 0 -or $x -eq 0) { $g[$x, $y] = $Levels - 1 }
            elseif ($y -eq 14 -or $x -eq 14) { $g[$x, $y] = 1 }
            else { $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 2) }
        }
    }
    return , $g
}

# Stone bricks: 4 courses of 4 px, bricks 8 px wide, staggered; dark mortar, lit brick tops.
function New-BricksGrid([int]$Seed, [int]$Levels, [int]$Height = 4, [int]$Width = 8) {
    $g = New-Grid; $n = Get-StretchedNoise $Seed 4 2
    for ($y = 0; $y -lt 16; $y++) {
        $row = [int][Math]::Floor($y / $Height); $dy = $y % $Height
        $off = if ($row % 2) { [int]($Width / 2) } else { 0 }
        for ($x = 0; $x -lt 16; $x++) {
            $dx = ($x + $off) % $Width
            if ($dy -eq $Height - 1 -or $dx -eq $Width - 1) { $g[$x, $y] = 0; continue }        # mortar
            if ($dy -eq 0 -or $dx -eq 0) { $g[$x, $y] = $Levels - 1; continue }                 # lit edge
            if ($dy -eq $Height - 2 -or $dx -eq $Width - 2) { $g[$x, $y] = 1; continue }        # shaded edge
            $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 2)
        }
    }
    return , $g
}

# Chiseled bricks: a polished frame around a carved square ring with a raised centre stud.
function New-ChiseledGrid([int]$Seed, [int]$Levels) {
    $g = New-PolishedGrid $Seed $Levels
    $n = Get-StretchedNoise ($Seed + 7) 4 1
    for ($y = 2; $y -le 13; $y++) {
        for ($x = 2; $x -le 13; $x++) {
            $r = [Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5))
            if ($r -ge 5.5) { $g[$x, $y] = if ($x -eq 2 -or $y -eq 2) { 1 } else { $Levels - 2 } }       # inner bevel
            elseif ($r -ge 4.5) { $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 3) }
            elseif ($r -ge 3.5) { $g[$x, $y] = if ($x -lt 8 -and $y -lt 8) { 0 } else { 1 } }            # carved groove
            elseif ($r -ge 1.5) { $g[$x, $y] = Get-NoiseIndex $n $x $y 3 ($Levels - 2) }
            else { $g[$x, $y] = if ($x -eq 7 -and $y -eq 7) { $Levels - 1 } else { $Levels - 2 } }         # stud
        }
    }
    return , $g
}

# Pillar side: fluted column between a cap and a base band.
function New-PillarSideGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-StretchedNoise $Seed 4 2
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($y -eq 0 -or $y -eq 13) { $g[$x, $y] = $Levels - 1 }
            elseif ($y -eq 2 -or $y -eq 15) { $g[$x, $y] = 0 }
            elseif ($y -eq 1 -or $y -eq 14) { $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 2) }
            elseif ($x -eq 0) { $g[$x, $y] = $Levels - 2 }
            elseif ($x -eq 15) { $g[$x, $y] = 1 }
            elseif (($x % 4) -eq 3) { $g[$x, $y] = 1 }                                                     # flute shadow
            elseif (($x % 4) -eq 0) { $g[$x, $y] = $Levels - 2 }                                           # flute light
            else { $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 3) }
        }
    }
    return , $g
}

# Pillar top: concentric squares.
function New-PillarTopGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-StretchedNoise $Seed 4 1
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $r = [int][Math]::Floor([Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5)))
            switch ($r) {
                7 { $g[$x, $y] = if ($x -eq 15 -or $y -eq 15) { 0 } else { $Levels - 1 } }
                5 { $g[$x, $y] = 1 }
                2 { $g[$x, $y] = $Levels - 2 }
                default { $g[$x, $y] = Get-NoiseIndex $n $x $y 2 ($Levels - 2) }
            }
        }
    }
    return , $g
}

# Raw metal bricks: chunky 8x8 bricks with lumpy ore-like faces.
function New-RawBricksGrid([int]$Seed, [int]$Levels) {
    $g = New-BricksGrid $Seed $Levels 8 8
    $n = Get-StretchedNoise ($Seed + 3) 2 1
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($g[$x, $y] -ge 2 -and $g[$x, $y] -le $Levels - 2 -and $n[$x, $y] -gt 0.8) { $g[$x, $y] = $Levels - 1 }
            elseif ($g[$x, $y] -ge 2 -and $n[$x, $y] -lt 0.15) { $g[$x, $y] = 1 }
        }
    }
    return , $g
}

# Sturdy stone: dense stone inside a heavy double frame with corner rivets.
function New-SturdyGrid([int]$Seed, [int]$Levels) {
    $g = New-Grid; $n = Get-StretchedNoise $Seed 4 3
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $e = [Math]::Min([Math]::Min($x, $y), [Math]::Min(15 - $x, 15 - $y))
            $corner = ($x -in 2, 13) -and ($y -in 2, 13)
            if ($e -eq 0) { $g[$x, $y] = 0 }
            elseif ($e -eq 1) { $g[$x, $y] = if ($x -eq 1 -or $y -eq 1) { $Levels - 2 } else { 1 } }
            elseif ($corner) { $g[$x, $y] = $Levels - 1 }
            elseif ($e -eq 2) { $g[$x, $y] = Get-NoiseIndex $n $x $y 1 2 }
            else { $g[$x, $y] = Get-NoiseIndex $n $x $y 1 ($Levels - 2) }
        }
    }
    return , $g
}

# Celebratory lamp: a stone (or stone brick) frame around a glowing glass window with a cross.
function Save-LampTexture([string]$Path, [int]$Seed, [switch]$Bricks) {
    $frame = $script:StoneRamps.stone
    $glow = @('#a0581c', '#d9892a', '#f2b544', '#fbd77a', '#fff1b8', '#fffbe8')
    $ramp = @($frame) + @($glow)
    $nf = $frame.Count
    $g = if ($Bricks) { New-BricksGrid $Seed $nf } else { New-PolishedGrid $Seed $nf }
    if (-not $Bricks) {
        $sn = Get-StretchedNoise ($Seed + 1) 4 3
        for ($y = 1; $y -lt 15; $y++) { for ($x = 1; $x -lt 15; $x++) { $g[$x, $y] = Get-NoiseIndex $sn $x $y 1 ($nf - 2) } }
    }
    for ($y = 3; $y -le 12; $y++) {
        for ($x = 3; $x -le 12; $x++) {
            $ring = [Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5))
            if ($ring -ge 4.5) { $g[$x, $y] = if ($x -eq 3 -or $y -eq 3) { 0 } else { $nf - 1 } ; continue }   # window rim
            if ($x -in 7, 8 -or $y -in 7, 8) { $g[$x, $y] = $nf + 0; continue }                               # mullions
            $d = [Math]::Sqrt([Math]::Pow($x - 7.5, 2) + [Math]::Pow($y - 7.5, 2))
            $g[$x, $y] = $nf + [Math]::Max(1, [Math]::Min(5, 6 - [int][Math]::Floor($d)))
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Writes every texture into $Dir; returns the written paths.
function Write-StonesTextures([string]$Dir) {
    $out = [System.Collections.Generic.List[string]]::new()
    $save = { param($name, $grid, $ramp) $p = Join-Path $Dir "$name.png"; Save-IndexGrid -Grid $grid -Ramp $ramp -Path $p; $out.Add($p) }
    $seed = 200
    foreach ($s in 'granite', 'diorite', 'andesite', 'calcite', 'dripstone') {
        $r = $script:StoneRamps[$s]; $l = $r.Count; $seed += 10
        & $save "${s}_bricks" (New-BricksGrid $seed $l) $r
        & $save "chiseled_${s}_bricks" (New-ChiseledGrid ($seed + 1) $l) $r
        & $save "${s}_pillar" (New-PillarSideGrid ($seed + 2) $l) $r
        & $save "${s}_pillar_top" (New-PillarTopGrid ($seed + 3) $l) $r
    }
    & $save 'polished_calcite' (New-PolishedGrid 301 6) $script:StoneRamps.calcite
    & $save 'polished_dripstone' (New-PolishedGrid 302 6) $script:StoneRamps.dripstone
    $seed = 320
    foreach ($m in 'raw_iron', 'raw_gold', 'raw_copper') {
        $seed += 5
        & $save "${m}_bricks" (New-RawBricksGrid $seed 6) $script:StoneRamps[$m]
    }
    & $save 'sturdy_stone' (New-SturdyGrid 340 6) $script:StoneRamps.stone
    $p = Join-Path $Dir 'stone_lamp.png'; Save-LampTexture -Path $p -Seed 350; $out.Add($p)
    $p = Join-Path $Dir 'stone_brick_lamp.png'; Save-LampTexture -Path $p -Seed 351 -Bricks; $out.Add($p)
    return $out
}

if ($MyInvocation.InvocationName -ne '.') {
    $dir = Join-Path $PSScriptRoot '../../assets/pack/rp/textures/blocks'
    Write-StonesTextures $dir | ForEach-Object { Write-Host $_ }
}
