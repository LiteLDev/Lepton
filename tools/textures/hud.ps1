# U16 hud: original pixel art for the HUD and tooltip components (elytra indicator, attribute,
# slot, food and fuel icons, upgrade arrows, the shulker preview frame and the map background).
# Output: assets/pack/rp/textures/lepton/hud/. Run: pwsh tools/textures/hud.ps1
. "$PSScriptRoot/lib.ps1"
$root = Join-Path $PSScriptRoot '../../assets/pack/rp/textures/lepton/hud'

function Icon([string]$name, [string[]]$rows, [hashtable]$pal) {
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $root "$name.png")
}

# ---- elytra (ElytraIndicator) ------------------------------------------------------------------
Icon 'elytra' @(
    '.d.....d.',
    'dLd...dLd',
    'dLMd.dMLd',
    'dLMMdMMLd',
    'dLMMdMMLd',
    'dLMd.dMLd',
    'dMd...dMd',
    'dd.....dd',
    'd.......d'
) @{ d = '#2d2440'; L = '#b9a8d6'; M = '#7c6aa3' }

# ---- attribute icons ---------------------------------------------------------------------------------
$steel = @{ o = '#1c1c1c'; w = '#f2f2f2'; s = '#b4b4b4'; h = '#6b4a2a'; g = '#d9b23a' }
Icon 'attribute/attack_damage' @(
    '.......oo',
    '......owo',
    '.....ows.',
    '....ows..',
    '.o.ows...',
    '.ooso....',
    '..oho....',
    '.oh.oo...',
    'oo.......'
) $steel
Icon 'attribute/attack_speed' @(
    '.......oo',
    '......owo',
    'g....ows.',
    '.g..ows..',
    'gg.ows...',
    '..oso....',
    '..oho....',
    '.oh.oo...',
    'oo.......'
) $steel
Icon 'attribute/armor' @(
    '.oo...oo.',
    'osso.osso',
    'oswsosso.',
    '.oswsso..',
    '.osssso..',
    '.osssso..',
    '.osssso..',
    '.oooooo..',
    '.........'
) $steel
Icon 'attribute/armor_toughness' @(
    '.oo...oo.',
    'occo.occo',
    'ocwcoccco',
    '.ocwcco..',
    '.occcco..',
    '.occcco..',
    '.occcco..',
    '.oooooo..',
    '.........'
) @{ o = '#0f2b2b'; c = '#4fd6c8'; w = '#d9fffb' }
Icon 'attribute/knockback_resistance' @(
    '.........',
    '.ooooooo.',
    '.ossssso.',
    '..ooooo..',
    '...oso...',
    '...oso...',
    '..oosoo..',
    '.ossssso.',
    '.ooooooo.'
) @{ o = '#1c1c1c'; s = '#5c5c66' }
Icon 'attribute/max_health' @(
    '.........',
    '.oo...oo.',
    'orro.orro',
    'orwrorrro',
    'orrrrrrro',
    '.orrrrro.',
    '..orrro..',
    '...oro...',
    '....o....'
) @{ o = '#2a0606'; r = '#d8262b'; w = '#ffc4c4' }
Icon 'attribute/speed' @(
    '.........',
    '....ooo..',
    'bb..obbo.',
    '....obbo.',
    '.bb.obbo.',
    '....obbbo',
    'bb.obbbbo',
    '...oooooo',
    '.........'
) @{ o = '#14202e'; b = '#5b9bd5' }
Icon 'attribute/luck' @(
    '..oo.oo..',
    '.oggoggo.',
    '.ogggggo.',
    '..ogggo..',
    '.ogggggo.',
    '.oggoggo.',
    '..oo.oo..',
    '....o....',
    '...o.....'
) @{ o = '#0e2b0e'; g = '#3fb33f' }
Icon 'attribute/attack_knockback' @(
    '.........',
    '...oo....',
    '..oSSo.a.',
    '.oSSSSo.a',
    '.oSSSSoaa',
    '.oSSSSo.a',
    '..oSSo.a.',
    '...oo....',
    '.........'
) @{ o = '#2b1a10'; S = '#e0a878'; a = '#f0f0f0' }
Icon 'attribute/reach_distance' @(
    '.........',
    '..oo.....',
    '.oSSo....',
    '.oSSSoaaa',
    '.oSSSo..a',
    '.oSSo...a',
    '..oo...aa',
    '.......a.',
    '.........'
) @{ o = '#2b1a10'; S = '#e0a878'; a = '#f0f0f0' }
Icon 'attribute/attack_range' @(
    '.......oo',
    '......owo',
    '.....ows.',
    '....ows..',
    'a..ows...',
    'aaoso....',
    'a.oho....',
    '.oh.oo...',
    'oo.......'
) ($steel + @{ a = '#7fd0ff' })
Icon 'attribute/gravity' @(
    '...ooo...',
    '..owwwo..',
    '..owwwo..',
    '...ooo...',
    '....a....',
    '....a....',
    '..a.a.a..',
    '...aaa...',
    '....a....'
) @{ o = '#3a2a50'; w = '#9d7fd0'; a = '#f0f0f0' }
Icon 'attribute/step_height' @(
    '.........',
    '......ooo',
    '......ogo',
    '...oooooo',
    '...ogggdo',
    'ooooooooo',
    'oggggggdo',
    'oddddddddo'.Substring(0, 9),
    'ooooooooo'
) @{ o = '#1e1e1e'; g = '#8f8f8f'; d = '#5e5e5e' }
Icon 'attribute/swim_speed' @(
    '.........',
    '....bb...',
    '...bbbb.b',
    '..bwbbbbb',
    '..bbbbbbb',
    '...bbbb.b',
    '....bb...',
    'c.c.c.c.c',
    '.c.c.c.c.'
) @{ b = '#3a7fd5'; w = '#ffffff'; c = '#9fd3ff' }
Icon 'attribute/unknown' @(
    '..oooo...',
    '.oyyyyo..',
    '.oyoooyo.',
    '..o..oyo.',
    '....oyo..',
    '...oyo...',
    '...ooo...',
    '...oyo...',
    '...ooo...'
) @{ o = '#2a2a00'; y = '#f5e050' }

# ---- upgrade / downgrade arrows (13x13) --------------------------------------------------------------
$up = @(
    '......o......',
    '.....oGo.....',
    '....oGGGo....',
    '...oGGyGGo...',
    '..oGGGyGGGo..',
    '.ooooGyGoooo.',
    '....oGyGo....',
    '....oGyGo....',
    '....oGyGo....',
    '....oGyGo....',
    '....oGGGo....',
    '....ooooo....',
    '.............'
)
Icon 'upgrade' $up @{ o = '#0b3d0b'; G = '#3fd13f'; y = '#a6ffa6' }
$down = @($up[11..0] + $up[12])
Icon 'downgrade' $down @{ o = '#3d0b0b'; G = '#e03a3a'; y = '#ffb0b0' }

# ---- equipment group icons (attribute tooltips with several groups) --------------------------------
$gp = @{ o = '#202020'; s = '#c8c8c8'; d = '#8a8a8a'; b = '#6b4a2a' }
Icon 'slot/mainhand' @('.........', '......oo.', '.....oso.', '....oso..', '.o.oso...', '.ooso....', '..obo....', '.ob.o....', 'oo.......') $gp
Icon 'slot/offhand' @('.ooooooo.', 'ossbbbsso', 'ossbsbsso', 'obbbsbbbo', 'ossbsbsso', '.osbbbso.', '..ossso..', '...ooo...', '.........') $gp
Icon 'slot/head' @('.........', '..ooooo..', '.osssssO.'.Replace('O','o'), 'osdddddso', 'osd...dso', 'oso...oso', 'oo.....oo', '.........', '.........') $gp
Icon 'slot/chest' @('.oo...oo.', 'osso.osso', 'osssossso', '.osssso..', '.osssso..', '.osssso..', '.oooooo..', '.........', '.........') $gp
Icon 'slot/legs' @('.ooooooo.', '.osssssO.'.Replace('O','o'), '.ossosso.', '.oso.oso.', '.oso.oso.', '.oso.oso.', '.ooo.ooo.', '.........', '.........') $gp
Icon 'slot/feet' @('.........', '.........', '.oo...oo.', '.os...os.', '.os...os.', 'oss..oss.', 'ooo..ooo.', '.........', '.........') $gp
Icon 'slot/any' @('....o....', '...oso...', 'oooosoooo', '.ossssso.', '..ossso..', '.osooos..', '.oo...oo.', '.........', '.........') $gp
Icon 'slot/armor' @('.oo...oo.', 'oddo.oddo', 'oddddddo.', '.oddddo..', '.oddddo..', '.oddddo..', '.oooooo..', '.........', '.........') $gp
Icon 'slot/body' @('.........', 'oooooo...', 'osssssooo', 'ossssssso', 'oossssso.', '.oso.oso.', '.oo..oo..', '.........', '.........') $gp

# ---- food shanks (normal + "hunger" variants for harmful food) ------------------------------------
$shankEmpty = @(
    '...oo....',
    '..o..oo..',
    '.o.....o.',
    '.o......o',
    '.o......o',
    '..o....o.',
    '.o.oooo..',
    'o.o......',
    '.o.......'
)
$shankFull = @(
    '...oo....',
    '..oMMoo..',
    '.oMMMMMo.',
    '.oMLMMMMo',
    '.oMMMMMMo',
    '..oMMMMo.',
    '.obooooo.',
    'obo......',
    '.o.......'
)
$shankHalf = @(
    '...oo....',
    '..oMMo...',
    '.oMMMM...',
    '.oMLMM...',
    '.oMMMM...',
    '..oMMM...',
    '.oboo....',
    'obo......',
    '.o.......'
)
Icon 'food_empty' $shankEmpty @{ o = '#3b2a1e' }
Icon 'food_full' $shankFull @{ o = '#3b1d10'; M = '#b5533a'; L = '#e8956b'; b = '#efe6cf' }
Icon 'food_half' $shankHalf @{ o = '#3b1d10'; M = '#b5533a'; L = '#e8956b'; b = '#efe6cf' }
Icon 'food_empty_hunger' $shankEmpty @{ o = '#1f2b14' }
Icon 'food_full_hunger' $shankFull @{ o = '#1f2b14'; M = '#6f8f3a'; L = '#a7c66b'; b = '#dfe8cf' }
Icon 'food_half_hunger' $shankHalf @{ o = '#1f2b14'; M = '#6f8f3a'; L = '#a7c66b'; b = '#dfe8cf' }

# ---- fuel flame (13x13) -------------------------------------------------------------------------------
Icon 'fuel' @(
    '......r......',
    '.....rr......',
    '.....rrr.....',
    '....rroor....',
    '...rroooor...',
    '...rooyoor.r.',
    '..rrooyyoorr.',
    '..rooyyyyoor.',
    '..rooyywyoor.',
    '..rrooyyyorr.',
    '...rrooooor..',
    '....rrrrrr...',
    '.............'
) @{ r = '#c2300f'; o = '#f07a14'; y = '#ffd23a'; w = '#fff6c8' }

# ---- shulker preview frame (30x30 nine-slice: 5 px corners, 18 px cells, 1 px gaps) --------------
# Light grey so the dye tint (multiplied) shows; cells look like inventory slots.
$frame = [System.Collections.Generic.List[string]]::new()
$startProf = 'OWLLL'; $endProf = 'LLDDO'
function Band([int]$v) { if ($v -lt 5) { return 's' } elseif ($v -ge 6 -and $v -le 23) { return 'm' } elseif ($v -ge 25) { return 'e' } else { return 'g' } }
function Prof([int]$v) { if ($v -lt 5) { return $startProf[$v] } else { return $endProf[$v - 25] } }
function CellPx([int]$x, [int]$y) {
    if ($x -eq 0 -or $y -eq 0) { return 'k' }
    if ($x -eq 17 -or $y -eq 17) { return 'W' }
    return 'S'
}
for ($y = 0; $y -lt 30; $y++) {
    $line = ''
    for ($x = 0; $x -lt 30; $x++) {
        $bx = Band $x; $by = Band $y
        if ($bx -eq 'g' -or $by -eq 'g') { $ch = '.' }
        elseif ($bx -eq 'm' -and $by -eq 'm') { $ch = CellPx ($x - 6) ($y - 6) }
        elseif ($bx -eq 'm') { $ch = Prof $y }
        elseif ($by -eq 'm') { $ch = Prof $x }
        else {
            $a = Prof $x; $b = Prof $y
            $outerX = ($x -eq 0 -or $x -eq 29); $outerY = ($y -eq 0 -or $y -eq 29)
            if ($outerX -and $outerY) { $ch = '.' }
            elseif ($a -eq 'O' -or $b -eq 'O') { $ch = 'O' }
            elseif ($a -eq 'D' -or $b -eq 'D') { $ch = 'D' }
            elseif ($a -eq 'W' -or $b -eq 'W') { $ch = 'W' }
            else { $ch = 'L' }
        }
        $line += $ch
    }
    $frame.Add($line)
}
Icon 'shulker_widget' $frame.ToArray() @{ O = '#1a1a1a'; W = '#ffffff'; L = '#d6d6d6'; D = '#8c8c8c'; k = '#5a5a5a'; S = '#a0a0a0' }

# ---- map background (71x71 parchment) -----------------------------------------------------------------
$noise = New-NoiseGrid -Seed 1616 -Size 71 -Cell 6 -Octaves 2
$ramp = New-Ramp '#8a6a3c', '#cdb383', '#e8d6aa' 6
$grid = New-Object 'int[,]' 71, 71
for ($y = 0; $y -lt 71; $y++) {
    for ($x = 0; $x -lt 71; $x++) {
        $edge = [Math]::Min([Math]::Min($x, $y), [Math]::Min(70 - $x, 70 - $y))
        if ($edge -eq 0) { $grid[$x, $y] = 0 }
        elseif ($edge -lt 3) { $grid[$x, $y] = 1 + [int]($noise[$x, $y] * 1.5) }
        else { $grid[$x, $y] = 3 + [int]($noise[$x, $y] * 2.9) }
    }
}
Save-IndexGrid -Grid $grid -Ramp $ramp -Path (Join-Path $root 'map_background.png')

Write-Host "hud textures written to $root"
