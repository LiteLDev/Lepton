# Textures of U13 enchanting (original pixel art): the Ancient Tome and the Rune smithing template.
#   pwsh tools/textures/enchanting.ps1
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$items = Join-Path $root 'assets/pack/rp/textures/items'

# Ancient Tome: a thick, worn teal-leather book with brass corners, a clasp and a glowing glyph.
$tome = @(
    '................',
    '...kkkkkkkkkk...',
    '..kgmmmmmmmmgk..',
    '..kmhhhhhhhmmkp.',
    '..kmhddddddhmkp.',
    '..kmhdqqqqdhmkp.',
    '..kmhdqwwqdhmkpp',
    'bbkmhdqwwqdhmkbp',
    'bekmhdqqqqdhmkep',
    '..kmhddddddhmkp.',
    '..kmhhhhhhhmmkp.',
    '..kmhhhhhhhmmkp.',
    '..kgmmmmmmmmgkp.',
    '...kkkkkkkkkkpp.',
    '....pppppppppp..',
    '................'
)
$tomePal = @{
    k = '#1d2a2b'   # outline
    m = '#2f6b66'   # leather
    h = '#3f8a82'   # leather highlight
    d = '#24504c'   # recessed panel
    g = '#c8a24a'   # brass corners
    q = '#7fe3d2'   # glyph glow
    w = '#e6fff8'   # glyph core
    b = '#b28a36'   # clasp
    e = '#e2c56e'   # clasp highlight
    p = '#e9dfc2'   # page edges
}
Save-PixelArt -Rows $tome -Palette $tomePal -Path (Join-Path $items 'ancient_tome.png')

# Rune smithing template: a dark stone tablet (smithing-template silhouette) with a carved rune.
$rune = @(
    '................',
    '....oooooooo....',
    '...osssssssso...',
    '..osttttttttso..',
    '..ostdddddddto..',
    '..ostdrrrrrdto..',
    '..ostdrddrddto..',
    '..ostdrrrrrdto..',
    '..ostdddrddtso..',
    '..ostdrrrrrdto..',
    '..ostdddddddto..',
    '..osttttttttso..',
    '...osssssssso...',
    '....oooooooo....',
    '................',
    '................'
)
$runePal = @{
    o = '#1b1a22'
    s = '#3e3b4c'
    t = '#5a566d'
    d = '#2c2a36'
    r = '#c58cff'
}
Save-PixelArt -Rows $rune -Palette $runePal -Path (Join-Path $items 'smithing_template_rune.png')
Write-Host 'enchanting textures written'
