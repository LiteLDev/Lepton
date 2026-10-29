# U26 mobtweaks textures: the dragon scale item.
# Original pixel art (drawn here, not traced from any game or mod texture).
#
#   pwsh tools/textures/mobtweaks.ps1      # writes assets/pack/rp/textures/items/lepton_mobtweaks/

param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$items = Join-Path $Root 'assets/pack/rp/textures/items/lepton_mobtweaks'

# Dragon Scale: a single curved, shield-shaped scale of the ender dragon: near-black body with a
# deep violet sheen, a lighter ridge along the middle and a pale glint on the upper edge.
$rows = @(
    '................',
    '......oooo......',
    '....ooghhhoo....',
    '...oghhmmhhho...',
    '..oghmmrrmmhmo..',
    '..ohmmdrrdmmmo..',
    '.ohmmddrrddmmdo.',
    '.ommdddrrdddmdo.',
    '.ommddkrrkddddo.',
    '.omdddkrrkdddko.',
    '..odddkrrkddko..',
    '..odkkkrrkkkko..',
    '...odkkrrkkko...',
    '....okkrrkko....',
    '.....ookkoo.....',
    '.......oo.......'
)
$pal = @{
    o = '#0c0810'  # outline
    k = '#1a1222'  # darkest body
    d = '#2a1d38'  # body
    m = '#3d2a55'  # violet sheen
    h = '#5b4180'  # light edge
    g = '#b9a6dd'  # glint
    r = '#4a3466'  # central ridge
}
Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $items 'dragon_scale.png')
Write-Output (Join-Path $items 'dragon_scale.png')
