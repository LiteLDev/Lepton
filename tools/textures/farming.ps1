# U25 farming textures (original pixel art).
#   pwsh tools/textures/farming.ps1
# water_pink_petals: four petal clusters, one per 8x8 quadrant (the block model shows 1..4 of them).
. "$PSScriptRoot/lib.ps1"

$root = Resolve-Path "$PSScriptRoot/../.."

$rows = @(
    '................',
    '..lp.....pl.....',
    '.ldpp...pdpl..g.',
    '.ppydp.lpydp.gg.',
    '..pdyp.ppydp..g.',
    '..lpp...ldp.....',
    '...gg....gg.....',
    '....g.....g.....',
    '................',
    '.....g.....lp...',
    '....gg....ldpp..',
    '..lpp.....ppydl.',
    '.ldydp...ppydp..',
    '.ppydl...lpdpg..',
    '..pdp.....pl.gg.',
    '...l............'
)
$pal = @{
    l = '#fbd3ea'   # petal highlight
    p = '#f19ac8'   # petal
    d = '#d76aa5'   # petal shade
    y = '#f6d65a'   # flower centre
    g = '#5f8a3a'   # stem
}
Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $root 'assets/pack/rp/textures/blocks/water_pink_petals.png')
