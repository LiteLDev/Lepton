# Textures of U24 frames (glass item frames, dyed item frames). Original pixel art drawn from
# simple per-pixel rules; run it again to regenerate byte-identical PNGs.
#
#   pwsh tools/textures/frames.ps1
. "$PSScriptRoot/lib.ps1"
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp/textures'

# Builds rows of palette keys from a per-pixel rule ($Fn returns a one-character key).
function New-Rows([int]$W, [int]$H, [scriptblock]$Fn) {
    $rows = for ($y = 0; $y -lt $H; $y++) { -join (0..($W - 1) | ForEach-Object { & $Fn $_ $y }) }
    return , @($rows)
}

function In([int]$v, [int]$lo, [int]$hi) { return $v -ge $lo -and $v -le $hi }

# ---- glass frame block (world-aligned UVs: the front view of the frame in 2..13, the frame's
# edges in rows 0/15 and columns 0/15 for the side faces of the 1 px deep border) ------------------
$glassPal = @{ A = '#f0e6f6fa'; s = '#e0a7cdd8'; d = '#f08fb6c2'; g = '#3ccbe9f2'; w = '#a8ffffff' }
$glassBlock = New-Rows 16 16 {
    param($x, $y)
    if (($y -eq 0 -or $y -eq 15) -and (In $x 2 13)) { return 'A' }
    if (($x -eq 0 -or $x -eq 15) -and (In $y 2 13)) { return 'A' }
    if ((In $x 2 13) -and (In $y 2 13)) {
        $ring = $x -eq 2 -or $x -eq 13 -or $y -eq 2 -or $y -eq 13
        if ($ring) {
            if (($x -eq 2 -or $x -eq 13) -and ($y -eq 2 -or $y -eq 13)) { return 'd' }
            if ($y -eq 2 -or $x -eq 2) { return 'A' }
            return 's'
        }
        if (($x - $y -eq 3 -and (In $x 7 10)) -or ($x - $y -eq 4 -and (In $x 9 11))) { return 'w' }
        if ($x -eq 4 -and $y -eq 10) { return 'w' }
        return 'g'
    }
    return '.'
}
Save-PixelArt -Rows $glassBlock -Palette $glassPal -Path (Join-Path $rp 'blocks/frames/glass_item_frame.png')

# ---- item icons: a frame seen from the front (outer ring 1..14, inner ring 2..13, inside 3..12) --
function New-FrameIcon([scriptblock]$Inside) {
    return New-Rows 16 16 {
        param($x, $y)
        if (-not ((In $x 1 14) -and (In $y 1 14))) { return '.' }
        if ($x -eq 1 -or $x -eq 14 -or $y -eq 1 -or $y -eq 14) {
            if (($x -eq 1 -or $x -eq 14) -and ($y -eq 1 -or $y -eq 14)) { return 'k' }
            return 'o'
        }
        if ($x -eq 2 -or $x -eq 13 -or $y -eq 2 -or $y -eq 13) {
            if ($y -eq 2 -or $x -eq 2) { return 'h' }
            if (($x + $y) % 5 -eq 0) { return 'o' }
            return 'm'
        }
        return & $Inside $x $y
    }
}

# glass frames: pale glass ring, clear inside with glints
$glassInside = {
    param($x, $y)
    if (($x - $y -eq 2 -and (In $x 6 9)) -or ($x - $y -eq 3 -and (In $x 8 10))) { return 'w' }
    if ($x -eq 4 -and $y -eq 10) { return 'w' }
    return 'g'
}
$glassIcon = New-FrameIcon $glassInside
Save-PixelArt -Rows $glassIcon -Palette @{ k = '#5f8590'; o = '#86b3c0'; m = '#c3e2ea'; h = '#eef9fb'; g = '#66d2eef4'; w = '#e8ffffff' } `
    -Path (Join-Path $rp 'items/frames/glass_item_frame.png')
Save-PixelArt -Rows $glassIcon -Palette @{ k = '#2c6b62'; o = '#3f9c8c'; m = '#7fe3c9'; h = '#d8fff1'; g = '#6693f0d6'; w = '#f0f4fff8' } `
    -Path (Join-Path $rp 'items/frames/glowing_glass_item_frame.png')

# dyed frames: wooden ring around a leather backing; the `dyed` icons are the same in greys (the
# game multiplies them by the dye colour)
$leatherInside = {
    param($x, $y)
    $n = ($x * 3 + $y * 5 + ($x * $y) % 3) % 7
    if ($n -eq 0) { return 'd' }
    if ($n -eq 3) { return 'l' }
    return 'b'
}
$frameIcon = New-FrameIcon $leatherInside
Save-PixelArt -Rows $frameIcon -Palette @{ k = '#3a2814'; o = '#5e4022'; m = '#8f6538'; h = '#b48a55'; b = '#a86e3c'; d = '#8a5630'; l = '#c48a52' } `
    -Path (Join-Path $rp 'items/frames/dyed_item_frame.png')
Save-PixelArt -Rows $frameIcon -Palette @{ k = '#5a5a5a'; o = '#8a8a8a'; m = '#c4c4c4'; h = '#eeeeee'; b = '#d6d6d6'; d = '#b9b9b9'; l = '#f2f2f2' } `
    -Path (Join-Path $rp 'items/frames/dyed_item_frame_dyed.png')
$glowInside = {
    param($x, $y)
    if (($x -eq 3 -or $x -eq 12) -and ($y -eq 3 -or $y -eq 12)) { return 's' }
    return & $leatherInside $x $y
}
$glowIcon = New-FrameIcon $glowInside
Save-PixelArt -Rows $glowIcon -Palette @{ k = '#3a2814'; o = '#6a5426'; m = '#b89a48'; h = '#f0d77c'; b = '#6fae98'; d = '#4f8a78'; l = '#94d6bc'; s = '#fff6c0' } `
    -Path (Join-Path $rp 'items/frames/dyed_glow_item_frame.png')
Save-PixelArt -Rows $glowIcon -Palette @{ k = '#5a5a5a'; o = '#8f8f8f'; m = '#cdcdcd'; h = '#f6f6f6'; b = '#d2d2d2'; d = '#b4b4b4'; l = '#ececec'; s = '#ffffff' } `
    -Path (Join-Path $rp 'items/frames/dyed_glow_item_frame_dyed.png')

# ---- dyed frame overlay entity (32x16, greys: tinted by the dye colour) -----------------------
# left half: the item frame seen from the front (ring 2..13, leather 3..12); right half: the map
# frame (ring 0..15). Side faces of the border sample the ring.
$overlay = New-Rows 32 16 {
    param($x, $y)
    if ($x -lt 16) {
        if (-not ((In $x 2 13) -and (In $y 2 13))) { return '.' }
        if ($x -eq 2 -or $x -eq 13 -or $y -eq 2 -or $y -eq 13) {
            if ((($x + $y) % 4) -eq 0) { return 'o' }
            if ($y -eq 2 -or $x -eq 2) { return 'h' }
            return 'm'
        }
        return & $leatherInside $x $y
    }
    $u = $x - 16
    if ($u -eq 0 -or $u -eq 15 -or $y -eq 0 -or $y -eq 15) {
        if ((($u + $y) % 4) -eq 0) { return 'o' }
        if ($y -eq 0 -or $u -eq 0) { return 'h' }
        return 'm'
    }
    return '.'
}
Save-PixelArt -Rows $overlay -Palette @{ o = '#9a9a9a'; m = '#c2c2c2'; h = '#e2e2e2'; b = '#d8d8d8'; d = '#bcbcbc'; l = '#f0f0f0' } `
    -Path (Join-Path $rp 'entity/frames/dyed_item_frame.png')

Write-Host '[textures] frames: done'
