# U7 newwoods textures: original pixel art (procedural, seeded) for the azalea, trumpet (blossom)
# and ashen (ancient) woods, their leaves, saplings and the Enchanted Fruit.
#
# Dot-sourced by tools/pack/newwoods.ps1, which records the written files for the unit.
# Vertical planks, bookshelves and ladders use U6's drawing functions (tools/textures/woodvariants.ps1).

. "$PSScriptRoot/lib.ps1"

# Colour ramps per wood (dark .. light). Picked to match Quark's map colours: azalea planks light green
# with brown bark, trumpet planks red with brown bark, ashen planks and bark terracotta-white.
$script:NwWoods = [ordered]@{
    azalea  = @{
        planks = @('#3f4a1c', '#7a8a3a', '#8f9f47', '#a2b257', '#b4c369')
        bark   = @('#2e2416', '#4a3a24', '#5d4a2e', '#6f5a39', '#86704a')
        moss   = @('#3d5a1e', '#56782a', '#6c9234')
        inner  = @('#7d8d3c', '#98a84d', '#aab95d', '#bccb70')    # stripped log / log top
    }
    blossom = @{
        planks = @('#4a1e1c', '#8a3a33', '#9e463c', '#b05447', '#c06353')
        bark   = @('#211510', '#3a261b', '#4a3123', '#5a3d2b', '#6c4b35')
        moss   = $null
        inner  = @('#8e3f36', '#a84c40', '#bb5a4b', '#cb6a58')
    }
    ancient = @{
        planks = @('#7c6a63', '#b8a49b', '#c8b6ad', '#d6c5bc', '#e3d5cd')
        bark   = @('#4a4440', '#7c766f', '#9a948c', '#b7b1a8', '#d0cbc2')
        moss   = $null
        inner  = @('#b39f96', '#c7b4ab', '#d6c5bc', '#e5d8d0')
    }
}

# Leaf colours (dark .. light) + a flower/highlight colour.
$script:NwLeaves = [ordered]@{
    ancient         = @{ ramp = @('#8f7f9c', '#b3a3bf', '#cdbfd6', '#e2d7e8', '#f3edf6'); spot = '#ffffff' }
    blue_blossom    = @{ ramp = @('#2f5f8f', '#4583b8', '#62a1d3', '#86bfe6', '#b2dcf5'); spot = '#e6f6ff' }
    lavender_blossom = @{ ramp = @('#7a4a8c', '#9c63ad', '#b87fc6', '#d29fdb', '#e8c3ee'); spot = '#fbe8ff' }
    orange_blossom  = @{ ramp = @('#9a3f14', '#c5591c', '#e07426', '#ef9440', '#f8b866'); spot = '#ffe0a8' }
    yellow_blossom  = @{ ramp = @('#9a7a12', '#c49e1c', '#ddbb2c', '#ecd24c', '#f7e888'); spot = '#fffbd6' }
    red_blossom     = @{ ramp = @('#7a1414', '#a5201c', '#c7302a', '#de4c3e', '#f07a64'); spot = '#ffc8b8' }
}

function New-NwGrid([int]$Fill = 0) {
    $g = [int[, ]]::new(16, 16)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $g[$x, $y] = $Fill } }
    return , $g
}

# Bark: vertical plates separated by dark crevices that wander sideways now and then.
function New-NwBarkTexture([string]$Path, [object[]]$Ramp, [int]$Seed, [object[]]$Moss) {
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    $g = New-NwGrid 2
    $cracks = @(1, 5, 9, 12)
    foreach ($c0 in $cracks) {
        $x = $c0 + $rng.Next(-1, 2)
        for ($y = 0; $y -lt 16; $y++) {
            if ($rng.NextDouble() -lt 0.18) { $x = ($x + $rng.Next(-1, 2) + 16) % 16 }
            $g[$x, $y] = 0
            $g[(($x + 1) % 16), $y] = [Math]::Max($g[(($x + 1) % 16), $y], 1)
        }
    }
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            if ($g[$x, $y] -ne 2) { continue }
            $r = $rng.NextDouble()
            $g[$x, $y] = if ($r -lt 0.3) { 3 } elseif ($r -lt 0.4) { $n - 1 } elseif ($r -lt 0.55) { 1 } else { 2 }
        }
    }
    $ramp = @($Ramp)
    if ($Moss) {
        # moss patches (azalea): a few blobs over the bark
        $ramp = @($Ramp) + @($Moss)
        $noise = New-NoiseGrid -Seed ($Seed + 7) -Size 16 -Cell 8 -Octaves 2
        for ($y = 0; $y -lt 16; $y++) {
            for ($x = 0; $x -lt 16; $x++) {
                $v = $noise[$x, $y]
                if ($v -gt 0.66) { $g[$x, $y] = $n + [Math]::Min($Moss.Count - 1, [int](($v - 0.66) / 0.34 * $Moss.Count) + ($(if ($g[$x, $y] -eq 0) { 0 } else { 1 }))) }
            }
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Stripped side: smooth vertical grain in the inner colours with a few darker fibres.
function New-NwStrippedSideTexture([string]$Path, [object[]]$Ramp, [int]$Seed) {
    $rng = [System.Random]::new($Seed)
    $g = New-NwGrid 2
    for ($x = 0; $x -lt 16; $x++) {
        $base = if ($x % 5 -eq 0) { 1 } elseif ($x % 5 -eq 3) { 3 } else { 2 }
        for ($y = 0; $y -lt 16; $y++) {
            $v = $base
            if ($rng.NextDouble() -lt 0.12) { $v = [Math]::Max(0, $v - 1) }
            $g[$x, $y] = $v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $Ramp -Path $Path
}

# Log top: bark rim, then concentric rounded rings of the inner wood with a dark core.
function New-NwLogTopTexture([string]$Path, [object[]]$Bark, [object[]]$Inner, [int]$Seed, [switch]$Stripped) {
    $rng = [System.Random]::new($Seed)
    $ramp = @($Bark) + @($Inner)
    $nb = $Bark.Count
    $g = New-NwGrid 0
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $edge = [Math]::Min([Math]::Min($x, $y), [Math]::Min(15 - $x, 15 - $y))
            if ($edge -eq 0) {
                $g[$x, $y] = if ($Stripped) { $nb + 1 } else { $rng.Next(1, 3) }
                continue
            }
            $dx = [Math]::Abs($x - 7.5); $dy = [Math]::Abs($y - 7.5)
            $r = [Math]::Max($dx, $dy) * 0.7 + [Math]::Sqrt($dx * $dx + $dy * $dy) * 0.3
            $ring = [int][Math]::Floor($r + 0.25)
            $v = if ($ring % 2 -eq 0) { 3 } else { 2 }
            if ($ring -le 1) { $v = 1 }
            if ($rng.NextDouble() -lt 0.06) { $v = [Math]::Max(0, $v - 1) }
            $g[$x, $y] = $nb + $v
        }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Door halves: a frame, two boards with a cross brace (bottom) or windows (top); '.' = see-through.
function New-NwDoorTextures([string]$TopPath, [string]$BottomPath, [string]$ItemPath, [object[]]$Ramp, [object[]]$Dark, [string]$Style) {
    $top = switch ($Style) {
        'azalea' { @(
                '1111111111111111', '1333333113333331', '13....3113....31', '13.33.3113.33.31', '13.33.3113.33.31', '13....3113....31',
                '1333333113333331', '1444444114444441', '1222222112222221', '1233332112333321', '1232232112322321', '1233332112333321',
                '1222222112222221', '1222222112222221', '1233332112333321', '1222222112222221') }
        'blossom' { @(
                '1111111111111111', '1222222222222221', '1222233333322221', '1223.......33221', '123..........321', '123..........321',
                '123..........321', '1223........3221', '1222333333332221', '1222222222222221', '1444444444444441', '1233333113333321',
                '1232222112222321', '1232222112222321', '1232222112222321', '1233333113333321') }
        default { @(
                '1111111111111111', '1333333333333331', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31',
                '13.3.3.33.3.3.31', '1333333333333331', '1444444444444441', '1222222222222221', '1233333333333321', '1232222222222321',
                '1232222222222321', '1232222222222321', '1232222222222321', '1232222222222321') }
    }
    $bottom = switch ($Style) {
        'azalea' { @(
                '1222222112222221', '1233332112333321', '1232232112322321', '1233332112333321', '1222222112222221', '1444444114444441',
                '1222222112222221', '12h2222112222221', '12h2222112222221', '1222222112222221', '1233332112333321', '1232232112322321',
                '1233332112333321', '1222222112222221', '1444444444444441', '1111111111111111') }
        'blossom' { @(
                '1232222112222321', '1232222112222321', '1233333113333321', '1222222222222221', '1444444444444441', '1222222112222221',
                '1233333113333321', '12h2222112222321', '12h2222112222321', '1232222112222321', '1233333113333321', '1222222112222221',
                '1222222112222221', '1222222112222221', '1444444444444441', '1111111111111111') }
        default { @(
                '1232222222222321', '1232222222222321', '1232222222222321', '1233333333333321', '1222222222222221', '1444444444444441',
                '1222222222222221', '12h2222222222221', '12h2222222222221', '1222222222222221', '1233333333333321', '1232222222222321',
                '1232222222222321', '1233333333333321', '1444444444444441', '1111111111111111') }
    }
    $pal = @{ '1' = $Ramp[0]; '2' = $Ramp[2]; '3' = $Ramp[1]; '4' = $Ramp[3]; 'h' = $Dark[0] }
    Save-PixelArt -Rows $top -Palette $pal -Path $TopPath
    Save-PixelArt -Rows $bottom -Palette $pal -Path $BottomPath
    # item: the two halves squeezed to 8 px wide (every other column), centred
    $rows = @()
    foreach ($half in @($top, $bottom)) {
        for ($y = 0; $y -lt 16; $y += 2) {
            $src = $half[$y]
            $sb = [System.Text.StringBuilder]::new('....')
            foreach ($x in 0, 2, 4, 6, 9, 11, 13, 15) { [void]$sb.Append($src[$x]) }
            [void]$sb.Append('....')
            $rows += $sb.ToString()
        }
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path $ItemPath
}

# Trapdoor: a frame with a 2x2 grid of openings ('.') and a brace.
function New-NwTrapdoorTexture([string]$Path, [object[]]$Ramp, [string]$Style) {
    $rows = switch ($Style) {
        'azalea' { @(
                '1111111111111111', '1222222112222221', '12....2112....21', '12.33.2112.33.21', '12.33.2112.33.21', '12....2112....21',
                '1222222112222221', '1444444444444441', '1444444444444441', '1222222112222221', '12....2112....21', '12.33.2112.33.21',
                '12.33.2112.33.21', '12....2112....21', '1222222112222221', '1111111111111111') }
        'blossom' { @(
                '1111111111111111', '1222222222222221', '1223333333333221', '123..........321', '123..........321', '123..........321',
                '1223333333333221', '1444444444444441', '1444444444444441', '1223333333333221', '123..........321', '123..........321',
                '123..........321', '1223333333333221', '1222222222222221', '1111111111111111') }
        default { @(
                '1111111111111111', '1232323232323231', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31',
                '1232323232323231', '1444444444444441', '1444444444444441', '1232323232323231', '13.3.3.33.3.3.31', '13.3.3.33.3.3.31',
                '13.3.3.33.3.3.31', '13.3.3.33.3.3.31', '1232323232323231', '1111111111111111') }
    }
    $pal = @{ '1' = $Ramp[0]; '2' = $Ramp[2]; '3' = $Ramp[1]; '4' = $Ramp[3] }
    Save-PixelArt -Rows $rows -Palette $pal -Path $Path
}

# Leaves: clustered leaf noise with see-through gaps and a few bright blossoms / highlights.
function New-NwLeavesTexture([string]$Path, [object[]]$Ramp, [string]$Spot, [int]$Seed, [double]$Holes = 0.16, [double]$Spots = 0.05) {
    $rng = [System.Random]::new($Seed)
    $noise = New-NoiseGrid -Seed $Seed -Size 16 -Cell 4 -Octaves 2
    $fine = New-NoiseGrid -Seed ($Seed + 1) -Size 16 -Cell 2 -Octaves 1
    $n = $Ramp.Count
    $ramp = @($Ramp) + @($Spot)
    $g = New-NwGrid 0
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $v = $noise[$x, $y] * 0.7 + $fine[$x, $y] * 0.3
            if ($fine[$x, $y] -lt $Holes * 1.6 -and $rng.NextDouble() -lt 0.85) { $g[$x, $y] = -1; continue }
            $i = [Math]::Max(0, [Math]::Min($n - 1, [int][Math]::Floor($v * $n * 1.15 - 0.3)))
            if ($rng.NextDouble() -lt $Spots) { $i = $n }
            $g[$x, $y] = $i
        }
    }
    # dark undersides: a pixel below a hole is shaded
    for ($y = 15; $y -ge 1; $y--) {
        for ($x = 0; $x -lt 16; $x++) { if ($g[$x, ($y - 1)] -eq -1 -and $g[$x, $y] -gt 0 -and $g[$x, $y] -lt $n) { $g[$x, $y] = [Math]::Max(0, $g[$x, $y] - 1) } }
    }
    Save-IndexGrid -Grid $g -Ramp $ramp -Path $Path
}

# Sapling: a thin stem with a small leafy crown of the leaf colours.
function New-NwSaplingTexture([string]$Path, [object[]]$Leaf, [string]$Spot, [object[]]$Stem, [int]$Seed, [string]$Shape) {
    $rows = switch ($Shape) {
        'ancient' { @(
                '................', '.......44.......', '.....443344.....', '....43322334....', '...4332..2334...', '....33.ss.33....',
                '.....4.ss.4.....', '...443.ss.344...', '..43322ss22334..', '...332.ss.233...', '....4..ss..4....', '.......ss.......',
                '......sss.......', '.......ss.......', '.......dd.......', '......d..d......') }
        default { @(
                '................', '......4334......', '....43*2233.....', '...4322332*3....', '...32233*2234...', '..4*322ss3223...',
                '...4322ss2234...', '....33.ss.33....', '.......ss.......', '.....4.ss.......', '....43.ss.......', '.....3sss.......',
                '.......ss.......', '.......ss.......', '.......dd.......', '......d..d......') }
    }
    $pal = @{ '2' = $Leaf[1]; '3' = $Leaf[2]; '4' = $Leaf[3]; '*' = $Spot; 's' = $Stem[2]; 'd' = $Stem[1] }
    Save-PixelArt -Rows $rows -Palette $pal -Path $Path
}

# Enchanted Fruit: a round violet-pink fruit with a leaf, a stem and a white glint.
function New-NwFruitTexture([string]$Path) {
    $rows = @(
        '................', '........s.......', '.......sll......', '.......s.ll.....', '....aabsbbaa....', '...abbccccbba...',
        '..abccddddccba..', '..abcddwwddcba..', '..abcddwdddcba..', '..abccdddddcba..', '..abbccdddccbba.', '...abbcccccbba..',
        '....abbbbbbba...', '.....aaabbaa....', '.......aa.......', '................')
    $pal = @{ a = '#3d1650'; b = '#6a2a86'; c = '#9a46b8'; d = '#c972d9'; w = '#fbe6ff'; s = '#4a3018'; l = '#4c8a2c' }
    Save-PixelArt -Rows $rows -Palette $pal -Path $Path
}

# Draws every U7 texture; returns the written paths. U6's woodvariants textures functions must be loaded
# (New-VerticalPlanksTexture, New-BookshelfTexture, New-LadderTexture).
function Write-NewWoodsTextures([string]$BlocksDir, [string]$ItemsDir) {
    $out = @()
    $seed = 700
    foreach ($w in $script:NwWoods.Keys) {
        $d = $script:NwWoods[$w]
        $seed += 10
        $p = Join-Path $BlocksDir "${w}_planks.png"; New-PlankTexture -Path $p -Ramp $d.planks -Seed $seed; $out += $p
        $p = Join-Path $BlocksDir "${w}_log.png"; New-NwBarkTexture $p $d.bark ($seed + 1) $d.moss; $out += $p
        $p = Join-Path $BlocksDir "${w}_log_top.png"; New-NwLogTopTexture $p $d.bark $d.inner ($seed + 2); $out += $p
        $p = Join-Path $BlocksDir "stripped_${w}_log.png"; New-NwStrippedSideTexture $p $d.inner ($seed + 3); $out += $p
        $p = Join-Path $BlocksDir "stripped_${w}_log_top.png"; New-NwLogTopTexture $p $d.inner $d.inner ($seed + 4) -Stripped; $out += $p
        $p = Join-Path $BlocksDir "vertical_${w}_planks.png"; New-VerticalPlanksTexture $p $d.planks ($seed + 5); $out += $p
        $p = Join-Path $BlocksDir "${w}_bookshelf.png"; New-BookshelfTexture $p $d.planks ($seed + 6); $out += $p
        $p = Join-Path $BlocksDir "${w}_ladder.png"; New-LadderTexture $p $d.planks; $out += $p
        $top = Join-Path $BlocksDir "${w}_door_top.png"; $bottom = Join-Path $BlocksDir "${w}_door_bottom.png"; $item = Join-Path $ItemsDir "${w}_door.png"
        New-NwDoorTextures $top $bottom $item $d.planks $d.bark $w; $out += $top, $bottom, $item
        $p = Join-Path $BlocksDir "${w}_trapdoor.png"; New-NwTrapdoorTexture $p $d.planks $w; $out += $p
    }
    foreach ($l in $script:NwLeaves.Keys) {
        $d = $script:NwLeaves[$l]
        $seed += 3
        $holes = if ($l -eq 'ancient') { 0.12 } else { 0.16 }
        $p = Join-Path $BlocksDir "${l}_leaves.png"; New-NwLeavesTexture $p $d.ramp $d.spot $seed $holes; $out += $p
        $stem = if ($l -eq 'ancient') { $script:NwWoods.ancient.bark } else { $script:NwWoods.blossom.bark }
        $shape = if ($l -eq 'ancient') { 'ancient' } else { 'blossom' }
        $p = Join-Path $BlocksDir "${l}_sapling.png"; New-NwSaplingTexture $p $d.ramp $d.spot $stem $seed $shape; $out += $p
    }
    $p = Join-Path $ItemsDir 'ancient_fruit.png'; New-NwFruitTexture $p; $out += $p
    return $out
}
