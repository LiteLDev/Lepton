# U23 gadgets textures: abacus, pathfinder's quill, music discs, tiny potato looks.
# Original pixel art (drawn here, not traced from any game or mod texture).
#
#   pwsh tools/textures/gadgets.ps1          # writes assets/pack/rp/textures/{items,blocks}/...
#
# Prints the written paths (one per line) so tools/pack/gadgets.ps1 can record them.

param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$items = Join-Path $Root 'assets/pack/rp/textures/items/lepton_gadgets'
$blocks = Join-Path $Root 'assets/pack/rp/textures/blocks/lepton_gadgets'
$written = [System.Collections.Generic.List[string]]::new()

function Save-Canvas([hashtable]$Px, [string]$Path, [int]$W = 16, [int]$H = 16) {
    $bmp = [System.Drawing.Bitmap]::new($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $H; $y++) {
            for ($x = 0; $x -lt $W; $x++) {
                $c = $Px["$x,$y"]
                $bmp.SetPixel($x, $y, $(if ($c) { ConvertTo-Color $c } else { [System.Drawing.Color]::Transparent }))
            }
        }
        New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
    $written.Add($Path)
}

function Save-Rows([string[]]$Rows, [hashtable]$Palette, [string]$Path) {
    Save-PixelArt -Rows $Rows -Palette $Palette -Path $Path
    $written.Add($Path)
}

# ---------------------------------------------------------------------------------------------
# Items
# ---------------------------------------------------------------------------------------------

# Abacus: a dark wooden frame, four iron rods with coloured beads.
Save-Rows @(
    '................',
    '.ffffffffffffff.',
    '.fmmmmmmmmmmmmf.',
    '.fm.ae.......mf.',
    '.fmaaeerrrrrrmf.',
    '.fm.ae.......mf.',
    '.fm....bc....mf.',
    '.fmrrrbbccrrrmf.',
    '.fm....bc....mf.',
    '.fm.gh.......mf.',
    '.fmgghhrrrrrrmf.',
    '.fm.gh....ae.mf.',
    '.fmmmmmmmmmmmmf.',
    '.ffffffffffffff.',
    '..ff........ff..',
    '................'
) @{ f = '#4a2f17'; m = '#7a4e26'; r = '#9da3a8'; a = '#c0392b'; e = '#e8655a'; b = '#2e6fc2'; c = '#6ea4ea'; g = '#3c9b3c'; h = '#7fd27f' } `
    (Join-Path $items 'abacus.png')

# Pathfinder's Quill: a white-and-grey feather with a dark nib dipped in ink.
Save-Rows @(
    '...........wwww.',
    '.........wwwvvw.',
    '........wwvvvw..',
    '.......wwvvvgw..',
    '......wwvvvgw...',
    '.....wwvvvgw....',
    '....wwvvvgw.....',
    '....wvvvgw......',
    '...wvvvgw.......',
    '...wvvgw........',
    '...wvgw.........',
    '....sgw.........',
    '...ss...........',
    '..sk............',
    '.kk.............',
    'k...............'
) @{ w = '#f2f2ee'; v = '#c9c9c2'; g = '#8e8e88'; s = '#c8a25a'; k = '#1d1d2a' } `
    (Join-Path $items 'pathfinders_quill.png')

# Music discs: a black disc with a coloured label, like vanilla records but drawn here.
$discRows = @(
    '................',
    '.....dddddd.....',
    '...ddqqqqqqdd...',
    '..dqqddddddqqd..',
    '.dqdddhddddddqd.',
    '.dqddhppppdddqd.',
    'dqdddplllppddqqd',
    'dqddpllooolpddqd',
    'dqddplloolllddqd',
    'dqdddpplllpdddqd',
    '.dqdddppppdddqd.',
    '.dqdddddddddhqd.',
    '..dqqddddddqqd..',
    '...ddqqqqqqdd...',
    '.....dddddd.....',
    '................'
)
$labels = [ordered]@{
    drips     = @('#4f9de0', '#8cc6f5')
    ocean     = @('#1e5f9a', '#3fa7c9')
    rain      = @('#5d6f86', '#9eb2c9')
    wind      = @('#c7d6d6', '#eef6f6')
    fire      = @('#d0541c', '#f4a62a')
    clock     = @('#b88b2c', '#f0d36a')
    crickets  = @('#3d7f2e', '#8fcf58')
    chatter   = @('#a04a8f', '#e08ad0')
    endermosh = @('#3b1f5c', '#b562d6')
}
foreach ($kv in $labels.GetEnumerator()) {
    Save-Rows $discRows @{ d = '#151515'; q = '#2d2d33'; h = '#5a5a66'; p = $kv.Value[0]; l = $kv.Value[1]; o = '#0b0b0b' } `
        (Join-Path $items "music_disc_$($kv.Key).png")
}

# ---------------------------------------------------------------------------------------------
# Tiny potato looks. UV layout (16x16): top 4..7 x 0..3, bottom 8..11 x 0..3; sides at y 4..9:
# side 0..3, face 4..7, side 8..11, back 12..15.
# ---------------------------------------------------------------------------------------------

$skin = @{ hi = '#e3c483'; mid = '#cfa462'; lo = '#a97d40'; spot = '#8c6532' }

function New-Tater([hashtable]$Body = $skin, [string[]]$Stripes, [string]$Eye = '#1b1410', [string]$Mouth = '#7a3c2a', [scriptblock]$Extra) {
    $px = @{}
    $col = { param($x, $y) if ($y -le 4) { $Body.hi } elseif ($y -ge 9) { $Body.lo } else { $Body.mid } }
    for ($x = 4; $x -lt 8; $x++) { for ($y = 0; $y -lt 4; $y++) { $px["$x,$y"] = $Body.hi } }
    for ($x = 8; $x -lt 12; $x++) { for ($y = 0; $y -lt 4; $y++) { $px["$x,$y"] = $Body.lo } }
    for ($x = 0; $x -lt 16; $x++) { for ($y = 4; $y -lt 10; $y++) { $px["$x,$y"] = & $col $x $y } }
    foreach ($s in @('1,6', '10,5', '13,8', '5,1', '9,2', '2,8', '14,5')) { $px[$s] = $Body.spot }
    if ($Stripes) {
        for ($y = 4; $y -lt 10; $y++) {
            $c = $Stripes[[Math]::Min($y - 4, $Stripes.Count - 1)]
            for ($x = 0; $x -lt 16; $x++) { $px["$x,$y"] = $c }
        }
        for ($x = 4; $x -lt 8; $x++) { for ($y = 0; $y -lt 4; $y++) { $px["$x,$y"] = $Stripes[0] } }
        for ($x = 8; $x -lt 12; $x++) { for ($y = 0; $y -lt 4; $y++) { $px["$x,$y"] = $Stripes[-1] } }
    }
    # the face (front, x 4..7): two wide-set eyes, a small smile
    $px['4,6'] = $Eye; $px['7,6'] = $Eye
    $px['5,8'] = $Mouth; $px['6,8'] = $Mouth
    if ($Extra) { & $Extra $px }
    return $px
}

$taters = [ordered]@{}
$taters.default = New-Tater
$taters.angry = New-Tater -Body @{ hi = '#e8b07a'; mid = '#d48a52'; lo = '#a4552c'; spot = '#8a3f1c' } -Eye '#2a0000' -Mouth '#4a0f0f' -Extra {
    param($p) $p['4,5'] = '#2a0000'; $p['5,5'] = '#2a0000'; $p['6,5'] = '#2a0000'; $p['7,5'] = '#2a0000'; $p['5,8'] = '#4a0f0f'; $p['6,7'] = '#4a0f0f'; $p['5,7'] = '#4a0f0f'; $p['6,8'] = '#4a0f0f' }
$taters.halloween = New-Tater -Body @{ hi = '#f2a33a'; mid = '#e0821e'; lo = '#b05e12'; spot = '#8a460c' } -Eye '#ffd84a' -Mouth '#ffd84a' -Extra {
    param($p) foreach ($k in '4,5', '7,5', '5,6', '6,6') { $p[$k] = '#3a1a05' }; $p['5,8'] = '#ffd84a'; $p['6,8'] = '#3a1a05'
    $p['5,0'] = '#4d7a22'; $p['6,0'] = '#4d7a22'; $p['6,1'] = '#3a5e18' }
$taters.ace = New-Tater -Stripes @('#202020', '#202020', '#a3a3a3', '#ffffff', '#800080', '#800080')
$taters.agender = New-Tater -Stripes @('#000000', '#bcc4c7', '#ffffff', '#b7f684', '#ffffff', '#bcc4c7')
$taters.aro = New-Tater -Stripes @('#3da542', '#a7d379', '#ffffff', '#ffffff', '#a9a9a9', '#000000')
$taters.aureylian = New-Tater -Body @{ hi = '#f5c6c6'; mid = '#eaa4ac'; lo = '#c97b88'; spot = '#b0606e' } -Extra {
    param($p) for ($x = 4; $x -lt 8; $x++) { for ($y = 0; $y -lt 4; $y++) { $p["$x,$y"] = '#b8323a' } }
    foreach ($x in 0..15) { $p["$x,4"] = '#b8323a' }; $p['4,5'] = '#b8323a'; $p['7,5'] = '#b8323a' }
$taters.bi = New-Tater -Stripes @('#d60270', '#d60270', '#9b4f96', '#9b4f96', '#0038a8', '#0038a8')
$taters.bob = New-Tater -Body @{ hi = '#9fd0f0'; mid = '#6fb0dc'; lo = '#3f7fae'; spot = '#2f6690' } -Eye '#10202c' -Mouth '#10202c'
$taters.bosnia = New-Tater -Stripes @('#002395', '#002395', '#002395', '#002395', '#002395', '#002395') -Extra {
    param($p) foreach ($k in '0,4', '1,5', '2,6', '3,7', '1,4', '2,5', '3,6', '8,7', '9,8', '12,4', '13,5', '14,6', '15,7') { $p[$k] = '#fecb00' }
    foreach ($k in '0,7', '2,9', '10,5', '12,9') { $p[$k] = '#ffffff' } }
$taters.enby = New-Tater -Stripes @('#fcf434', '#fcf434', '#ffffff', '#9c59d1', '#2c2c2c', '#2c2c2c')
$taters.pride = New-Tater -Stripes @('#e40303', '#ff8c00', '#ffed00', '#008026', '#004dff', '#750787')
$taters.genderfluid = New-Tater -Stripes @('#ff76a4', '#ffffff', '#c011d7', '#000000', '#2f3cbe', '#2f3cbe')
$taters.lesbian = New-Tater -Stripes @('#d52d00', '#ff9a56', '#ffffff', '#ffffff', '#d362a4', '#a30262')
$taters.gray = New-Tater -Body @{ hi = '#b9b9b9'; mid = '#999999'; lo = '#6e6e6e'; spot = '#5a5a5a' }
$taters.pan = New-Tater -Stripes @('#ff218c', '#ff218c', '#ffd800', '#ffd800', '#21b1ff', '#21b1ff')
$taters.robo = New-Tater -Body @{ hi = '#c9d0d6'; mid = '#9aa5ae'; lo = '#6b7680'; spot = '#56606a' } -Eye '#ff3030' -Mouth '#30ff60' -Extra {
    param($p) foreach ($x in 0..15) { $p["$x,7"] = '#56606a' }; $p['4,6'] = '#ff3030'; $p['7,6'] = '#ff3030'; $p['5,0'] = '#56606a'; $p['5,1'] = '#ff3030' }
$taters.trans = New-Tater -Stripes @('#5bcefa', '#f5a9b8', '#ffffff', '#ffffff', '#f5a9b8', '#5bcefa')

foreach ($kv in $taters.GetEnumerator()) { Save-Canvas $kv.Value (Join-Path $blocks "tiny_potato_$($kv.Key).png") }

# Tomato (own layout: 5x5 faces: top 0..4 x 0..4, bottom 0..4 x 5..9, front 5..9 x 0..4,
# back 5..9 x 5..9, sides 10..14 x 0..4 / 10..14 x 5..9).
$tom = @{}
for ($x = 0; $x -lt 15; $x++) { for ($y = 0; $y -lt 10; $y++) { $tom["$x,$y"] = $(if (($x + $y) % 5 -eq 0) { '#c81e1e' } else { '#e2332a' }) } }
foreach ($k in '2,2', '1,2', '3,2', '2,1', '2,3', '0,0', '4,4', '0,4', '4,0') { $tom[$k] = '#3f8a2a' }
foreach ($k in '6,1', '8,1') { $tom[$k] = '#1b1410' }; $tom['7,3'] = '#7a1010'
foreach ($k in '7,0', '12,0', '7,5', '12,5') { $tom[$k] = '#3f8a2a' }
Save-Canvas $tom (Join-Path $blocks 'tiny_potato_tomato.png')

# Sus glasses (16x5 strip in a 16x16 texture): a cyan visor with a white glint.
$sus = @{}
for ($x = 0; $x -lt 16; $x++) { for ($y = 0; $y -lt 5; $y++) { $sus["$x,$y"] = '#7fc6d8' } }
for ($x = 0; $x -lt 16; $x++) { $sus["$x,0"] = '#4a8ea0'; $sus["$x,4"] = '#4a8ea0' }
foreach ($k in '3,1', '4,1', '5,1', '4,2') { $sus[$k] = '#e8fbff' }
Save-Canvas $sus (Join-Path $blocks 'tiny_potato_sus.png')

# Abacus highlight line (particle): one white pixel, tinted with highlightColor by the particle.
Save-Canvas @{ '0,0' = '#ffffff' } (Join-Path $Root 'assets/pack/rp/textures/particle/lepton_gadgets_line.png') 1 1

$written | ForEach-Object { $_ }
