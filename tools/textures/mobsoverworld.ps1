# U20 mobsoverworld: entity models (geometry) and textures for the crab, shiba and oretoise, plus
# the item icons. Everything is original pixel art drawn here from seeded noise and hand-placed
# details; the geometry and its UV layout are defined in this file so the textures line up.
#
#   pwsh -NoProfile -File tools/textures/mobsoverworld.ps1     # regenerates the committed output
. "$PSScriptRoot/lib.ps1"

$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$RP = Join-Path $root 'assets/pack/rp'
$EntTex = Join-Path $RP 'textures/entity/lepton'
$ItemTex = Join-Path $RP 'textures/items'
$Geo = Join-Path $RP 'models/entity'
New-Item -ItemType Directory -Force $EntTex, $ItemTex, $Geo | Out-Null

# ---------------------------------------------------------------------------------------------
# Model helpers. A cube is @{ o = @(x,y,z); s = @(w,h,d); uv = @(u,v); mat = 'key'; inflate = n }.
# A bone is @{ name; parent; pivot = @(x,y,z); rot = @(x,y,z); cubes = @(...) }.
# ---------------------------------------------------------------------------------------------

function Write-Geo([string]$File, [string]$Id, [int]$TexW, [int]$TexH, [object[]]$Bones, [double]$BoundsW = 2, [double]$BoundsH = 2) {
    $bonesJson = foreach ($b in $Bones) {
        $j = [ordered]@{ name = $b.name }
        if ($b.parent) { $j.parent = $b.parent }
        $j.pivot = @($b.pivot)
        if ($b.rot) { $j.rotation = @($b.rot) }
        if ($b.cubes) {
            $j.cubes = @(foreach ($c in $b.cubes) {
                    $cj = [ordered]@{ origin = @($c.o); size = @($c.s); uv = @($c.uv) }
                    if ($c.inflate) { $cj.inflate = $c.inflate }
                    if ($c.mirror) { $cj.mirror = $true }
                    if ($c.pivot) { $cj.pivot = @($c.pivot); $cj.rotation = @($c.rot) }
                    $cj
                })
        }
        if ($b.locators) { $j.locators = $b.locators }
        $j
    }
    $doc = [ordered]@{
        format_version     = '1.12.0'
        'minecraft:geometry' = @([ordered]@{
                description = [ordered]@{
                    identifier            = $Id
                    texture_width         = $TexW
                    texture_height        = $TexH
                    visible_bounds_width  = $BoundsW
                    visible_bounds_height = $BoundsH
                    visible_bounds_offset = @(0, ($BoundsH / 2), 0)
                }
                bones       = @($bonesJson)
            })
    }
    $text = ($doc | ConvertTo-Json -Depth 50) -replace "`r`n", "`n"
    [IO.File]::WriteAllText((Join-Path $Geo $File), $text + "`n")
}

# Canvas: a bitmap plus a deterministic random source.
function New-Canvas([int]$W, [int]$H, [int]$Seed) {
    return [pscustomobject]@{
        Bmp = [System.Drawing.Bitmap]::new($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        Rng = [System.Random]::new($Seed)
    }
}
function Save-Canvas($C, [string]$Path) { $C.Bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $C.Bmp.Dispose() }
function Set-Px($C, [int]$X, [int]$Y, $Color) {
    if ($X -ge 0 -and $Y -ge 0 -and $X -lt $C.Bmp.Width -and $Y -lt $C.Bmp.Height) { $C.Bmp.SetPixel($X, $Y, (ConvertTo-Color $Color)) }
}
# Fills a rectangle with a colour ramp picked by noise; `bias` shifts towards light (+) / dark (-).
function Fill-Noise($C, [int]$X, [int]$Y, [int]$W, [int]$H, [object[]]$Ramp, [double]$Bias = 0) {
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $t = 0.5 + $Bias + ($C.Rng.NextDouble() - 0.5) * 0.55
            Set-Px $C ($X + $i) ($Y + $j) (Get-RampColor $Ramp $t)
        }
    }
}
# Paints the box-UV net of a cube: top light, bottom dark, sides mid; `TopRamp` overrides the top.
function Paint-Cube($C, $Cube, [object[]]$Ramp, [object[]]$TopRamp = $null) {
    $u = $Cube.uv[0]; $v = $Cube.uv[1]; $w = $Cube.s[0]; $h = $Cube.s[1]; $d = $Cube.s[2]
    $w = [int][Math]::Ceiling($w); $h = [int][Math]::Ceiling($h); $d = [int][Math]::Ceiling($d)
    $top = if ($TopRamp) { $TopRamp } else { $Ramp }
    Fill-Noise $C ($u + $d) $v $w $d $top 0.2                 # up
    Fill-Noise $C ($u + $d + $w) $v $w $d $Ramp -0.25         # down
    Fill-Noise $C $u ($v + $d) $d $h $Ramp -0.05              # east side
    Fill-Noise $C ($u + $d) ($v + $d) $w $h $Ramp 0.05        # north (front)
    Fill-Noise $C ($u + $d + $w) ($v + $d) $d $h $Ramp -0.05  # west side
    Fill-Noise $C ($u + 2 * $d + $w) ($v + $d) $w $h $Ramp -0.1 # south (back)
}
# Front (north) face origin of a cube in texture space.
function Get-Front($Cube) { return @(($Cube.uv[0] + $Cube.s[2]), ($Cube.uv[1] + $Cube.s[2])) }
function Get-Top($Cube) { return @(($Cube.uv[0] + $Cube.s[2]), $Cube.uv[1]) }

# =============================================================================================
# Crab (64 x 32). Java size 0.9 x 0.5. Faces north (-z).
# =============================================================================================
$crabBody = @{ o = @(-4, 3, -3); s = @(8, 3, 6); uv = @(0, 0) }
$crabEyeL = @{ o = @(-3, 6, -3); s = @(1, 3, 1); uv = @(0, 9) }
$crabEyeR = @{ o = @(2, 6, -3); s = @(1, 3, 1); uv = @(4, 9) }
$crabArmL = @{ o = @(-7, 3, -5); s = @(3, 2, 2); uv = @(28, 0) }
$crabClawL = @{ o = @(-8, 2, -9); s = @(4, 3, 4); uv = @(28, 4) }
$crabArmR = @{ o = @(4, 3, -5); s = @(3, 2, 2); uv = @(28, 0); mirror = $true }
$crabClawR = @{ o = @(4, 2, -9); s = @(4, 3, 4); uv = @(28, 4); mirror = $true }
$crabLeg = @{ s = @(5, 1, 1); uv = @(0, 16) }
$crabBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 3, 0); cubes = @($crabBody) }
    @{ name = 'eyes'; parent = 'body'; pivot = @(0, 6, -2.5); cubes = @($crabEyeL, $crabEyeR) }
    @{ name = 'left_claw'; parent = 'body'; pivot = @(-4, 4, -4); cubes = @($crabArmL, $crabClawL) }
    @{ name = 'right_claw'; parent = 'body'; pivot = @(4, 4, -4); cubes = @($crabArmR, $crabClawR) }
)
$legZ = @(-2, 0, 2)
for ($i = 0; $i -lt 3; $i++) {
    $z = $legZ[$i]
    $crabBones += @{ name = "left_leg$i"; parent = 'body'; pivot = @(-4, 4, ($z + 0.5)); rot = @(0, 0, -35)
        cubes = @(@{ o = @(-9, 3.5, $z); s = $crabLeg.s; uv = $crabLeg.uv }) }
    $crabBones += @{ name = "right_leg$i"; parent = 'body'; pivot = @(4, 4, ($z + 0.5)); rot = @(0, 0, 35)
        cubes = @(@{ o = @(4, 3.5, $z); s = $crabLeg.s; uv = $crabLeg.uv; mirror = $true }) }
}
Write-Geo 'lepton_crab.geo.json' 'geometry.lepton.crab' 64 32 $crabBones 2 1.5

$crabColours = @{
    red   = @('#4a1408', '#8e2a14', '#c4502a', '#e98454')
    blue  = @('#13234a', '#24468a', '#3f72c0', '#80aee6')
    green = @('#173a17', '#2c6b2a', '#4c9a3c', '#8acb6c')
}
$seed = 2000
foreach ($name in 'red', 'blue', 'green') {
    $ramp = $crabColours[$name]
    $c = New-Canvas 64 32 ($seed++)
    Paint-Cube $c $crabBody $ramp
    Paint-Cube $c $crabArmL $ramp
    Paint-Cube $c $crabClawL $ramp (@($ramp[1], $ramp[2], $ramp[3]))
    Paint-Cube $c $crabLeg (@($ramp[0], $ramp[1], $ramp[2]))
    Paint-Cube $c $crabEyeL @('#2a1a14', '#4a3226')
    Paint-Cube $c $crabEyeR @('#2a1a14', '#4a3226')
    # eye tips: black pupils with a white glint on top of each stalk
    foreach ($eye in $crabEyeL, $crabEyeR) {
        $f = Get-Front $eye; Set-Px $c $f[0] $f[1] '#0b0b0b'
        $t = Get-Top $eye; Set-Px $c $t[0] $t[1] '#f0f0f0'
    }
    # shell: a lighter rim along the front, a few spots on top, claw tips dark
    $f = Get-Front $crabBody
    for ($x = 0; $x -lt 8; $x++) { Set-Px $c ($f[0] + $x) $f[1] $ramp[3] }
    Set-Px $c ($f[0] + 3) ($f[1] + 1) '#1a0d08'; Set-Px $c ($f[0] + 4) ($f[1] + 1) '#1a0d08'   # mouth
    $t = Get-Top $crabBody
    foreach ($p in @(1, 1), @(5, 2), @(3, 4), @(6, 4)) { Set-Px $c ($t[0] + $p[0]) ($t[1] + $p[1]) $ramp[3] }
    $cf = Get-Front $crabClawL
    for ($y = 0; $y -lt 3; $y++) { Set-Px $c ($cf[0] + 1) ($cf[1] + $y) $ramp[0] }
    Save-Canvas $c (Join-Path $EntTex "crab_$name.png")
}
# Mould overlay: mossy patches on the shell top and claws, transparent elsewhere.
$c = New-Canvas 64 32 2100
$moss = @('#3b4f1e', '#5d7a2a', '#86a43e')
$t = Get-Top $crabBody
for ($y = 0; $y -lt 6; $y++) { for ($x = 0; $x -lt 8; $x++) { if ($c.Rng.NextDouble() -lt 0.45) { Set-Px $c ($t[0] + $x) ($t[1] + $y) (Get-RampColor $moss $c.Rng.NextDouble()) } } }
$f = Get-Front $crabBody
for ($x = 0; $x -lt 8; $x++) { if ($c.Rng.NextDouble() -lt 0.5) { Set-Px $c ($f[0] + $x) ($f[1]) $moss[1] } }
$t = Get-Top $crabClawL
for ($y = 0; $y -lt 4; $y++) { for ($x = 0; $x -lt 4; $x++) { if ($c.Rng.NextDouble() -lt 0.35) { Set-Px $c ($t[0] + $x) ($t[1] + $y) $moss[2] } } }
Save-Canvas $c (Join-Path $EntTex 'crab_mold.png')

# =============================================================================================
# Shiba (64 x 32). Java size 0.8 x 0.8. Faces north.
# =============================================================================================
$sBody = @{ o = @(-3, 5, -5); s = @(6, 5, 10); uv = @(0, 0) }
$sHead = @{ o = @(-3, 6, -10); s = @(6, 6, 5); uv = @(32, 0) }
$sSnout = @{ o = @(-1.5, 6, -12); s = @(3, 3, 2); uv = @(54, 0) }
$sEarL = @{ o = @(-3, 12, -8); s = @(2, 2, 1); uv = @(54, 5) }
$sEarR = @{ o = @(1, 12, -8); s = @(2, 2, 1); uv = @(54, 5); mirror = $true }
$sTail = @{ o = @(-1, 9, 4); s = @(2, 2, 5); uv = @(32, 11) }
$sLeg = @{ s = @(2, 5, 2); uv = @(0, 15) }
$sCollar = @{ o = @(-3, 5, -6); s = @(6, 6, 1); uv = @(0, 24); inflate = 0.3 }
$shibaBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 7.5, 0); cubes = @($sBody) }
    @{ name = 'head'; parent = 'body'; pivot = @(0, 9, -5); cubes = @($sHead, $sSnout, $sEarL, $sEarR)
        locators = [ordered]@{ lead = @(0, 8, -11) } }
    @{ name = 'rightItem'; parent = 'head'; pivot = @(0, 7, -11.5) }
    @{ name = 'tail'; parent = 'body'; pivot = @(0, 10, 4); rot = @(-40, 0, 0); cubes = @($sTail) }
    @{ name = 'leg0'; parent = 'root'; pivot = @(-2, 5, -3); cubes = @(@{ o = @(-3, 0, -4); s = $sLeg.s; uv = $sLeg.uv }) }
    @{ name = 'leg1'; parent = 'root'; pivot = @(2, 5, -3); cubes = @(@{ o = @(1, 0, -4); s = $sLeg.s; uv = $sLeg.uv; mirror = $true }) }
    @{ name = 'leg2'; parent = 'root'; pivot = @(-2, 5, 3); cubes = @(@{ o = @(-3, 0, 2); s = $sLeg.s; uv = $sLeg.uv }) }
    @{ name = 'leg3'; parent = 'root'; pivot = @(2, 5, 3); cubes = @(@{ o = @(1, 0, 2); s = $sLeg.s; uv = $sLeg.uv; mirror = $true }) }
)
Write-Geo 'lepton_shiba.geo.json' 'geometry.lepton.shiba' 64 32 $shibaBones 1.5 1.5
# Collar: same skeleton, only the collar ring on the head bone's neck.
$collarBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 7.5, 0) }
    @{ name = 'head'; parent = 'body'; pivot = @(0, 9, -5); cubes = @($sCollar) }
)
Write-Geo 'lepton_shiba_collar.geo.json' 'geometry.lepton.shiba.collar' 64 32 $collarBones 1.5 1.5
# Armour: the body, head and legs slightly inflated.
$armorBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 7.5, 0); cubes = @(@{ o = $sBody.o; s = $sBody.s; uv = $sBody.uv; inflate = 0.45 }) }
    @{ name = 'head'; parent = 'body'; pivot = @(0, 9, -5); cubes = @(@{ o = $sHead.o; s = $sHead.s; uv = $sHead.uv; inflate = 0.45 }) }
    @{ name = 'leg0'; parent = 'root'; pivot = @(-2, 5, -3); cubes = @(@{ o = @(-3, 0, -4); s = $sLeg.s; uv = $sLeg.uv; inflate = 0.4 }) }
    @{ name = 'leg1'; parent = 'root'; pivot = @(2, 5, -3); cubes = @(@{ o = @(1, 0, -4); s = $sLeg.s; uv = $sLeg.uv; inflate = 0.4; mirror = $true }) }
    @{ name = 'leg2'; parent = 'root'; pivot = @(-2, 5, 3); cubes = @(@{ o = @(-3, 0, 2); s = $sLeg.s; uv = $sLeg.uv; inflate = 0.4 }) }
    @{ name = 'leg3'; parent = 'root'; pivot = @(2, 5, 3); cubes = @(@{ o = @(1, 0, 2); s = $sLeg.s; uv = $sLeg.uv; inflate = 0.4; mirror = $true }) }
)
Write-Geo 'lepton_shiba_armor.geo.json' 'geometry.lepton.shiba.armor' 64 32 $armorBones 1.5 1.5

function Paint-Shiba([string]$File, [object[]]$Coat, [object[]]$Cream, [int]$Seed, [scriptblock]$Extra = $null) {
    $c = New-Canvas 64 32 $Seed
    Paint-Cube $c $sBody $Coat
    Paint-Cube $c $sHead $Coat
    Paint-Cube $c $sSnout $Cream
    Paint-Cube $c $sEarL $Coat
    Paint-Cube $c $sTail $Coat (@($Cream[0], $Cream[1]))
    Paint-Cube $c $sLeg $Coat
    # cream belly (bottom of the body), cream cheeks and chest, white leg tips (socks)
    Fill-Noise $c ($sBody.uv[0] + $sBody.s[2] + $sBody.s[0]) $sBody.uv[1] $sBody.s[0] $sBody.s[2] $Cream 0
    $f = Get-Front $sHead
    for ($y = 3; $y -lt 6; $y++) { foreach ($x in 0, 1, 4, 5) { Set-Px $c ($f[0] + $x) ($f[1] + $y) (Get-RampColor $Cream 0.6) } }
    Set-Px $c ($f[0] + 1) ($f[1] + 2) '#1b1410'; Set-Px $c ($f[0] + 4) ($f[1] + 2) '#1b1410'   # eyes
    Set-Px $c ($f[0] + 1) ($f[1] + 1) (Get-RampColor $Cream 0.8); Set-Px $c ($f[0] + 4) ($f[1] + 1) (Get-RampColor $Cream 0.8) # brows
    $sf = Get-Front $sSnout
    Set-Px $c ($sf[0] + 1) ($sf[1]) '#151010'                                             # nose
    $ef = Get-Front $sEarL
    Set-Px $c ($ef[0]) ($ef[1] + 1) '#e9a8a0'; Set-Px $c ($ef[0] + 1) ($ef[1] + 1) '#e9a8a0'
    $bf = Get-Front $sBody
    for ($y = 1; $y -lt 5; $y++) { for ($x = 1; $x -lt 5; $x++) { Set-Px $c ($bf[0] + $x) ($bf[1] + $y) (Get-RampColor $Cream 0.5) } }
    for ($x = 0; $x -lt 8; $x++) { Set-Px $c ($sLeg.uv[0] + $x) ($sLeg.uv[1] + 2 + 5 - 1) (Get-RampColor $Cream 0.7) }
    if ($Extra) { & $Extra $c }
    Save-Canvas $c (Join-Path $EntTex $File)
}
$cream = @('#d9c3a0', '#efe0c4', '#fbf3e3')
Paint-Shiba 'shiba0.png' @('#7a3f1c', '#b0632f', '#d48a4c') $cream 2200
Paint-Shiba 'shiba1.png' @('#8a5530', '#c27c43', '#e0a466') $cream 2201
Paint-Shiba 'shiba2.png' @('#1e1a1a', '#3a3230', '#5a4c46') $cream 2202
Paint-Shiba 'shiba_rare.png' @('#bdbdbd', '#e2e2e2', '#ffffff') @('#d6d0c6', '#ebe5da', '#ffffff') 2203
Paint-Shiba 'shiba_doge.png' @('#a8743a', '#d4a050', '#f0c878') $cream 2204 {
    param($c)
    $f = Get-Front $sHead   # raised eyebrows of a famous meme dog
    foreach ($x in 0, 1, 4, 5) { Set-Px $c ($f[0] + $x) ($f[1] + 1) '#7a5426' }
}
Paint-Shiba 'shiba_rad.png' @('#5a2a6a', '#8a3fa0', '#b56ad0') @('#36e0d0', '#7af0e4', '#c0fff8') 2205 {
    param($c)
    $f = Get-Front $sHead   # shades
    for ($x = 0; $x -lt 6; $x++) { Set-Px $c ($f[0] + $x) ($f[1] + 2) '#101010' }
}
# Collars, one texture per dye colour (white collar shaded with the dye's colour + a tag).
$dyes = [ordered]@{
    white = '#f9fffe'; orange = '#f9801d'; magenta = '#c74ebd'; light_blue = '#3ab3da'; yellow = '#fed83d'; lime = '#80c71f'
    pink = '#f38baa'; gray = '#474f52'; light_gray = '#9d9d97'; cyan = '#169c9c'; purple = '#8932b8'; blue = '#3c44aa'
    brown = '#835432'; green = '#5e7c16'; red = '#b02e26'; black = '#1d1d21'
}
foreach ($dye in $dyes.Keys) {
    $c = New-Canvas 64 32 2300
    $base = $dyes[$dye]
    Paint-Cube $c $sCollar @((Shade-Color $base 0.75), $base, (Shade-Color $base 1.15))
    $f = Get-Front $sCollar
    Set-Px $c ($f[0] + 3) ($f[1] + 5) '#e8c040'   # tag
    Save-Canvas $c (Join-Path $EntTex "shiba_collar_$dye.png")
}
# Wolf-armour style plates for the shiba.
$c = New-Canvas 64 32 2310
$plate = @('#5c4a3a', '#8a7058', '#b4977a')
foreach ($cube in $sBody, $sHead, $sLeg) { Paint-Cube $c $cube $plate }
$f = Get-Front $sHead
for ($y = 2; $y -lt 6; $y++) { for ($x = 1; $x -lt 5; $x++) { Set-Px $c ($f[0] + $x) ($f[1] + $y) ([System.Drawing.Color]::Transparent) } }
$t = Get-Top $sBody
for ($y = 0; $y -lt 10; $y += 3) { for ($x = 0; $x -lt 6; $x++) { Set-Px $c ($t[0] + $x) ($t[1] + $y) '#3a2e24' } }
Save-Canvas $c (Join-Path $EntTex 'shiba_armor.png')

# =============================================================================================
# Oretoise (128 x 64). Java size 2 x 1. Faces north.
# =============================================================================================
$tBelly = @{ o = @(-10, 3, -10); s = @(20, 3, 20); uv = @(0, 0) }
$tShell = @{ o = @(-12, 6, -12); s = @(24, 8, 24); uv = @(0, 24) }
$tHead = @{ o = @(-3, 4, -18); s = @(6, 6, 6); uv = @(80, 0) }
$tLeg = @{ s = @(6, 6, 6); uv = @(80, 12) }
$tBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 6, 0); cubes = @($tBelly, $tShell) }
    @{ name = 'head'; parent = 'body'; pivot = @(0, 7, -12); cubes = @($tHead) }
    @{ name = 'leg0'; parent = 'root'; pivot = @(-8, 6, -8); cubes = @(@{ o = @(-11, 0, -11); s = $tLeg.s; uv = $tLeg.uv }) }
    @{ name = 'leg1'; parent = 'root'; pivot = @(8, 6, -8); cubes = @(@{ o = @(5, 0, -11); s = $tLeg.s; uv = $tLeg.uv; mirror = $true }) }
    @{ name = 'leg2'; parent = 'root'; pivot = @(-8, 6, 8); cubes = @(@{ o = @(-11, 0, 5); s = $tLeg.s; uv = $tLeg.uv }) }
    @{ name = 'leg3'; parent = 'root'; pivot = @(8, 6, 8); cubes = @(@{ o = @(5, 0, 5); s = $tLeg.s; uv = $tLeg.uv; mirror = $true }) }
)
Write-Geo 'lepton_toretoise.geo.json' 'geometry.lepton.toretoise' 128 64 $tBones 3 2
# Ore crystals on the shell (0.4 blocks = 6.4 px high), on the body bone.
$ores = @(
    @{ o = @(-8, 14, -7); s = @(5, 5, 5); uv = @(0, 0) }
    @{ o = @(2, 14, -9); s = @(4, 6, 4); uv = @(20, 0) }
    @{ o = @(-4, 14, 3); s = @(6, 4, 6); uv = @(40, 0) }
    @{ o = @(5, 14, 4); s = @(3, 4, 3); uv = @(64, 0) }
    @{ o = @(-10, 14, 6); s = @(3, 3, 3); uv = @(80, 0) }
)
$oreBones = @(
    @{ name = 'root'; pivot = @(0, 0, 0) }
    @{ name = 'body'; parent = 'root'; pivot = @(0, 6, 0); cubes = $ores }
)
Write-Geo 'lepton_toretoise_ore.geo.json' 'geometry.lepton.toretoise.ore' 128 64 $oreBones 3 2.5

$c = New-Canvas 128 64 2400
$skin = @('#2e3a26', '#4a5a38', '#6c7c50')
$shell = @('#2a2622', '#45403a', '#625a50', '#7c7266')
Paint-Cube $c $tBelly $skin
Paint-Cube $c $tShell $shell @('#3a3a3e', '#56565c', '#76767e', '#93939a')   # stony top
Paint-Cube $c $tHead $skin
Paint-Cube $c $tLeg $skin
$f = Get-Front $tHead
Set-Px $c ($f[0] + 1) ($f[1] + 2) '#0e0e0e'; Set-Px $c ($f[0] + 4) ($f[1] + 2) '#0e0e0e'
Set-Px $c ($f[0] + 1) ($f[1] + 1) '#d8d8c0'; Set-Px $c ($f[0] + 4) ($f[1] + 1) '#d8d8c0'
for ($x = 1; $x -lt 5; $x++) { Set-Px $c ($f[0] + $x) ($f[1] + 4) '#1e2618' }
# shell plates: dark seams on the top in a 3x3 grid
$t = Get-Top $tShell
for ($i = 0; $i -lt 24; $i++) { foreach ($k in 0, 8, 16, 23) { Set-Px $c ($t[0] + $i) ($t[1] + $k) '#2a2a2e'; Set-Px $c ($t[0] + $k) ($t[1] + $i) '#2a2a2e' } }
Save-Canvas $c (Join-Path $EntTex 'toretoise.png')
# One ore texture per type: deepslate-like stone with ore specks.
$oreColours = @{
    1 = @('#151515', '#2c2c2c', '#444444')   # coal
    2 = @('#a8805c', '#d8af93', '#efd2b8')   # iron (raw)
    3 = @('#6e0000', '#b01010', '#ff3030')   # redstone
    4 = @('#11307a', '#1d4fbf', '#4a80e8')   # lapis
    5 = @('#7a4027', '#c0673f', '#e88e5c')   # copper
}
$deep = @('#26262c', '#3c3c44', '#53535c')
foreach ($k in 1..5) {
    $c = New-Canvas 128 64 (2410 + $k)
    foreach ($cube in $ores) {
        Paint-Cube $c $cube $deep
        # specks on every face
        $u = $cube.uv[0]; $v = $cube.uv[1]; $w = 2 * ($cube.s[0] + $cube.s[2]); $h = $cube.s[1] + $cube.s[2]
        for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) {
                if ($c.Rng.NextDouble() -lt 0.3) {
                    $px = $c.Bmp.GetPixel($u + $x, $v + $y)
                    if ($px.A -gt 0) { Set-Px $c ($u + $x) ($v + $y) (Get-RampColor $oreColours[$k] $c.Rng.NextDouble()) }
                }
            } }
    }
    Save-Canvas $c (Join-Path $EntTex "toretoise_ore$k.png")
}

# =============================================================================================
# Item icons (16 x 16)
# =============================================================================================
$leg = @(
    '................',
    '................',
    '..........oo....',
    '.........oRRo...',
    '........oRhRo...',
    '.......oRhRo....',
    '......oRhRo.....',
    '.....oRhRo......',
    '....owwRo.......',
    '...owwwo........',
    '..owwwwo........',
    '..owwwo.........',
    '...ooo..........',
    '................',
    '................',
    '................')
Save-PixelArt -Rows $leg -Palette @{ o = '#3a0f06'; R = '#c4502a'; h = '#e98454'; w = '#f2dcd0' } -Path (Join-Path $ItemTex 'crab_leg.png')
Save-PixelArt -Rows $leg -Palette @{ o = '#2e0c04'; R = '#9a3a18'; h = '#c86a3a'; w = '#f0c890' } -Path (Join-Path $ItemTex 'cooked_crab_leg.png')
$shellIcon = @(
    '................',
    '................',
    '....oooooooo....',
    '...oRRhRRhRRo...',
    '..oRhRRRRRRhRo..',
    '.oRRRRhRRhRRRRo.',
    '.oRhRRRRRRRRhRo.',
    '.oRRRRRRRRRRRRo.',
    '.oRRhRRRRRRhRRo.',
    '..oRRRRRRRRRRo..',
    '..oddRRRRRRddo..',
    '...oddddddddo...',
    '....oooooooo....',
    '................',
    '................',
    '................')
Save-PixelArt -Rows $shellIcon -Palette @{ o = '#3a0f06'; R = '#b0462a'; h = '#e98454'; d = '#7a2a14' } -Path (Join-Path $ItemTex 'crab_shell.png')
$bucket = @(
    '................',
    '................',
    '...oooooooooo...',
    '..oWbbbbbbbbWo..',
    '..oWbRRhRRbbWo..',
    '..oWbRhRRRRbWo..',
    '...oWbbRRbbWo...',
    '...oWWbbbbWWo...',
    '...oWWWWWWWWo...',
    '....oWWWWWWo....',
    '....oWWWWWWo....',
    '....oWWWWWWo....',
    '.....oooooo.....',
    '................',
    '................',
    '................')
Save-PixelArt -Rows $bucket -Palette @{ o = '#2a2a2a'; W = '#b8b8b8'; b = '#3a68c8'; R = '#c4502a'; h = '#e98454' } -Path (Join-Path $ItemTex 'crab_bucket.png')
$bottle = @(
    '................',
    '......oooo......',
    '......occo......',
    '.......oo.......',
    '......oggo......',
    '.....oggggo.....',
    '....oLLLLLLo....',
    '...oLsLLLLLLo...',
    '...oLsLLLLLLo...',
    '...oLLLLLLLLo...',
    '...oLLLLLLLLo...',
    '....oLLLLLLo....',
    '.....oooooo.....',
    '................',
    '................',
    '................')
$liquid = @{ o = '#2a2a3a'; c = '#8a5a30'; g = '#d8e8f080'; L = '#6e2410'; s = '#b0502a' }
Save-PixelArt -Rows $bottle -Palette $liquid -Path (Join-Path $ItemTex 'resilience_potion.png')
$splash = @($bottle)
$splash[2] = '......oooo......'; $splash[1] = '.....o....o.....'
Save-PixelArt -Rows $splash -Palette $liquid -Path (Join-Path $ItemTex 'resilience_splash_potion.png')
$lingering = for ($i = 0; $i -lt 16; $i++) { $bottle[$i] }
$lingering[1] = '.....oooooo.....'; $lingering[2] = '.....occcco.....'
Save-PixelArt -Rows $lingering -Palette $liquid -Path (Join-Path $ItemTex 'resilience_lingering_potion.png')

Write-Host 'mobsoverworld: geometry and textures written'
