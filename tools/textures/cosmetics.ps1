# U18 cosmetics textures and sounds (original art, procedural):
#   - animal variant / shiny skins, pride bees, beach and ocean villager clothes (cosmetics-mobs.cs,
#     painted on the vanilla models' box-UV layouts by cosmetics-painter.cs),
#   - the camera icon and the bucket icons of BucketsShowInhabitants (pixel rows below),
#   - the camera shutter sound (synthesised WAV).
# Run: pwsh tools/textures/cosmetics.ps1   (writes into assets/pack/rp; commit the output)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp'

Add-Type -ReferencedAssemblies System.Drawing, System.Drawing.Primitives, System.Drawing.Common, System.Collections, System.Runtime `
    -Path "$PSScriptRoot/cosmetics-painter.cs", "$PSScriptRoot/cosmetics-mobs.cs"
[LeptonTex.Mobs]::Run($rp)

# ---- camera icon (16x16) -------------------------------------------------------------------------
$camera = @(
    '................',
    '................',
    '..........kkk...',
    '...kkkk..kyyyk..',
    '..kddddkkkkkkk..',
    '.kggggggggggggk.',
    '.kgllgkkkkkgggk.',
    '.kgggkbbbbbkggk.',
    '.kgggkbccbbkggk.',
    '.kgggkbcbbbkggk.',
    '.kgggkbbbbbkggk.',
    '.kggggkkkkkgggk.',
    '.kdddddddddddddk',
    '..kkkkkkkkkkkkk.',
    '................',
    '................'
)
# Fix the row width of the last frame row.
$camera[12] = '.kddddddddddddk.'
Save-PixelArt -Rows $camera -Palette @{ k = '#1c1c22'; d = '#3a3a44'; g = '#5c5e6a'; l = '#a8acb8'; b = '#2a3c66'; c = '#8fb4ff'; y = '#ffd84a' } `
    -Path (Join-Path $rp 'textures/lepton/cosmetics/camera.png')

# ---- bucket icons (16x16) ------------------------------------------------------------------------
# The bucket: rim, water surface, body. Creatures are drawn over the water.
$bucket = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '..kkkkkkkkkkkk..',
    '.kmwwwwwwwwwwmk.',
    '.kmwwvwwwwvwwmk.',
    '..kmmmmmmmmmmk..',
    '..kliiiiiiiihk..',
    '..kliiiiiiiihk..',
    '...kliiiiiihk...',
    '...kliiiiiihk...',
    '...kliiiiiihk...',
    '....kkkkkkkk....',
    '................'
)
$bucketPal = @{ k = '#2c2c30'; m = '#9a9ca6'; w = '#3f6fd8'; v = '#6f9cf0'; l = '#d8dae2'; i = '#b4b7c2'; h = '#8a8d98' }
function Save-Bucket([string[]]$Overlay, [hashtable]$Extra, [string]$Name, [string[]]$Base = $bucket) {
    $rows = Merge-Rows $Base $Overlay
    $pal = $bucketPal.Clone()
    foreach ($k in $Extra.Keys) { $pal[$k] = $Extra[$k] }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $rp "textures/lepton/cosmetics/buckets/$Name.png")
}

# Axolotls: a head with frilly gills peeking out of the water.
$axolotl = @(
    '................',
    '................',
    '...g........g...',
    '....g.aaaa.g....',
    '...ggaaaaaagg...',
    '....aeaaaaea....',
    '....aaaddaaa....',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
$axolotls = [ordered]@{
    lucy = @{ a = '#f4a8c8'; g = '#e0508a'; e = '#2a1a22'; d = '#c86a94' }
    wild = @{ a = '#8a6a50'; g = '#5e4432'; e = '#1a1210'; d = '#6e523c' }
    gold = @{ a = '#f2c84a'; g = '#e08a2a'; e = '#2a1a0a'; d = '#c8962a' }
    cyan = @{ a = '#d8f2f6'; g = '#e86aa0'; e = '#2a1a22'; d = '#a8d0d8' }
    blue = @{ a = '#5a72d8'; g = '#f2d64a'; e = '#141428'; d = '#3f52a8' }
}
foreach ($k in $axolotls.Keys) { Save-Bucket $axolotl $axolotls[$k] "axolotl_$k" }

# Crabs: two raised claws and eyes on stalks.
$crab = @(
    '................',
    '................',
    '..cc........cc..',
    '..cs..e..e..sc..',
    '...c..s..s..c...',
    '...ccbbbbbbcc...',
    '....bbbbbbbb....',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
$crabs = [ordered]@{
    red   = @{ c = '#c8442a'; b = '#a8361e'; s = '#e8805a'; e = '#141414' }
    blue  = @{ c = '#3a6ac8'; b = '#2a52a0'; s = '#7aa2e8'; e = '#141414' }
    green = @{ c = '#4aa042'; b = '#367e30'; s = '#8ad07a'; e = '#141414' }
}
foreach ($k in $crabs.Keys) { Save-Bucket $crab $crabs[$k] "crab_$k" }

# Shiny slime (golden) sitting in the bucket.
$slime = @(
    '................',
    '................',
    '................',
    '....oooooooo....',
    '...oyyyyyyyyo...',
    '...oyeyyyyeyo...',
    '...oyyyymyyyo...',
    '....oooooooo....',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
Save-Bucket $slime @{ o = '#e3a930'; y = '#f6d86a'; e = '#4a2e08'; m = '#4a2e08' } 'slime_shiny'

# Tropical fish: the bucket alone, then the two body shapes and six patterns per shape as white
# masks (the client tints them with the fish's base and pattern colours).
$empty = @('................') * 16
Save-Bucket $empty @{} 'tropical_bucket'
$shapeA = @(
    '................',
    '................',
    '................',
    '.....ffffff.....',
    '...fffffffff.f..',
    '..fefffffffffff.',
    '...fffffffff.f..',
    '.....ffffff.....',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
$shapeB = @(
    '................',
    '................',
    '.......ff.......',
    '.....fffff......',
    '....ffffffff.ff.',
    '...fefffffffff..',
    '...fffffffffff..',
    '....ffffffff.ff.',
    '.....fffff......',
    '.......f........',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
foreach ($s in @(@{ n = 'a'; r = $shapeA }, @{ n = 'b'; r = $shapeB })) {
    $body = $s.r | ForEach-Object { $_ -replace 'e', 'f' }
    Save-PixelArt -Rows $body -Palette @{ f = '#d8d8d8' } -Path (Join-Path $rp "textures/lepton/cosmetics/buckets/tropical_$($s.n).png")
    $eyes = $s.r | ForEach-Object { ($_ -replace 'f', '.') }
    # Patterns: stripes, spots, a band, a tail, a top fin, a belly.
    $patterns = @(
        { param($x, $y) ($x % 3) -eq 0 },
        { param($x, $y) (($x + $y) % 4) -eq 0 },
        { param($x, $y) $x -ge 7 -and $x -le 9 },
        { param($x, $y) $x -ge 11 },
        { param($x, $y) $y -le 4 },
        { param($x, $y) $y -ge 7 }
    )
    for ($pi = 0; $pi -lt $patterns.Count; $pi++) {
        $test = $patterns[$pi]
        $rows = for ($y = 0; $y -lt 16; $y++) {
            -join (0..15 | ForEach-Object { if ($s.r[$y][$_] -eq 'f' -and (& $test $_ $y)) { 'p' } elseif ($s.r[$y][$_] -eq 'e') { 'e' } else { '.' } })
        }
        Save-PixelArt -Rows $rows -Palette @{ p = '#ffffff'; e = '#141414' } -Path (Join-Path $rp "textures/lepton/cosmetics/buckets/tropical_$($s.n)_$($pi + 1).png")
    }
}

# ---- camera shutter sound ---------------------------------------------------------------------
# Two short clicks (mirror up / down) made from decaying filtered noise; 44.1 kHz mono 16-bit WAV.
$rate = 44100
$len = [int]($rate * 0.22)
$samples = New-Object 'int16[]' $len
$rng = [Random]::new(18)
$lp = 0.0
for ($i = 0; $i -lt $len; $i++) {
    $t = $i / $rate
    $env1 = [Math]::Exp(-($t) * 90)
    $env2 = if ($t -gt 0.09) { [Math]::Exp(-($t - 0.09) * 70) * 0.8 } else { 0 }
    $n = $rng.NextDouble() * 2 - 1
    $lp = $lp + 0.35 * ($n - $lp)
    $tone = [Math]::Sin(2 * [Math]::PI * 1800 * $t) * 0.25
    $v = ($lp + $tone) * ($env1 + $env2)
    $samples[$i] = [int16]([Math]::Max(-1, [Math]::Min(1, $v)) * 26000)
}
$wav = Join-Path $rp 'sounds/lepton/camera_shutter.wav'
New-Item -ItemType Directory -Force (Split-Path $wav) | Out-Null
$fs = [IO.File]::Create($wav)
$bw = [IO.BinaryWriter]::new($fs)
$dataBytes = $len * 2
$bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $dataBytes)); $bw.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
$bw.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]1)
$bw.Write([int]$rate); $bw.Write([int]($rate * 2)); $bw.Write([int16]2); $bw.Write([int16]16)
$bw.Write([Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]$dataBytes)
foreach ($s in $samples) { $bw.Write($s) }
$bw.Close()
Write-Host "cosmetics textures and sounds written"
