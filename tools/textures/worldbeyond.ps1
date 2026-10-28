# U29 worldbeyond textures (original pixel art): chorus weeds (3 variants), chorus twist,
# dusky myalite and the 16 colour steps of the myalite crystal.
#
# Dot-sourced by tools/pack/worldbeyond.ps1 (which registers the files); can also run alone:
#   pwsh -NoProfile -File tools/textures/worldbeyond.ps1
. "$PSScriptRoot/lib.ps1"

$script:WbTex = Join-Path (Resolve-Path "$PSScriptRoot/../..").Path 'assets/pack/rp/textures/blocks'

function Get-WbTexPath([string]$Name) { return (Join-Path $script:WbTex "$Name.png") }

# Chorus plants: pale violet stems with lilac buds, in the colours of vanilla chorus plants.
$script:ChorusPalette = @{
    d = '#4a2f4f'   # dark stem
    s = '#7b5a80'   # stem
    l = '#a582a8'   # light stem
    b = '#c9a6cc'   # bud
    h = '#ead7ec'   # bud highlight
}

$script:ChorusWeeds = @(
    @(
        '................',
        '................',
        '..........h.....',
        '...b.....bb.....',
        '...bh....bs.....',
        '....s....s......',
        '....s...ls...b..',
        '.b..ls..s...bh..',
        '.bh..s.ls...s...',
        '..s..sls...ls...',
        '..ls.ss...ss....',
        '...ssls..ss.....',
        '....sdsssd......',
        '.....dsdd.......',
        '.....ddsd.......',
        '......dd........'
    ),
    @(
        '................',
        '.......b........',
        '......bh........',
        '......s.....b...',
        '..b...s....bh...',
        '..hb..ls...s....',
        '...s...s..ls....',
        '...ls..s..s.....',
        '....s..sl.s..b..',
        '....ls.ss.s.bh..',
        '.....s.sssl.s...',
        '.....sdsss.ls...',
        '......dsddss....',
        '......dssd......',
        '.......dd.......',
        '.......dd.......'
    ),
    @(
        '................',
        '................',
        '................',
        '.....b......b...',
        '....bh.....hb...',
        '....s......s....',
        '.b..ls....ls....',
        '.hb..s....s..b..',
        '..s..sl..ls.bh..',
        '..ls..s..s..s...',
        '...s..ss.s.ls...',
        '...lss.sss.s....',
        '.....sdsdsss....',
        '......dsdd......',
        '......dssd......',
        '.......dd.......'
    )
)

$script:ChorusTwist = @(
    '.......bh.......',
    '......bb........',
    '......s.........',
    '.......s........',
    '........s..b....',
    '.......ls.bh....',
    '......ls..s.....',
    '.b....s..ls.....',
    '.hb...s.ls......',
    '..s....ss.......',
    '..ls...s........',
    '...ss..sl.......',
    '....sslds.......',
    '......dsd.......',
    '......dsd.......',
    '.......d........'
)

# Myalite crystal: a faceted crystal block, drawn in grey and tinted per colour step.
$script:CrystalRows = @(
    'kkkkkkkkkkkkkkkk',
    'kwwhhhhhhhhhhhmk',
    'kwhhhmmmmmhhhmmk',
    'khhmmmhhhmmmhmmk',
    'khmmhhwwhhmmmmdk',
    'khmhhwwhhhmmmddk',
    'khmhwwhhhmmmdddk',
    'khmhwhhhmmmdddmk',
    'khmmhhhmmmdddmmk',
    'khmmmhmmmdddmmhk',
    'khhmmmmmdddmmhhk',
    'kmhhmmmdddmmhhmk',
    'kmmhhmdddmmhhmmk',
    'kmmmhhddmmhhmmdk',
    'kdmmmhhmmhhmmddk',
    'kkkkkkkkkkkkkkkk'
)
$script:CrystalGreys = @{ k = 0.42; d = 0.55; m = 0.70; h = 0.86; w = 1.0 }

# HSB -> RGB (java.awt.Color.HSBtoRGB semantics).
function ConvertFrom-Hsb([double]$H, [double]$S, [double]$B) {
    $h6 = ($H - [Math]::Floor($H)) * 6.0
    $f = $h6 - [Math]::Floor($h6)
    $p = $B * (1 - $S); $q = $B * (1 - $S * $f); $t = $B * (1 - $S * (1 - $f))
    switch ([int][Math]::Floor($h6)) {
        0 { $r = $B; $g = $t; $bb = $p }
        1 { $r = $q; $g = $B; $bb = $p }
        2 { $r = $p; $g = $B; $bb = $t }
        3 { $r = $p; $g = $q; $bb = $B }
        4 { $r = $t; $g = $p; $bb = $B }
        default { $r = $B; $g = $p; $bb = $q }
    }
    return @([int]($r * 255 + 0.5), [int]($g * 255 + 0.5), [int]($bb * 255 + 0.5))
}

# Colour step i of the crystal: hue 0.6 + i * 0.02, saturation 0.7, brightness 0.8 (Quark's colour
# logic), multiplied with the grey crystal; translucent body, more opaque rim.
function New-CrystalTexture([string]$Path, [int]$Step) {
    $rgb = ConvertFrom-Hsb (0.6 + $Step * 0.02) 0.7 0.8
    $pal = @{}
    foreach ($k in $script:CrystalGreys.Keys) {
        $v = $script:CrystalGreys[$k]
        $a = if ($k -eq 'k') { 235 } elseif ($k -eq 'w') { 220 } else { 190 }
        $r = [Math]::Min(255, [int]($rgb[0] * $v * 1.15)); $g = [Math]::Min(255, [int]($rgb[1] * $v * 1.15)); $b = [Math]::Min(255, [int]($rgb[2] * $v * 1.15))
        $pal[$k] = '#{0:x2}{1:x2}{2:x2}{3:x2}' -f $a, $r, $g, $b
    }
    Save-PixelArt -Rows $script:CrystalRows -Palette $pal -Path $Path
}

function Write-WorldBeyondTextures {
    $out = @()
    for ($i = 0; $i -lt 3; $i++) {
        $p = Get-WbTexPath "chorus_weeds_$i"
        Save-PixelArt -Rows $script:ChorusWeeds[$i] -Palette $script:ChorusPalette -Path $p
        $out += $p
    }
    $p = Get-WbTexPath 'chorus_twist'
    Save-PixelArt -Rows $script:ChorusTwist -Palette $script:ChorusPalette -Path $p
    $out += $p
    # Dusky myalite: a dark, dusky purple stone with faint lighter veins.
    $p = Get-WbTexPath 'dusky_myalite'
    New-StoneTexture -Path $p -Ramp (New-Ramp '#2a1f30', '#4b3652', '#6c5274' 5) -Seed 2901
    $out += $p
    for ($i = 0; $i -lt 16; $i++) {
        $p = Get-WbTexPath "myalite_crystal_$i"
        New-CrystalTexture -Path $p -Step $i
        $out += $p
    }
    return $out
}

if ($MyInvocation.InvocationName -ne '.') { Write-WorldBeyondTextures | Out-Null }
