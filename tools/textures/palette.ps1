# U5 palette textures: original pixel art for shingles, framed glass, gold bars, iron plates,
# iron pillar, iron ladder, paper lanterns / walls, bamboo mats, thatch and the grate.
#
#   . tools/textures/palette.ps1; Write-PaletteTextures -Dir assets/pack/rp/textures/blocks/palette
#
# Called by tools/pack/palette.ps1 (which records the files for the unit). Deterministic: the same
# script always draws the same pixels.
. "$PSScriptRoot/lib.ps1"

$script:PaletteDyes = [ordered]@{
    white = '#f0f0f0'; orange = '#f07613'; magenta = '#bd44b3'; light_blue = '#3aafd9'; yellow = '#f8c527'; lime = '#70b919'
    pink = '#ed8dac'; gray = '#3e4447'; light_gray = '#8e8e86'; cyan = '#158991'; purple = '#792aac'; blue = '#35399d'
    brown = '#724728'; green = '#546d1b'; red = '#a12722'; black = '#141519'
}
# Fired terracotta tones (eyeballed from the colour names, not sampled from game files).
$script:PaletteTerracotta = [ordered]@{
    '' = '#9a5f44'; white = '#cfb3a3'; orange = '#a1562a'; magenta = '#94586e'; light_blue = '#716d8a'; yellow = '#b9862a'
    lime = '#687536'; pink = '#a14f4f'; gray = '#3b2c26'; light_gray = '#886c63'; cyan = '#575c5c'; purple = '#774858'
    blue = '#4b3c5c'; brown = '#4e3425'; green = '#4d532c'; red = '#8f3d2f'; black = '#271a14'
}

function New-Grid([int]$Fill = 0) {
    $g = [int[, ]]::new(16, 16)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $g[$x, $y] = $Fill } }
    return , $g
}

function Get-Shades($Base, [double[]]$Factors) { return , @($Factors | ForEach-Object { ConvertTo-Hex (Shade-Color $Base $_) }) }

# Overlapping roof tiles: 4 courses of 4 px, tiles 4 px wide, every other course shifted by 2.
function Save-ShinglesTexture([string]$Path, [string]$Base, [int]$Seed) {
    $ramp = Get-Shades $Base @(0.55, 0.78, 0.92, 1.0, 1.08)
    $rng = [System.Random]::new($Seed)
    $g = New-Grid
    for ($y = 0; $y -lt 16; $y++) {
        $course = [int][Math]::Floor($y / 4); $dy = $y % 4
        $shift = if ($course % 2) { 2 } else { 0 }
        for ($x = 0; $x -lt 16; $x++) {
            $dx = ($x + $shift) % 4
            if ($dy -eq 3) { $v = 0 }                              # shadow under the course above
            elseif ($dx -eq 3) { $v = 1 }                          # gap between tiles
            elseif ($dy -eq 2 -and ($dx -eq 0 -or $dx -eq 2)) { $v = 1 }   # rounded tile corners
            elseif ($dy -eq 0) { $v = 4 }                          # lit upper edge
            else { $v = 2 + $rng.Next(0, 2) }
            $g[$x, $y] = $v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Iron frame around a glass pane; $Tint = $null for clear glass (transparent centre).
function Save-FramedGlassTexture([string]$Path, $Tint) {
    $center = if ($Tint) { ConvertTo-Hex ([System.Drawing.Color]::FromArgb(150, (ConvertTo-Color $Tint))) } else { '#00000000' }
    $shine = if ($Tint) { ConvertTo-Hex ([System.Drawing.Color]::FromArgb(190, (Shade-Color $Tint 1.35))) } else { '#90ffffff' }
    $ramp = @('#3c3f44', '#6d7178', '#a9adb3', '#d6d8db', $center, $shine)
    $g = New-Grid 4
    for ($i = 0; $i -lt 16; $i++) {
        foreach ($p in @(@($i, 0), @($i, 15), @(0, $i), @(15, $i))) { $g[$p[0], $p[1]] = 0 }
        if ($i -ge 1 -and $i -le 14) {
            foreach ($p in @(@($i, 1), @(1, $i))) { $g[$p[0], $p[1]] = 2 }
            foreach ($p in @(@($i, 14), @(14, $i))) { $g[$p[0], $p[1]] = 1 }
        }
    }
    # corner brackets with rivets
    foreach ($c in @(@(2, 2, 1, 1), @(13, 2, -1, 1), @(2, 13, 1, -1), @(13, 13, -1, -1))) {
        $x = $c[0]; $y = $c[1]
        $g[$x, $y] = 1; $g[($x + $c[2]), $y] = 1; $g[$x, ($y + $c[3])] = 1
        $g[$x, $y] = 3
    }
    # glass highlights
    foreach ($p in @(@(4, 6), @(5, 5), @(6, 4), @(9, 11), @(10, 10), @(11, 9))) { $g[$p[0], $p[1]] = 5 }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Vertical bars with cross rails, transparent between (bars texture).
function Save-BarsTexture([string]$Path, [object[]]$Ramp) {
    $g = New-Grid -1
    foreach ($x in 1, 5, 10, 14) {
        for ($y = 0; $y -lt 16; $y++) { $g[$x, $y] = 2; $g[($x + 1), $y] = 1 }
    }
    foreach ($rail in @(@(1, 2), @(14, 13))) {
        for ($x = 0; $x -lt 16; $x++) { $g[$x, $rail[0]] = 3; $g[$x, $rail[1]] = 0 }
    }
    foreach ($x in 1, 5, 10, 14) { $g[$x, 7] = 3; $g[$x, 8] = 0 }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path $Path
}

# 2x2 riveted plates; -Rust blends seeded rust patches in.
function Save-PlateTexture([string]$Path, [switch]$Rust, [int]$Seed = 7) {
    $ramp = @('#4a4c50', '#7b7e83', '#9c9fa4', '#b6b9bd', '#d7d9dc', '#5e3218', '#8a4a20', '#b0642c', '#c98345')
    $noise = New-NoiseGrid -Seed $Seed -Size 16 -Cell 4 -Octaves 3
    $g = New-Grid
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $px = $x % 8; $py = $y % 8
            if ($px -eq 7 -or $py -eq 7) { $v = 0 }
            elseif ($px -eq 0 -or $py -eq 0) { $v = 4 }
            elseif ($px -eq 6 -or $py -eq 6) { $v = 1 }
            else { $v = if ($noise[$x, $y] -gt 0.55) { 3 } else { 2 } }
            if (($px -eq 1 -or $px -eq 5) -and ($py -eq 1 -or $py -eq 5)) { $v = 4 }   # rivets
            if ($Rust) {
                $n = $noise[$x, $y]
                if ($n -gt 0.72) { $v = 8 } elseif ($n -gt 0.63) { $v = 7 } elseif ($n -gt 0.56 -and $v -ne 0) { $v = 6 }
                if ($v -eq 0 -and $n -gt 0.55) { $v = 5 }
            }
            $g[$x, $y] = $v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

function Save-PillarTextures([string]$SidePath, [string]$TopPath) {
    $ramp = @('#3f4145', '#6f7277', '#9a9da2', '#c0c3c7', '#e0e2e4')
    $g = New-Grid
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $v = switch ($x % 4) { 0 { 3 } 1 { 2 } 2 { 2 } 3 { 1 } }
            if ($y -eq 0 -or $y -eq 15) { $v = 0 } elseif ($y -eq 1 -or $y -eq 14) { $v = 4 } elseif ($y -eq 2 -or $y -eq 13) { $v = 1 }
            $g[$x, $y] = $v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $SidePath
    $t = New-Grid
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $r = [Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5))
            $t[$x, $y] = if ($r -gt 7) { 0 } elseif ($r -gt 6) { 4 } elseif ($r -gt 5) { 1 } elseif ($r -gt 3) { 2 } elseif ($r -gt 2) { 1 } else { 3 }
        }
    }
    Save-IndexGrid -Grid $t -Ramp $ramp -Path $TopPath
}

function Save-LadderTexture([string]$Path) {
    $ramp = @('#3e4044', '#7a7d82', '#b4b7bb', '#dfe1e3')
    $g = New-Grid -1
    for ($y = 0; $y -lt 16; $y++) { $g[2, $y] = 2; $g[3, $y] = 1; $g[13, $y] = 2; $g[12, $y] = 1 }
    foreach ($y in 1, 5, 9, 13) {
        for ($x = 4; $x -le 11; $x++) { $g[$x, $y] = 3; $g[$x, ($y + 1)] = 0 }
        $g[2, $y] = 3; $g[13, $y] = 3
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Paper with a dark wooden frame / lattice. $Lines: positions of the lattice bars; $Blossoms: draw sakura.
function Save-PaperTexture([string]$Path, [int[]]$Lines, [switch]$Blossoms, [int]$Seed = 3, [int]$Frame = 0) {
    $ramp = @('#3a2414', '#5e3c22', '#e9e2cf', '#f5f0e2', '#fffaf0', '#d9708f', '#f2a7bd', '#6b4a2e')
    $rng = [System.Random]::new($Seed)
    $g = New-Grid
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $v = 2 + $rng.Next(0, 3)
            if ($x -le $Frame -or $x -ge 15 - $Frame -or $y -le $Frame -or $y -ge 15 - $Frame) { $v = if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { 0 } else { 1 } }
            elseif ($Lines -contains $x -or $Lines -contains $y) { $v = 1 }
            $g[$x, $y] = $v
        }
    }
    if ($Blossoms) {
        # a branch from the lower left with blossoms
        foreach ($p in @(@(2, 12), @(3, 11), @(4, 11), @(5, 10), @(6, 9), @(7, 9), @(8, 8), @(9, 7), @(10, 7), @(11, 6), @(6, 10), @(5, 12))) { $g[$p[0], $p[1]] = 7 }
        foreach ($c in @(@(4, 9), @(8, 6), @(11, 4), @(12, 8), @(6, 12), @(9, 10))) {
            $x = $c[0]; $y = $c[1]
            $g[$x, $y] = 5
            foreach ($d in @(@(1, 0), @(-1, 0), @(0, 1), @(0, -1))) { $nx = $x + $d[0]; $ny = $y + $d[1]; if ($nx -gt 1 -and $nx -lt 14 -and $ny -gt 1 -and $ny -lt 14) { $g[$nx, $ny] = 6 } }
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

function Save-LanternTextures([string]$Dir) {
    # side: paper between dark ribs; the model uses pixels 2..13 of each face
    $ramp = @('#2e1c10', '#5a3a20', '#f3e7c9', '#fbf2dc', '#ffe9a8', '#d9708f', '#f2a7bd', '#6b4a2e')
    foreach ($sakura in $false, $true) {
        $g = New-Grid 3
        for ($y = 0; $y -lt 16; $y++) {
            for ($x = 0; $x -lt 16; $x++) {
                if ($x -le 2 -or $x -ge 13) { $g[$x, $y] = 1 }
                elseif ($y -le 2 -or $y -ge 13) { $g[$x, $y] = 0 }
                elseif ($y % 3 -eq 0) { $g[$x, $y] = 2 }
                elseif (($x + $y) % 7 -eq 0) { $g[$x, $y] = 4 }
            }
        }
        if ($sakura) {
            foreach ($p in @(@(4, 10), @(5, 9), @(6, 8), @(7, 8), @(8, 7), @(9, 6), @(10, 5))) { $g[$p[0], $p[1]] = 7 }
            foreach ($c in @(@(5, 7), @(9, 4), @(10, 8), @(7, 10))) { $g[$c[0], $c[1]] = 5; $g[($c[0] + 1), $c[1]] = 6; $g[$c[0], ($c[1] + 1)] = 6 }
        }
        $name = if ($sakura) { 'paper_lantern_sakura_side' } else { 'paper_lantern_side' }
        Save-IndexGrid -Grid $g -Ramp $ramp -Path (Join-Path $Dir "$name.png")
    }
    $t = New-Grid 1
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if ($x -le 2 -or $x -ge 13 -or $y -le 2 -or $y -ge 13) { $t[$x, $y] = 0 } elseif ($x -ge 6 -and $x -le 9 -and $y -ge 6 -and $y -le 9) { $t[$x, $y] = 4 } } }
    Save-IndexGrid -Grid $t -Ramp $ramp -Path (Join-Path $Dir 'paper_lantern_top.png')
    $in = New-Grid 4
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if (($x + 2 * $y) % 5 -eq 0) { $in[$x, $y] = 3 } } }
    Save-IndexGrid -Grid $in -Ramp @('#2e1c10', '#5a3a20', '#ffd27a', '#fff3c4', '#ffe08f') -Path (Join-Path $Dir 'paper_lantern_inside.png')
}

# Bamboo: stalks 3 px wide (dark seam, body, highlight) with nodes. -Along u|v; ends = cut stalks.
function Save-BambooTextures([string]$Dir) {
    $ramp = @('#4a5a1c', '#6f8a2a', '#93ad3d', '#b5c95a', '#d3dc8a', '#3a4716')
    foreach ($along in 'u', 'v') {
        $g = New-Grid
        for ($a = 0; $a -lt 16; $a++) {
            for ($b = 0; $b -lt 16; $b++) {
                # b: across the stalks, a: along them
                $k = $b % 4
                $v = switch ($k) { 0 { 0 } 1 { 2 } 2 { 3 } 3 { 1 } }
                $stalk = [int][Math]::Floor($b / 4)
                if ((($a + 5 * $stalk) % 8) -eq 0 -and $k -ne 0) { $v = 4 }     # node
                if ((($a + 5 * $stalk) % 8) -eq 1 -and $k -ne 0) { $v = 5 }
                if ($along -eq 'v') { $g[$b, $a] = $v } else { $g[$a, $b] = $v }
            }
        }
        Save-IndexGrid -Grid $g -Ramp $ramp -Path (Join-Path $Dir "bamboo_mat_$along.png")
    }
    $e = New-Grid 0
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $cx = $x % 4; $cy = $y % 4
            $e[$x, $y] = if ($cx -eq 0 -or $cy -eq 0) { 0 } elseif ($cx -eq 2 -and $cy -eq 2) { 4 } elseif ($cx -eq 1 -or $cy -eq 1) { 3 } else { 2 }
        }
    }
    Save-IndexGrid -Grid $e -Ramp $ramp -Path (Join-Path $Dir 'bamboo_mat_end.png')
}

function Save-ThatchTextures([string]$Dir) {
    $ramp = @('#7a5a1c', '#a37e2c', '#c49c3c', '#d9b552', '#ead07a')
    foreach ($kind in 'side', 'top') {
        $rng = [System.Random]::new($(if ($kind -eq 'side') { 41 } else { 42 }))
        $g = New-Grid 2
        for ($s = 0; $s -lt 40; $s++) {
            $x = $rng.Next(0, 16); $y = $rng.Next(0, 16); $len = $rng.Next(3, 7); $shade = $rng.Next(0, 5)
            for ($i = 0; $i -lt $len; $i++) {
                $px = if ($kind -eq 'side') { $x } else { ($x + $i) % 16 }
                $py = if ($kind -eq 'side') { ($y + $i) % 16 } else { ($y + [int]($i / 2)) % 16 }
                $g[$px, $py] = $shade
            }
        }
        if ($kind -eq 'side') { foreach ($y in 7, 15) { for ($x = 0; $x -lt 16; $x++) { if ($g[$x, $y] -gt 1) { $g[$x, $y] = 1 } } } }
        Save-IndexGrid -Grid $g -Ramp $ramp -Path (Join-Path $Dir "thatch_$kind.png")
    }
}

function Save-GrateTexture([string]$Path) {
    $ramp = @('#34363a', '#5c5f64', '#8c9095', '#b8bbbf')
    $g = New-Grid -1
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $bx = ($x % 4) -eq 0; $by = ($y % 4) -eq 0
            if ($bx -and $by) { $g[$x, $y] = 3 }
            elseif ($bx) { $g[$x, $y] = 2 }
            elseif ($by) { $g[$x, $y] = 1 }
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

function Write-PaletteTextures([Parameter(Mandatory)][string]$Dir) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $seed = 100
    foreach ($kv in $script:PaletteTerracotta.GetEnumerator()) {
        $name = if ($kv.Key) { "$($kv.Key)_shingles" } else { 'shingles' }
        Save-ShinglesTexture -Path (Join-Path $Dir "$name.png") -Base $kv.Value -Seed ($seed++)
    }
    Save-FramedGlassTexture -Path (Join-Path $Dir 'framed_glass.png') -Tint $null
    foreach ($kv in $script:PaletteDyes.GetEnumerator()) { Save-FramedGlassTexture -Path (Join-Path $Dir "$($kv.Key)_framed_glass.png") -Tint $kv.Value }
    Save-BarsTexture -Path (Join-Path $Dir 'gold_bars.png') -Ramp @('#8a5a10', '#c99422', '#f2cc45', '#fff2a0')
    Save-PlateTexture -Path (Join-Path $Dir 'iron_plate.png') -Seed 7
    Save-PlateTexture -Path (Join-Path $Dir 'rusty_iron_plate.png') -Seed 8 -Rust
    Save-PillarTextures -SidePath (Join-Path $Dir 'iron_pillar.png') -TopPath (Join-Path $Dir 'iron_pillar_top.png')
    Save-LadderTexture -Path (Join-Path $Dir 'iron_ladder.png')
    Save-LanternTextures -Dir $Dir
    Save-PaperTexture -Path (Join-Path $Dir 'paper_wall.png') -Lines @(5, 10) -Seed 3
    Save-PaperTexture -Path (Join-Path $Dir 'paper_wall_big.png') -Lines @() -Seed 4 -Frame 1
    Save-PaperTexture -Path (Join-Path $Dir 'paper_wall_sakura.png') -Lines @() -Seed 5 -Blossoms
    Save-BambooTextures -Dir $Dir
    Save-ThatchTextures -Dir $Dir
    Save-GrateTexture -Path (Join-Path $Dir 'grate.png')
}
