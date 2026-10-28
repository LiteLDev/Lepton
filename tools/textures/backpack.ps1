# U14 backpack textures: original pixel art for the backpack, ravager hide, bonded ravager hide,
# seed pouch, the worn backpack (attachable) and the Totem of Holding entity.
#
# Dot-sourced by tools/pack/backpack.ps1, which records the files; it can also run alone:
#   pwsh -NoProfile -File tools/textures/backpack.ps1
. "$PSScriptRoot/lib.ps1"

function Write-BackpackTextures {
    param([Parameter(Mandatory)][string]$RP, [scriptblock]$Record = { param($p) $p })
    $items = Join-Path $RP 'textures/items'
    $blocks = Join-Path $RP 'textures/blocks'
    $entity = Join-Path $RP 'textures/entity'
    $armor = Join-Path $RP 'textures/models/armor'

    # --- backpack item: a brown leather bag with a carry handle, a flap and a metal buckle
    $bag = @{ o = '#3a2414'; d = '#6e4425'; m = '#8f5a32'; l = '#b57a46'; h = '#d39a5e'; b = '#d8d8d8'; k = '#7d7d7d' }
    Save-PixelArt -Palette $bag -Path (& $Record (Join-Path $items 'backpack.png')) -Rows @(
        '................',
        '.....oooooo.....',
        '....oo....oo....',
        '....o......o....',
        '..oooooooooooo..',
        '.ollllllllllllo.',
        '.olhhhhhhhhhhlo.',
        '.ollllllllllllo.',
        '.ommmmmkkmmmmmo.',
        '.odddddbbdddddo.',
        '.ommmmmmmmmmmmo.',
        '.omllllllllllmo.',
        '.omllllllllllmo.',
        '.ommmmmmmmmmmmo.',
        '.oddddddddddddo.',
        '..oooooooooooo..')

    # --- ravager hide: a draped grey-brown pelt with lighter fur tufts and a ragged fringe
    $hide = @{ o = '#2b2421'; d = '#4a3d35'; m = '#63544a'; l = '#7d6c5f'; f = '#9c8b7a' }
    Save-PixelArt -Palette $hide -Path (& $Record (Join-Path $items 'ravager_hide.png')) -Rows @(
        '................',
        '..oooo....oooo..',
        '.oddddo..oddddo.',
        '.odmmddoodddmdo.',
        '..odmmlmmmlmmo..',
        '..omlmmfmmmlmo..',
        '..omllmmmflmmo..',
        '..odmmfmmmmmdo..',
        '..omlmmmlmmfmo..',
        '..ommfmmmmlmmo..',
        '..odmmmlmmmmdo..',
        '..omlmmmmfmlmo..',
        '.odmmmlmmmmmmdo.',
        '.oddo.odddo.oddo',
        '.oo...ooo...ooo.',
        '................')

    # --- seed pouch: a tied cloth pouch with seeds peeking out
    $pouch = @{ o = '#3d2a17'; d = '#6b4a2a'; m = '#8c6439'; l = '#b08551'; h = '#cfa36b'; s = '#e8e2d0'; g = '#6e9a3a'; y = '#c9b458' }
    Save-PixelArt -Palette $pouch -Path (& $Record (Join-Path $items 'seed_pouch.png')) -Rows @(
        '................',
        '......gyyg......',
        '.....oygygo.....',
        '......osso......',
        '.....osssso.....',
        '......oddo......',
        '.....odmmdo.....',
        '....odmllmdo....',
        '...odmlhhlmdo...',
        '..odmlhhhhlmdo..',
        '..odmlhhhhlmdo..',
        '..odmmllllmmdo..',
        '..oddmmmmmmddo..',
        '...oddddddddo...',
        '....oooooooo....',
        '................')

    # --- bonded ravager hide: four stitched pelt panels (seeded noise + seams)
    $noise = New-NoiseGrid -Seed 1401 -Size 16 -Cell 4 -Octaves 3
    $grid = ConvertTo-IndexGrid $noise 4
    $chars = 'dmlf'
    $rows = for ($y = 0; $y -lt 16; $y++) {
        -join (0..15 | ForEach-Object {
                $x = $_
                $seamX = ($x -eq 0 -or $x -eq 8); $seamY = ($y -eq 0 -or $y -eq 8)
                if ($seamX -or $seamY) { if ((($x + $y) % 2) -eq 0) { 's' } else { 'o' } }
                else { $chars[$grid[$x, $y]] }
            })
    }
    Save-PixelArt -Palette @{ o = '#1e1916'; s = '#a8957f'; d = '#3d332c'; m = '#4f4239'; l = '#5f5045'; f = '#73614f' } `
        -Path (& $Record (Join-Path $blocks 'bonded_ravager_hide.png')) -Rows $rows

    # --- worn backpack (attachable, 32x32 box UV; greyscale: tinted by the leather material)
    # main box (u0 v0, 8x10x3): top 3..11/0..3, bottom 11..19/0..3, sides 0..3 & 11..14, front 3..11, back 14..22 (rows 3..13)
    # pocket box (u0 v14, 6x4x1): top 1..7/14..15, front 1..7/15..19, back 8..14/15..19
    $w = [char[][]]::new(32)
    for ($y = 0; $y -lt 32; $y++) { $w[$y] = ('.' * 32).ToCharArray() }
    function Fill([int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$c) { for ($yy = $y0; $yy -lt $y1; $yy++) { for ($xx = $x0; $xx -lt $x1; $xx++) { $w[$yy][$xx] = $c } } }
    $n2 = New-NoiseGrid -Seed 1402 -Size 32 -Cell 4 -Octaves 2
    Fill 3 0 19 3 'l'; Fill 0 3 22 13 'm'
    for ($y = 3; $y -lt 13; $y++) { for ($x = 0; $x -lt 22; $x++) { if ($n2[$x, $y] -gt 0.62) { $w[$y][$x] = 'l' } elseif ($n2[$x, $y] -lt 0.32) { $w[$y][$x] = 'd' } } }
    foreach ($fx in 3, 14) {
        Fill $fx 3 ($fx + 8) 7 'h'          # flap
        Fill $fx 7 ($fx + 8) 8 'o'          # flap edge
        $w[7][$fx + 3] = 'b'; $w[7][$fx + 4] = 'b'; $w[8][$fx + 3] = 'k'; $w[8][$fx + 4] = 'k'   # buckle
        Fill $fx 12 ($fx + 8) 13 'd'        # bottom seam
    }
    Fill 1 14 7 15 'l'; Fill 0 15 14 19 'm'; Fill 1 15 7 16 'h'; Fill 8 15 14 16 'h'
    $w[17][3] = 'k'; $w[17][4] = 'k'; $w[17][10] = 'k'; $w[17][11] = 'k'
    Save-PixelArt -Palette @{ o = '#4a4a4a'; d = '#8a8a8a'; m = '#b4b4b4'; l = '#cdcdcd'; h = '#e2e2e2'; b = '#f4f4f4'; k = '#5c5c5c' } `
        -Path (& $Record (Join-Path $armor 'backpack.png')) -Rows @($w | ForEach-Object { -join $_ })

    # --- Totem of Holding (32x32 box UV): body 6x8x2 at (0,0), head 4x4x2 at (0,10), wings 3x4x1 at (16,0) / (16,6)
    $t = [char[][]]::new(32)
    for ($y = 0; $y -lt 32; $y++) { $t[$y] = ('.' * 32).ToCharArray() }
    function TFill([int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$c) { for ($yy = $y0; $yy -lt $y1; $yy++) { for ($xx = $x0; $xx -lt $x1; $xx++) { $t[$yy][$xx] = $c } } }
    $n3 = New-NoiseGrid -Seed 1403 -Size 32 -Cell 4 -Octaves 2
    TFill 0 0 16 10 'p'; TFill 0 10 12 16 'g'; TFill 16 0 24 5 'c'; TFill 16 6 24 11 'c'
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 24; $x++) {
            if ($t[$y][$x] -eq 'p' -and $n3[$x, $y] -gt 0.6) { $t[$y][$x] = 'q' }
            if ($t[$y][$x] -eq 'g' -and $n3[$x, $y] -gt 0.6) { $t[$y][$x] = 'h' }
            if ($t[$y][$x] -eq 'c' -and $n3[$x, $y] -lt 0.35) { $t[$y][$x] = 'e' } } }
    # body front (2,2) 6x8 and back (10,2): gold bands and an emerald heart
    foreach ($bx in 2, 10) {
        TFill $bx 3 ($bx + 6) 4 'g'; TFill $bx 8 ($bx + 6) 9 'g'
        TFill ($bx + 2) 5 ($bx + 4) 7 'e'
    }
    # head front (2,12) 4x4: eyes and mouth
    $t[13][2] = 'k'; $t[13][5] = 'k'; $t[15][3] = 'k'; $t[15][4] = 'k'
    Save-PixelArt -Palette @{ p = '#5b3a8c'; q = '#7a52b3'; g = '#d9a93a'; h = '#f2d27a'; c = '#3aa58c'; e = '#2fd67d'; k = '#1a1024' } `
        -Path (& $Record (Join-Path $entity 'totem_of_holding.png')) -Rows @($t | ForEach-Object { -join $_ })
}

if ($MyInvocation.InvocationName -ne '.') {
    Write-BackpackTextures -RP (Resolve-Path "$PSScriptRoot/../../assets/pack/rp").Path
}
