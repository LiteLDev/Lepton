# U28 worldsurface textures: original pixel art for the Monster Box and the Glimmering Weald blocks.
# Dot-sourced by tools/pack/worldsurface.ps1, which calls New-WorldsurfaceTextures with a
# function mapping a texture name to its output path (and recording the file for the unit).
. "$PSScriptRoot/lib.ps1"

# Seeded noise rendered into palette keys: $Keys[0] darkest .. $Keys[-1] lightest.
function Get-NoiseRows([int]$Seed, [string[]]$Keys, [int]$Cell = 4, [int]$Octaves = 3) {
    $grid = ConvertTo-IndexGrid (New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves $Octaves) $Keys.Count
    $rows = for ($y = 0; $y -lt 16; $y++) { -join (0..15 | ForEach-Object { $Keys[$grid[$_, $y]] }) }
    return , @($rows)
}

# Puts round "spots" (key $Key, rim $Rim) at the given centres onto rows.
function Add-Spots([string[]]$Rows, [object[]]$Centres, [string]$Key, [string]$Rim) {
    $b = [System.Collections.Generic.List[char[]]]::new()
    foreach ($row in $Rows) { $b.Add($row.ToCharArray()) }
    foreach ($c in $Centres) {
        $cx = $c[0]; $cy = $c[1]; $r = $c[2]
        for ($y = $cy - $r - 1; $y -le $cy + $r + 1; $y++) {
            for ($x = $cx - $r - 1; $x -le $cx + $r + 1; $x++) {
                $xx = ($x + 16) % 16; $yy = ($y + 16) % 16
                $d = [Math]::Sqrt(($x - $cx) * ($x - $cx) + ($y - $cy) * ($y - $cy))
                if ($d -le $r) { $b[$yy][$xx] = $Key[0] } elseif ($Rim -and $d -le $r + 0.9) { $b[$yy][$xx] = $Rim[0] }
            }
        }
    }
    $out = foreach ($row in $b) { [string]::new($row) }
    return , @($out)
}

function New-WorldsurfaceTextures([scriptblock]$Out) {
    # ---- Monster Box: a riveted dark-iron cage with a smouldering core ----------------------
    $cage = @(
        'ffffffffffffffff',
        'fRbbbbbbbbbbbbRf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fbbbbbbbbbbbbbbf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fbbbbbbbbbbbbbbf',
        'fb.b..b..b..b.bf',
        'fb.b..b..b..b.bf',
        'fRbbbbbbbbbbbbRf',
        'ffffffffffffffff')
    $cagePal = @{ f = '#24262b'; b = '#4a4e57'; R = '#8d939e' }
    Save-PixelArt -Rows $cage -Palette $cagePal -Path (& $Out 'blocks' 'monster_box')
    $core = Get-NoiseRows 2801 @('k', 'k', 'm', 'n') 4 2
    $core = Merge-Rows $core @(
        '................',
        '................',
        '................',
        '................',
        '................',
        '...ee......ee...',
        '...eye....eye...',
        '....ee....ee....',
        '................',
        '................',
        '.....hhhhhh.....',
        '....h.h..h.h....',
        '................',
        '................',
        '................',
        '................')
    Save-PixelArt -Rows $core -Palette @{ k = '#140b0b'; m = '#261210'; n = '#3a1a12'; e = '#e0461a'; y = '#ffd27a'; h = '#a5301a' } -Path (& $Out 'blocks' 'monster_box_core')

    # ---- Glow Shroom (cross sprite) ---------------------------------------------------------
    $shroom = @(
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '.....acccca.....',
        '....acwccwca....',
        '....dccccccd....',
        '.....ddccdd.....',
        '.......st.......',
        '.......st.......',
        '......sst.......',
        '.......st.......',
        '......ssst......',
        '................')
    $shroomPal = @{ a = '#3db4cc'; c = '#7fe4f2'; w = '#d6fbff'; d = '#2a85a0'; s = '#d3ddd6'; t = '#9fb1aa' }
    Save-PixelArt -Rows $shroom -Palette $shroomPal -Path (& $Out 'blocks' 'glow_shroom')

    # ---- Glow Lichen Growth (cross sprite) --------------------------------------------------
    $growth = @(
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '.......y........',
        '....y..g..y.....',
        '...yg.gyg.gy....',
        '....gg.g.gg.....',
        '..y..gggg..y....',
        '...gg.gg.gg.....',
        '.....gkkkg......',
        '......kkk.......',
        '................')
    Save-PixelArt -Rows $growth -Palette @{ g = '#5f8f6f'; k = '#476b54'; y = '#e4f7a2' } -Path (& $Out 'blocks' 'glow_lichen_growth')

    # ---- Glow Shroom Block: glowing cyan skin with pale spots ---------------------------------
    $skin = Get-NoiseRows 2802 @('d', 'c', 'c', 'e') 4 2
    $skin = Add-Spots $skin @(@(3, 3, 1), @(11, 2, 1), @(7, 8, 1), @(13, 11, 1), @(2, 12, 1), @(9, 14, 0)) 'w' 'p'
    Save-PixelArt -Rows $skin -Palette @{ d = '#2a8fa8'; c = '#41b9d1'; e = '#5ccbe0'; w = '#dffbff'; p = '#9ee9f5' } -Path (& $Out 'blocks' 'glow_shroom_block')

    # ---- Pores (inside of both huge-shroom blocks) --------------------------------------------
    $pores = Get-NoiseRows 2803 @('b', 'b', 'n') 4 2
    $pores = Add-Spots $pores @(@(2, 2, 0), @(6, 1, 0), @(10, 3, 0), @(14, 2, 0), @(4, 6, 0), @(8, 5, 0), @(12, 7, 0),
        @(1, 9, 0), @(6, 10, 0), @(10, 9, 0), @(14, 11, 0), @(3, 13, 0), @(8, 14, 0), @(12, 14, 0)) 'o' ''
    Save-PixelArt -Rows $pores -Palette @{ b = '#e3dccb'; n = '#d2cbb9'; o = '#9fd6dc' } -Path (& $Out 'blocks' 'glow_shroom_inside')

    # ---- Glow Shroom Stem: pale fibrous stem ---------------------------------------------------
    $stem = for ($y = 0; $y -lt 16; $y++) {
        -join (0..15 | ForEach-Object {
                $v = (($_ * 7 + [int][Math]::Floor($y / 5) * 3) % 5)
                if ($_ % 4 -eq 1) { 't' } elseif ($v -eq 0) { 's' } elseif ($v -eq 3) { 'h' } else { 'm' }
            })
    }
    Save-PixelArt -Rows $stem -Palette @{ m = '#d7e1db'; s = '#c3d0c9'; t = '#a9bab3'; h = '#e8f1ec' } -Path (& $Out 'blocks' 'glow_shroom_stem')

    # ---- Glow Shroom Ring: the shelf seen from above / below -----------------------------------
    # rows 0..15 = from the outer edge (row 0) towards the wall (row 15)
    $ring = @(
        '..aaaaaaaaaaaa..',
        '.acccccccccccca.',
        'acwwwwwwwwwwwwca',
        'acwccccccccccwca',
        'acwcddddddddcwca',
        'acwcdccccccdcwca',
        'acwcdcwwwwcdcwca',
        'acwcdcwddwcdcwca',
        'acwcdcwddwcdcwca',
        'acwcdcwwwwcdcwca',
        'acwcdccccccdcwca',
        'acwcddddddddcwca',
        'acwccccccccccwca',
        'acwwwwwwwwwwwwca',
        'acccccccccccccca',
        'aaaaaaaaaaaaaaaa')
    Save-PixelArt -Rows $ring -Palette @{ a = '#2a85a0'; c = '#56c6db'; w = '#bff4fb'; d = '#3aa6bf' } -Path (& $Out 'blocks' 'glow_shroom_ring')
}
