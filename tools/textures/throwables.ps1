# Textures of U22 throwables (original pixel art). Run: pwsh tools/textures/throwables.ps1
. "$PSScriptRoot/lib.ps1"

$root  = Resolve-Path "$PSScriptRoot/../.."
$items = Join-Path $root 'assets/pack/rp/textures/items'
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$entity = Join-Path $root 'assets/pack/rp/textures/entity/lepton'

# Pads (or cuts) every row to $Width so hand-drawn rows need no exact counting.
function Fit-Rows([string[]]$Rows, [int]$Width = 16, [int]$Height = 16) {
    $out = foreach ($r in $Rows) { if ($r.Length -ge $Width) { $r.Substring(0, $Width) } else { $r + ('.' * ($Width - $r.Length)) } }
    $out = @($out)
    while ($out.Count -lt $Height) { $out += ('.' * $Width) }
    return , $out
}

# ---- pickarang / flamerang: a V-shaped throwing pick ------------------------------------------
$rang = Fit-Rows @(
    '..........kkk',
    '.........kcbk',
    '........kccek',
    '.......kwcek',
    '......kwlwk',
    '.....kwlwk',
    '....kwlwk',
    '...kwlwdk',
    '...kwdwwk',
    '....kdwwlk',
    '.....kdwwlk',
    '......kdwwlk',
    '.......kdwcek',
    '........kccbk',
    '.........kcck',
    '..........kkk'
)
Save-PixelArt -Rows $rang -Path "$items/pickarang.png" -Palette @{
    k = '#2a1c10'; w = '#8b5a2b'; l = '#b07a40'; d = '#5e3b1a'
    c = '#4fe0d2'; b = '#c9fff7'; e = '#1f9c92'
}
Save-PixelArt -Rows $rang -Path "$items/flamerang.png" -Palette @{
    k = '#1a1416'; w = '#4a3f44'; l = '#6d6066'; d = '#2f272b'
    c = '#f07a1a'; b = '#ffd36b'; e = '#b23a10'
}

# ---- torch arrow --------------------------------------------------------------------------------
$torchArrow = Fit-Rows @(
    '................',
    '............yy',
    '...........yrry',
    '...........yry',
    '..........kcy',
    '.........skc',
    '........sk',
    '.......s',
    '......s',
    '.....s',
    '....s',
    'f..s',
    'ffs',
    '.fs',
    'f.f'
)
Save-PixelArt -Rows $torchArrow -Path "$items/torch_arrow.png" -Palette @{
    y = '#ffd84a'; r = '#ff8a1c'; k = '#3a2a1a'; c = '#5c4326'; s = '#8a6236'; f = '#e8e8e8'
}

# Entity texture (32x32): side view of the arrow in the top-left 16x5 strip, used by both crossed
# planes of geometry.lepton.torch_arrow.
$arrowSide = @(
    'f..............yy',
    'ff.sssssssssskcyry',
    'fffsssssssssskcrrr',
    'ff.sssssssssskcyry',
    'f..............yy'
)
$arrowRows = @($arrowSide | ForEach-Object { $_ }) + @(1..27 | ForEach-Object { '' })
Save-PixelArt -Rows (Fit-Rows $arrowRows 32 32) -Path "$entity/torch_arrow.png" -Palette @{
    y = '#ffd84a'; r = '#ff8a1c'; k = '#3a2a1a'; c = '#5c4326'; s = '#8a6236'; f = '#e8e8e8'
}

# ---- parrot eggs ----------------------------------------------------------------------------------
$egg = Fit-Rows @(
    '................',
    '................',
    '......kkkk',
    '.....kaaahk',
    '....kaaaaahk',
    '....kaabaaak',
    '...kaaaaaabak',
    '...kaabaaaaak',
    '...kaaaaabaak',
    '...kabaaaaaak',
    '...kaaaaaaadk',
    '....kaaabaadk',
    '....kdaaaaddk',
    '.....kddddk',
    '......kkkk'
)
$eggColors = [ordered]@{
    red_blue    = @('#c8302c', '#2f5fd0')
    blue        = @('#2c55c8', '#9bbcff')
    green       = @('#3f9f2b', '#bfe873')
    yellow_blue = @('#2bb3c4', '#f0d23c')
    gray        = @('#9d9d9d', '#e4e4e4')
}
foreach ($v in $eggColors.Keys) {
    $base = $eggColors[$v][0]; $spot = $eggColors[$v][1]
    Save-PixelArt -Rows $egg -Path "$items/egg_parrot_$v.png" -Palette @{
        k = (ConvertTo-Hex (Shade-Color $base 0.45)); a = $base; b = $spot
        d = (ConvertTo-Hex (Shade-Color $base 0.75)); h = (ConvertTo-Hex (Shade-Color $base 1.35))
    }
}

# ---- bottled cloud ------------------------------------------------------------------------------
$bottle = Fit-Rows @(
    '................',
    '......kkkk',
    '......kppk',
    '......kggk',
    '.....kgqqgk',
    '....kgqqqqgk',
    '...kgcccccqgk',
    '...kccmcccccgk',
    '...kcccccmcccgk',
    '...kcmcccccccgk',
    '...kccccccmccgk',
    '...kgcccccccggk',
    '....kgccccccgk',
    '.....kggggggk',
    '......kkkkkk'
)
Save-PixelArt -Rows $bottle -Path "$items/bottled_cloud.png" -Palette @{
    k = '#3d4b5c'; p = '#8b5a2b'; g = '#b8d7e8'; q = '#dcecf5'; c = '#ffffff'; m = '#d9e2ea'
}

# ---- slime in a bucket (calm and excited) -------------------------------------------------------
$bucketBase = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '..kkkkkkkkkkkk',
    '..kmmmmmmmmmmk',
    '..kissssssssik',
    '...kissssssik',
    '...kihhhhhhik',
    '...kiiiiiiiik',
    '....kiiiiiik',
    '....kiiiiiik',
    '....kddddddk',
    '.....kkkkkk'
)
$calm = Merge-Rows (Fit-Rows $bucketBase) (Fit-Rows @(
    '................',
    '................',
    '................',
    '................',
    '....oooooooo',
    '...oggllgggo',
    '...gexgggexg'
))
$excited = Merge-Rows (Fit-Rows $bucketBase) (Fit-Rows @(
    '................',
    '....oooooooo',
    '...oglllgggo',
    '...ggexggexgg',
    '...ggggggggg',
    '...gggaaaggg',
    '...ogggggggo'
))
$slimePal = @{
    k = '#2b2f33'; m = '#c9ced3'; i = '#9aa1a8'; s = '#6aa84f'; h = '#b6bcc2'; d = '#6f767c'
    o = '#3e7a2f'; g = '#78c25a'; l = '#a9e58c'; e = '#1d3a14'; x = '#e9ffe0'; a = '#2f5e22'
}
Save-PixelArt -Rows $calm -Palette $slimePal -Path "$items/slime_in_a_bucket.png"
Save-PixelArt -Rows $excited -Palette $slimePal -Path "$items/slime_in_a_bucket_excited.png"

# ---- cloud block: soft white noise ----------------------------------------------------------------
$noise = New-NoiseGrid -Seed 2207 -Size 16 -Cell 4 -Octaves 2
$ramp  = New-Ramp '#dfe6ee', '#f4f7fa', '#ffffff' 4
Save-IndexGrid -Grid (ConvertTo-IndexGrid $noise $ramp.Count) -Ramp $ramp -Path "$blocks/cloud.png"

Write-Host 'throwables textures written'
