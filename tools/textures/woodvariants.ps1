# U6 woodvariants textures: original pixel art (procedural, seeded) for the blocks that can't reuse a
# vanilla texture: vertical planks, bookshelf sides, ladders, the bamboo posts and the post chain.
# Everything else (logs, leaves, planks) references the game's own textures.
#
# Dot-sourced by tools/pack/woodvariants.ps1 (which records the files for the unit). U7 reuses the
# functions for its woods: New-VerticalPlanksTexture, New-BookshelfTexture, New-LadderTexture.

. "$PSScriptRoot/lib.ps1"

# Plank colour ramps per wood (dark gap .. light highlight), picked to sit next to the vanilla planks.
$script:WoodRamps = [ordered]@{
    oak      = @('#4f3a20', '#8a6a3c', '#a5824e', '#b8925a', '#c7a46b')
    spruce   = @('#3a2716', '#5a3f24', '#6b4c2c', '#7a5933', '#8c6a3f')
    birch    = @('#8c7748', '#b39e66', '#c4b077', '#d2bf86', '#ddcc96')
    jungle   = @('#5a3c25', '#8c6243', '#a0724f', '#b0805b', '#bf8f68')
    acacia   = @('#6b301a', '#97502c', '#ab5d34', '#bb693b', '#c97747')
    dark_oak = @('#24170b', '#3b2713', '#48301a', '#53381e', '#5f4124')
    mangrove = @('#4a1a1a', '#6a2f2b', '#783733', '#86403a', '#934a43')
    cherry   = @('#a46c69', '#d09d93', '#dfb0a5', '#e8bdb2', '#f1cbc1')
    bamboo   = @('#8f7a2a', '#bba546', '#cbb452', '#d6c05f', '#e1cd70')
    crimson  = @('#3c1a29', '#5c2c41', '#69334b', '#763b55', '#83455f')
    warped   = @('#16403b', '#245a55', '#2b6963', '#327670', '#3a847c')
}

# Saves the grid rotated by 90 degrees (rows become columns): boards run vertically.
function Save-RotatedIndexGrid([int[, ]]$Grid, [object[]]$Ramp, [string]$Path) {
    $w = $Grid.GetLength(0); $h = $Grid.GetLength(1)
    $r = [int[, ]]::new($h, $w)
    for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $nx = $h - 1 - $y; $r[$nx, $x] = $Grid[$x, $y] } }
    Save-IndexGrid -Grid $r -Ramp $Ramp -Path $Path
}

# Vertical planks: four boards running top to bottom, with grain streaks and board ends.
function New-VerticalPlanksTexture([string]$Path, [object[]]$Ramp, [int]$Seed) {
    $rng = [System.Random]::new($Seed)
    $n = $Ramp.Count
    $g = [int[, ]]::new(16, 16)
    for ($board = 0; $board -lt 4; $board++) {
        $end = $rng.Next(3, 13)
        $base = 1 + $rng.Next(0, [Math]::Max(1, $n - 2))
        for ($dy = 0; $dy -lt 4; $dy++) {
            $y = $board * 4 + $dy
            for ($x = 0; $x -lt 16; $x++) {
                $v = $base
                if ($dy -eq 3) { $v = 0 }
                elseif ($rng.NextDouble() -lt 0.2) { $v = [Math]::Max(1, $v - 1) }
                elseif ($dy -eq 0 -and $rng.NextDouble() -lt 0.45) { $v = [Math]::Min($n - 1, $v + 1) }
                if ($x -eq $end -and ($board % 2 -eq 0)) { $v = 0 }
                $g[$x, $y] = $v
            }
        }
    }
    Save-RotatedIndexGrid $g $Ramp $Path
}

# Bookshelf side: a planks frame (top, middle shelf, bottom) holding two rows of books.
function New-BookshelfTexture([string]$Path, [object[]]$Ramp, [int]$Seed) {
    $rng = [System.Random]::new($Seed)
    $books = @(
        @('#5b1a17', '#8c2a22', '#a8392c'), @('#1d3557', '#2f4f7f', '#466a9f'), @('#2d4a1f', '#3f6b2a', '#58893a'),
        @('#5a4320', '#87672f', '#a8843f'), @('#4a2a55', '#6a3f7a', '#87569a'), @('#6b5a46', '#9a8566', '#b8a383'),
        @('#20504f', '#2f7270', '#43928f')
    )
    $pal = @{}
    for ($i = 0; $i -lt $Ramp.Count; $i++) { $pal["$i"] = $Ramp[$i] }
    $letters = 'abcdefghijklmnopqrstu'
    for ($b = 0; $b -lt $books.Count; $b++) {
        for ($s = 0; $s -lt 3; $s++) { $pal[[string]$letters[$b * 3 + $s]] = $books[$b][$s] }
    }
    $pal['z'] = '#1a120a'   # shadow behind the books
    $rows = [string[]]::new(16)
    $grid = [char[, ]]::new(16, 16)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $grid[$x, $y] = 'z' } }
    # frame: rows 0, 7, 8, 15 and the side posts
    foreach ($x in 0..15) { $grid[$x, 0] = '4'; $grid[$x, 7] = '3'; $grid[$x, 8] = '1'; $grid[$x, 15] = '0' }
    foreach ($y in 0..15) { $grid[0, $y] = '2'; $grid[15, $y] = '1' }
    foreach ($x in 1..14) { $grid[$x, 1] = '0' }
    foreach ($x in 1..14) { $grid[$x, 9] = '0' }
    foreach ($shelf in @(@(2, 6), @(10, 14))) {
        $x = 1
        while ($x -lt 15) {
            $w = [Math]::Min(15 - $x, $rng.Next(1, 4))
            $book = $rng.Next(0, $books.Count)
            $top = $shelf[0] + $rng.Next(0, 2)
            if ($rng.NextDouble() -lt 0.12) { $x += $w; continue }   # a gap on the shelf
            for ($dx = 0; $dx -lt $w; $dx++) {
                for ($y = $top; $y -le $shelf[1]; $y++) {
                    $shade = if ($dx -eq 0) { 2 } elseif ($dx -eq $w - 1 -and $w -gt 1) { 0 } else { 1 }
                    if ($y -eq $top) { $shade = [Math]::Min(2, $shade + 1) }
                    $grid[($x + $dx), $y] = $letters[$book * 3 + $shade]
                }
            }
            $x += $w
        }
    }
    for ($y = 0; $y -lt 16; $y++) {
        $sb = [System.Text.StringBuilder]::new()
        for ($x = 0; $x -lt 16; $x++) { [void]$sb.Append($grid[$x, $y]) }
        $rows[$y] = $sb.ToString()
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path $Path
}

# Ladder: two rails and four rungs on a transparent background.
function New-LadderTexture([string]$Path, [object[]]$Ramp) {
    $rows = @(
        '.32..........32.',
        '.21..........21.',
        '.2144444444443l.',
        '.21000000000021.',
        '.32..........32.',
        '.21..........21.',
        '.2144444444443l.',
        '.21000000000021.',
        '.32..........32.',
        '.21..........21.',
        '.2144444444443l.',
        '.21000000000021.',
        '.32..........32.',
        '.21..........21.',
        '.2144444444443l.',
        '.21000000000021.'
    )
    $pal = @{ '0' = $Ramp[0]; '1' = $Ramp[1]; '2' = $Ramp[2]; '3' = $Ramp[3]; '4' = $Ramp[4]; 'l' = $Ramp[2] }
    Save-PixelArt -Rows $rows -Palette $pal -Path $Path
}

# Bamboo post: vertical stalks with nodes (any 4 px wide strip shows a stalk).
function New-BambooPostTexture([string]$Path, [object[]]$Ramp) {
    $rows = foreach ($y in 0..15) {
        $node = ($y -eq 4 -or $y -eq 11)
        $sb = [System.Text.StringBuilder]::new()
        foreach ($x in 0..15) {
            $c = switch ($x % 4) { 0 { '1' } 1 { '3' } 2 { '2' } 3 { '0' } }
            if ($node) { $c = if ($x % 4 -eq 3) { '0' } else { '4' } }
            [void]$sb.Append($c)
        }
        $sb.ToString()
    }
    $pal = @{ '0' = $Ramp[0]; '1' = $Ramp[1]; '2' = $Ramp[2]; '3' = $Ramp[3]; '4' = $Ramp[4] }
    Save-PixelArt -Rows @($rows) -Palette $pal -Path $Path
}

# The small chain drawn on post sides: two 3 px wide link strips (columns 0..2 and 3..5).
function New-PostChainTexture([string]$Path) {
    $rows = @(
        '.a..b...........', 'aca.dbd.........', 'a.adb.b.........', 'aca.dbd.........', '.a..b...........', '.a..b...........',
        'aca.dbd.........', 'a.adb.b.........', 'aca.dbd.........', '.a..b...........', '.a..b...........', 'aca.dbd.........',
        'a.adb.b.........', 'aca.dbd.........', '.a..b...........', '.a..b...........')
    Save-PixelArt -Rows $rows -Palette @{ a = '#3d4554'; b = '#2c323d'; c = '#5d6a7f'; d = '#4a5466' } -Path $Path
}

# Draws every U6 texture into $BlocksDir; returns the written paths.
function Write-WoodVariantTextures([string]$BlocksDir) {
    $out = @()
    $seed = 600
    foreach ($w in $script:WoodRamps.Keys) {
        $ramp = $script:WoodRamps[$w]
        $seed++
        $p = Join-Path $BlocksDir "vertical_${w}_planks.png"; New-VerticalPlanksTexture $p $ramp $seed; $out += $p
        if ($w -ne 'oak') {
            $p = Join-Path $BlocksDir "${w}_bookshelf.png"; New-BookshelfTexture $p $ramp ($seed + 50); $out += $p
            $p = Join-Path $BlocksDir "${w}_ladder.png"; New-LadderTexture $p $ramp; $out += $p
        }
    }
    $p = Join-Path $BlocksDir 'bamboo_post.png'; New-BambooPostTexture $p @('#3f5a14', '#5d7f1d', '#6f9424', '#82a82c', '#9cc23a'); $out += $p
    $p = Join-Path $BlocksDir 'stripped_bamboo_post.png'; New-BambooPostTexture $p $script:WoodRamps.bamboo; $out += $p
    $p = Join-Path $BlocksDir 'post_chain.png'; New-PostChainTexture $p; $out += $p
    return $out
}
