# U12 crystals: original pixel art for corundum, crystal lamps, magnets, beacon beam pieces and
# particles. Dot-sourced by tools/pack/crystals.ps1 (which registers the files with the pack unit).
#
# Every texture is drawn here from hand-written rows and per-colour palettes; nothing is traced.

. "$PSScriptRoot/lib.ps1"

# Display colours of the nine corundum colours (the texture look, not the beacon colours).
$script:CrystalLook = [ordered]@{
    red    = '#d8343a'; orange = '#ec7f22'; yellow = '#ecd935'; green = '#45cc4a'; blue = '#3fc9df'
    indigo = '#3c4fd8'; violet = '#b346d6'; white = '#ecebf0'; black = '#2c2833'
}

function Get-CrystalPalette([string]$Color, [int]$Alpha = 255) {
    $base = $script:CrystalLook[$Color]
    $dark = if ($Color -eq 'black') { '#141218' } else { Shade-Color $base 0.55 }
    $light = if ($Color -eq 'black') { '#5a5366' } else { Shade-Color $base 1.35 }
    $shine = if ($Color -eq 'black') { '#8c8499' } else { Mix-Color $base '#ffffff' 0.7 }
    $mid = Shade-Color $base 0.8
    $a = { param($c, [int]$al) $x = ConvertTo-Color $c; ('#{0:x2}{1:x2}{2:x2}{3:x2}' -f $al, $x.R, $x.G, $x.B) }
    return @{
        d = (& $a $dark ([Math]::Min(255, $Alpha + 40))); m = (& $a $mid $Alpha); b = (& $a $base $Alpha)
        l = (& $a $light $Alpha); s = (& $a $shine ([Math]::Min(255, $Alpha + 30)))
    }
}

# A translucent faceted crystal block: facets of different brightness meeting at ridges.
$script:CorundumRows = @(
    'ddddddddddddddd',
    'dssllllbbbbmmmd',
    'dslllllbbbbmmmd',
    'dllsllbbbbmmmmd',
    'dlllslbbbmmmmmd',
    'dllllsbbmmmmmbd',
    'dbbbbbsmmmmmbbd',
    'dbbbbbmsmmmbbbd',
    'dmmbbmmmsmbbbld',
    'dmmmmmmmmsbbbld',
    'dmmmmmmmbbsblld',
    'dmmmmmmbbbbslld',
    'dmmmmmbbbbblsld',
    'dmmmmbbbbbllssd',
    'ddddddddddddddd'
)
function Save-CorundumTexture([string]$Color, [string]$Path) {
    # 15 rows of 15 are padded to 16 by repeating the frame edge (keeps tiling seams thin)
    $rows = @($script:CorundumRows | ForEach-Object { $_ + 'd' }) + @('dddddddddddddddd')
    Save-PixelArt -Rows $rows -Palette (Get-CrystalPalette $Color 200) -Path $Path
}

# A cluster of crystal spikes growing from the bottom edge (shown on a cross model).
$script:ClusterRows = @(
    '................',
    '.......s........',
    '.......sl.......',
    '......dsl.......',
    '..s...dsl.......',
    '..sl..dbl...s...',
    '..sl..dbl..lsd..',
    '..bl..dbm..lbd..',
    '.dbl..dbm..lbd..',
    '.dbm.ddbm..bmd..',
    '.dbm.dbbm.lbmd..',
    '.dbmddbbmdlbmd.s',
    '.dbmdbbmmdlbmdlb',
    'ddbmdbbmmdbbmdlb',
    'dbbmdbbmmdbmmdbm',
    'dbmmdbmmmdbmmdbm'
)
function Save-ClusterTexture([string]$Color, [string]$Path) {
    Save-PixelArt -Rows $script:ClusterRows -Palette (Get-CrystalPalette $Color 235) -Path $Path
}

# Crystal lamp: a light metal frame around a crystal pane; lit = bright centre with a glow.
$script:LampRows = @(
    'ffffffffffffffff',
    'fggggggggggggggf',
    'fgddddddddddddgf',
    'fgdssllbbbbmmdgf',
    'fgdslllbbbmmmdgf',
    'fgdllslbbmmmmdgf',
    'fgdlllsbmmmmbdgf',
    'fgdbbbbsmmmbbdgf',
    'fgdbbbmmsmbbbdgf',
    'fgdmmmmmmsbbldgf',
    'fgdmmmmmbbsbldgf',
    'fgdmmmmbbbbsldgf',
    'fgdmmmbbbbllsdgf',
    'fgddddddddddddgf',
    'fggggggggggggggf',
    'ffffffffffffffff'
)
function Save-LampTexture([string]$Color, [bool]$Lit, [string]$Path) {
    $p = if ($Lit) {
        $glow = Get-CrystalPalette $Color 255
        @{ d = $glow.m; m = $glow.b; b = $glow.l; l = $glow.s; s = '#ffffff' }
    } else { Get-CrystalPalette $Color 255 | ForEach-Object { $h = @{}; foreach ($k in $_.Keys) { $h[$k] = Shade-Color $_[$k] 0.62 }; $h } }
    $p.f = '#5b5960'; $p.g = if ($Lit) { '#b9b6c0' } else { '#87848f' }
    Save-PixelArt -Rows $script:LampRows -Palette $p -Path $Path
}

# Magnet: an iron casing; the front (pushing) pole is red, the back (pulling) pole blue, the
# sides show both halves. Powered textures light the poles up.
$script:MagnetFrontRows = @(
    'cccccccccccccccc',
    'ciiiiiiiiiiiiiic',
    'cirrrrrrrrrrrric',
    'cirRRRRRRRRRRric',
    'cirRhhhhhhhhRric',
    'cirRhrrrrrrhRric',
    'cirRhrcccrrhRric',
    'cirRhrcrcrrhRric',
    'cirRhrcccrrhRric',
    'cirRhrrrrrrhRric',
    'cirRhhhhhhhhRric',
    'cirRRRRRRRRRRric',
    'cirrrrrrrrrrrric',
    'ciiiiiiiiiiiiiic',
    'cicicicicicicicc',
    'cccccccccccccccc'
)
$script:MagnetSideRows = @(
    'cccccccccccccccc',
    'ciiiiiiiiiiiiiic',
    'cirrrrriiuuuuuic',
    'cirRRRriiuUUUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRhRriiuUhUuic',
    'cirRRRriiuUUUuic',
    'cirrrrriiuuuuuic',
    'ciiiiiiiiiiiiiic',
    'cicicicicicicicc',
    'cccccccccccccccc'
)
function Save-MagnetTextures([string]$Dir) {
    foreach ($on in $false, $true) {
        $sfx = if ($on) { 'on' } else { 'off' }
        # PowerShell hashtables are case-insensitive: upper-case row keys become x (bright red) / y (bright blue)
        $front = $script:MagnetFrontRows | ForEach-Object { $_.Replace('R', 'x') }
        $back = $script:MagnetFrontRows | ForEach-Object { $_.Replace('R', 'y').Replace('r', 'u') }
        $side = $script:MagnetSideRows | ForEach-Object { $_.Replace('R', 'x').Replace('U', 'y') }
        $p = @{
            c = '#4a4a50'; i = '#a9a9b0'
            r = $(if ($on) { '#c2262c' } else { '#8e2226' }); x = $(if ($on) { '#ff4a4a' } else { '#b0383c' })
            u = $(if ($on) { '#2440c6' } else { '#203686' }); y = $(if ($on) { '#4c78ff' } else { '#3450b0' })
            h = $(if ($on) { '#ffe6a0' } else { '#d0d0d8' })
        }
        Save-PixelArt -Rows $front -Palette $p -Path (Join-Path $Dir "magnet_front_$sfx.png")
        Save-PixelArt -Rows $back -Palette $p -Path (Join-Path $Dir "magnet_back_$sfx.png")
        Save-PixelArt -Rows $side -Palette $p -Path (Join-Path $Dir "magnet_side_$sfx.png")
    }
}

# Beacon beam pieces (entity textures): the core is a white stripy column (tinted by the entity's
# overlay colour), the glow a faint sheet in five alpha steps (255, 170, 112, 74, 0 of Quark's
# tinted-glass dimming).
function Save-BeamTextures([string]$Dir) {
    $core = for ($y = 0; $y -lt 16; $y++) { -join (0..15 | ForEach-Object { if ((($_ + $y) % 8) -lt 2) { 'a' } elseif ((($_ + $y) % 8) -lt 5) { 'b' } else { 'c' } }) }
    Save-PixelArt -Rows $core -Palette @{ a = '#ffffff'; b = '#f2f2f2'; c = '#e2e2e2' } -Path (Join-Path $Dir 'beacon_beam_core.png')
    $i = 0
    foreach ($alpha in 255, 170, 112, 74, 0) {
        $a = [int][Math]::Round(32 * $alpha / 255)
        $rows = for ($y = 0; $y -lt 16; $y++) { 'g' * 16 }
        Save-PixelArt -Rows $rows -Palette @{ g = ('#{0:x2}ffffff' -f $a) } -Path (Join-Path $Dir "beacon_beam_glow_$i.png")
        $i++
    }
}

function Save-ParticleTextures([string]$Dir) {
    $plus = @(
        '........', '...ww...', '...ww...', '.wwwwww.', '.wwwwww.', '...ww...', '...ww...', '........')
    $minus = @(
        '........', '........', '........', '.wwwwww.', '.wwwwww.', '........', '........', '........')
    $spark = @(
        '........', '...w....', '...w....', '.wwwww..', '...w....', '...w....', '........', '........')
    $dot = @(
        '........', '........', '...ww...', '..wwww..', '..wwww..', '...ww...', '........', '........')
    Save-PixelArt -Rows $plus -Palette @{ w = '#ffffff' } -Path (Join-Path $Dir 'magnet_plus.png')
    Save-PixelArt -Rows $minus -Palette @{ w = '#ffffff' } -Path (Join-Path $Dir 'magnet_minus.png')
    Save-PixelArt -Rows $spark -Palette @{ w = '#ffffff' } -Path (Join-Path $Dir 'corundum_spark.png')
    Save-PixelArt -Rows $dot -Palette @{ w = '#ffffff' } -Path (Join-Path $Dir 'corundum_dust.png')
}
