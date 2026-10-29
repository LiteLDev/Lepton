# U9 variantcontainers textures: chest atlases (one per chest block) and the variant furnaces.
# Original procedural pixel art (seeded noise), drawn here; nothing is traced from game textures.
#
#   pwsh -NoProfile -File tools/textures/variantcontainers.ps1      # (re)draws every texture
#
# tools/pack/variantcontainers.ps1 dot-sources this file: it uses $ChestAtlas (the UV layout the
# chest geometry maps onto), $ChestMaterials and the Draw-* functions.

. "$PSScriptRoot/lib.ps1"

$script:VcRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$script:VcTex = Join-Path $script:VcRoot 'assets/pack/rp/textures/blocks/variantcontainers'

# Atlas layout of a chest texture (64 x 128). Single chest faces are 14 wide; the double chest
# faces are 30-wide composites (both halves side by side, as seen from the front / from above /
# from below; the back composite as seen from behind). Each half of a double chest maps onto one
# 15-wide half of a composite.
$script:ChestAtlas = [ordered]@{
    Width = 64; Height = 128
    S_LID_TOP = @(0, 0, 14, 14); S_LID_UNDER = @(14, 0, 14, 14); S_FLOOR = @(28, 0, 14, 14); S_BOTTOM = @(42, 0, 14, 14)
    S_LID_FRONT = @(0, 14, 14, 4); S_LID_BACK = @(14, 14, 14, 4); S_LID_SIDE = @(28, 14, 14, 4)
    S_BODY_FRONT = @(0, 18, 14, 10); S_BODY_BACK = @(14, 18, 14, 10); S_BODY_SIDE = @(28, 18, 14, 10)
    L_FRONT = @(42, 14, 2, 4); L_SIDE = @(44, 14, 1, 4); L_TOP = @(45, 14, 2, 1); L_BOTTOM = @(45, 15, 2, 1); L_BACK = @(47, 14, 2, 4)
    D_LID_TOP = @(0, 28, 30, 14); D_LID_UNDER = @(30, 28, 30, 14); D_FLOOR = @(0, 42, 30, 14); D_BOTTOM = @(30, 42, 30, 14)
    D_LID_FRONT = @(0, 56, 30, 4); D_LID_BACK = @(30, 56, 30, 4); D_BODY_FRONT = @(0, 60, 30, 10); D_BODY_BACK = @(30, 60, 30, 10)
}

# Chest materials: planks colours (dark .. light) and the surface pattern.
$script:ChestMaterials = [ordered]@{
    oak          = @{ Ramp = @('#4f3a1e', '#7a5c34', '#9c7a48', '#b8945f', '#c9a872'); Kind = 'wood'; Seed = 901 }
    spruce       = @{ Ramp = @('#33230f', '#4f3920', '#694c2c', '#7f5d37', '#94704a'); Kind = 'wood'; Seed = 902 }
    birch        = @{ Ramp = @('#8a7647', '#b29d66', '#c9b37b', '#d8c48c', '#e6d6a6'); Kind = 'wood'; Seed = 903 }
    jungle       = @{ Ramp = @('#58371f', '#80553a', '#9c6c4b', '#b5845f', '#c69a76'); Kind = 'wood'; Seed = 904 }
    acacia       = @{ Ramp = @('#5c2c13', '#8a4424', '#a9552e', '#bf6739', '#cf7d4e'); Kind = 'wood'; Seed = 905 }
    dark_oak     = @{ Ramp = @('#22150a', '#36230f', '#4a3018', '#5b3d1f', '#6d4b2a'); Kind = 'wood'; Seed = 906 }
    mangrove     = @{ Ramp = @('#3f1714', '#5f2522', '#76302b', '#8b3c35', '#a04d45'); Kind = 'wood'; Seed = 907 }
    bamboo       = @{ Ramp = @('#7a6a22', '#a8952f', '#c4b03e', '#d6c457', '#e4d57a'); Kind = 'bamboo'; Seed = 908 }
    cherry       = @{ Ramp = @('#8e5a57', '#bf8c87', '#d6a7a1', '#e3bbb4', '#f0d2cc'); Kind = 'wood'; Seed = 909 }
    crimson      = @{ Ramp = @('#33141f', '#4f2132', '#652a42', '#7a3551', '#8f4563'); Kind = 'wood'; Seed = 910 }
    warped       = @{ Ramp = @('#0f302d', '#1a4a45', '#245e58', '#2f746c', '#3d8a80'); Kind = 'wood'; Seed = 911 }
    nether_brick = @{ Ramp = @('#170a0d', '#2a1318', '#3b1c22', '#4b252c', '#5d3139'); Kind = 'brick'; Seed = 912 }
    purpur       = @{ Ramp = @('#5e3f60', '#83608a', '#9c78a0', '#b08fb3', '#c6a9c8'); Kind = 'tile'; Seed = 913 }
    prismarine   = @{ Ramp = @('#21453f', '#326b60', '#45897b', '#5aa292', '#78bcaa'); Kind = 'tile'; Seed = 914 }
}

# ---- canvas -------------------------------------------------------------------------------------

function New-Canvas([int]$W, [int]$H) {
    $bmp = [System.Drawing.Bitmap]::new($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    return [pscustomobject]@{ Bmp = $bmp; W = $W; H = $H }
}
function Set-Px($C, [int]$X, [int]$Y, $Color) {
    if ($X -lt 0 -or $Y -lt 0 -or $X -ge $C.W -or $Y -ge $C.H) { return }
    $C.Bmp.SetPixel($X, $Y, (ConvertTo-Color $Color))
}
function Save-Canvas($C, [string]$Path) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $C.Bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $C.Bmp.Dispose()
}

# Deterministic per-pixel hash noise in 0..1.
function Get-Hash([int]$Seed, [int]$X, [int]$Y) {
    [long]$h = ($Seed * 374761393L + $X * 668265263L + $Y * 2147483647L) -band 0xFFFFFFFFL
    $h = (($h -bxor ($h -shr 13)) * 1274126177L) -band 0xFFFFFFFFL
    $h = $h -bxor ($h -shr 16)
    return ($h -band 0xFFFF) / 65535.0
}

# ---- surface patterns ---------------------------------------------------------------------------

# The surface colour of material $M at surface pixel (x, y) (y grows downwards on the face).
# $Vertical: boards run vertically (top / bottom faces) instead of horizontally.
function Get-SurfaceColor($M, [int]$X, [int]$Y, [bool]$Vertical, [int]$Salt) {
    $r = $M.Ramp; $s = $M.Seed + $Salt
    switch ($M.Kind) {
        'wood' {
            $a = if ($Vertical) { $X } else { $Y }; $b = if ($Vertical) { $Y } else { $X }
            $board = [Math]::Floor($a / 4)
            if (($a % 4) -eq 3) { return $r[1] }                                  # seam between boards
            $n = Get-Hash $s ([int]($b / 3) + $board * 17) $board                  # long grain streaks
            $fine = Get-Hash ($s + 7) $b $a
            $t = 0.35 + 0.45 * $n + 0.2 * $fine
            if ($t -lt 0.45) { return $r[2] }
            if ($t -lt 0.8) { return $r[3] }
            return $r[4]
        }
        'bamboo' {
            $a = if ($Vertical) { $Y } else { $X }; $b = if ($Vertical) { $X } else { $Y }
            if (($a % 4) -eq 3) { return $r[1] }                                   # cane gaps
            if ((($b + [Math]::Floor($a / 4) * 3) % 7) -eq 0) { return $r[2] }     # nodes
            $n = Get-Hash $s $a $b
            if ($n -lt 0.3) { return $r[2] }
            if ($n -lt 0.85) { return $r[3] }
            return $r[4]
        }
        'brick' {
            $row = [Math]::Floor($Y / 4); $off = if ($row % 2) { 4 } else { 0 }
            if (($Y % 4) -eq 3 -or ((($X + $off) % 8) -eq 7)) { return $r[0] }    # mortar
            $n = Get-Hash $s $X $Y
            if ($n -lt 0.25) { return $r[1] }
            if ($n -lt 0.8) { return $r[2] }
            return $r[3]
        }
        default {
            # square tiles (purpur / prismarine): 4x4 tiles with light top-left edges
            $tx = $X % 4; $ty = $Y % 4
            if ($tx -eq 3 -or $ty -eq 3) { return $r[1] }
            $n = Get-Hash $s $X $Y
            if ($tx -eq 0 -or $ty -eq 0) { return $r[4] }
            if ($n -lt 0.35) { return $r[2] }
            return $r[3]
        }
    }
}

# Fills atlas region $Rect = @(x, y, w, h). $Frame: which edges get the dark outline
# (any of 'left','right','top','bottom'). $Shade: brightness factor (interior faces are darker).
function Fill-Face($C, $M, $Rect, [string[]]$Frame = @('left', 'right', 'top', 'bottom'), [bool]$Vertical = $false,
    [double]$Shade = 1.0, [int]$Salt = 0) {
    $x0, $y0, $w, $h = $Rect
    $outline = $M.Ramp[0]
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $edge = ($Frame -contains 'left' -and $x -eq 0) -or ($Frame -contains 'right' -and $x -eq $w - 1) -or
            ($Frame -contains 'top' -and $y -eq 0) -or ($Frame -contains 'bottom' -and $y -eq $h - 1)
            $col = if ($edge) { $outline } else { Get-SurfaceColor $M $x $y $Vertical $Salt }
            if ($Shade -ne 1.0) { $col = Shade-Color $col $Shade }
            Set-Px $C ($x0 + $x) ($y0 + $y) $col
        }
    }
}

# Interior faces (the floor inside the chest, the underside of the lid): dark with a lighter rim.
function Fill-Inside($C, $M, $Rect, [string[]]$Rim = @('left', 'right', 'top', 'bottom'), [int]$Salt = 0) {
    $x0, $y0, $w, $h = $Rect
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $rim = ($Rim -contains 'left' -and $x -eq 0) -or ($Rim -contains 'right' -and $x -eq $w - 1) -or
            ($Rim -contains 'top' -and $y -eq 0) -or ($Rim -contains 'bottom' -and $y -eq $h - 1)
            $col = if ($rim) { $M.Ramp[1] } else { Shade-Color (Get-SurfaceColor $M $x $y $true $Salt) 0.45 }
            Set-Px $C ($x0 + $x) ($y0 + $y) $col
        }
    }
}

# The latch: iron (regular chests) or iron with a redstone-red core (trapped chests).
function Draw-Latch($C, [bool]$Trapped) {
    $A = $script:ChestAtlas
    $light = '#e0e0e0'; $mid = '#a8a8a8'; $dark = '#5a5a5a'
    $core = if ($Trapped) { '#c42a1d' } else { $light }
    $coreDark = if ($Trapped) { '#7a140d' } else { $mid }
    # front 2x4: outline + core
    $fx = $A.L_FRONT[0]; $fy = $A.L_FRONT[1]
    foreach ($p in @(@(0, 0, $dark), @(1, 0, $dark), @(0, 1, $core), @(1, 1, $core), @(0, 2, $coreDark), @(1, 2, $core), @(0, 3, $dark), @(1, 3, $dark))) {
        Set-Px $C ($fx + $p[0]) ($fy + $p[1]) $p[2]
    }
    $sx = $A.L_SIDE[0]; $sy = $A.L_SIDE[1]
    for ($y = 0; $y -lt 4; $y++) { Set-Px $C $sx ($sy + $y) $(if ($y -eq 0 -or $y -eq 3) { $dark } else { $mid }) }
    $tx = $A.L_TOP[0]; $ty = $A.L_TOP[1]; Set-Px $C $tx $ty $mid; Set-Px $C ($tx + 1) $ty $mid
    $bx = $A.L_BOTTOM[0]; $by = $A.L_BOTTOM[1]; Set-Px $C $bx $by $dark; Set-Px $C ($bx + 1) $by $dark
    $kx = $A.L_BACK[0]; $ky = $A.L_BACK[1]
    for ($y = 0; $y -lt 4; $y++) { for ($x = 0; $x -lt 2; $x++) { Set-Px $C ($kx + $x) ($ky + $y) $mid } }
}

# A trapped chest has a thin redstone line under the lid's front edge (both chest kinds share the
# rest of the atlas, so the trap is visible from the front).
function Draw-TrapLine($C, $Rect) {
    $x0, $y0, $w, $h = $Rect
    for ($x = 1; $x -lt $w - 1; $x++) { if (($x % 3) -ne 2) { Set-Px $C ($x0 + $x) ($y0 + $h - 2) '#a3241a' } }
}

# Draws the full chest atlas of material $Name to $Path.
function Draw-ChestAtlas([string]$Name, [bool]$Trapped, [string]$Path) {
    $M = $script:ChestMaterials[$Name]
    $A = $script:ChestAtlas
    $C = New-Canvas $A.Width $A.Height
    # single chest
    Fill-Face $C $M $A.S_LID_TOP -Vertical $true
    Fill-Inside $C $M $A.S_LID_UNDER -Salt 3
    Fill-Inside $C $M $A.S_FLOOR -Salt 5
    Fill-Face $C $M $A.S_BOTTOM -Vertical $true -Shade 0.8 -Salt 9
    Fill-Face $C $M $A.S_LID_FRONT -Salt 1
    Fill-Face $C $M $A.S_LID_BACK -Salt 2
    Fill-Face $C $M $A.S_LID_SIDE -Salt 4
    Fill-Face $C $M $A.S_BODY_FRONT -Salt 6
    Fill-Face $C $M $A.S_BODY_BACK -Salt 7
    Fill-Face $C $M $A.S_BODY_SIDE -Salt 8
    # double chest composites (outline only on the outer edges)
    Fill-Face $C $M $A.D_LID_TOP -Vertical $true -Salt 11
    Fill-Inside $C $M $A.D_LID_UNDER -Salt 13
    Fill-Inside $C $M $A.D_FLOOR -Salt 15
    Fill-Face $C $M $A.D_BOTTOM -Vertical $true -Shade 0.8 -Salt 19
    Fill-Face $C $M $A.D_LID_FRONT -Salt 21
    Fill-Face $C $M $A.D_LID_BACK -Salt 22
    Fill-Face $C $M $A.D_BODY_FRONT -Salt 26
    Fill-Face $C $M $A.D_BODY_BACK -Salt 27
    # the seam in the middle of the double composites: a darker joint line on both halves
    foreach ($key in 'D_LID_TOP', 'D_FLOOR', 'D_BOTTOM', 'D_LID_FRONT', 'D_LID_BACK', 'D_BODY_FRONT', 'D_BODY_BACK') {
        $x0, $y0, $w, $h = $A[$key]
        for ($y = 0; $y -lt $h; $y++) {
            foreach ($x in 14, 15) {
                $p = $C.Bmp.GetPixel($x0 + $x, $y0 + $y)
                Set-Px $C ($x0 + $x) ($y0 + $y) (Shade-Color $p 0.82)
            }
        }
    }
    if ($Trapped) {
        Draw-TrapLine $C $A.S_LID_FRONT
        Draw-TrapLine $C $A.D_LID_FRONT
    }
    Draw-Latch $C $Trapped
    Save-Canvas $C $Path
}

# ---- furnaces -----------------------------------------------------------------------------------

$script:FurnaceMaterials = [ordered]@{
    deepslate  = @{ Ramp = @('#1f1f24', '#34343b', '#47474f', '#5a5a63', '#6e6e78'); Seed = 951 }
    blackstone = @{ Ramp = @('#120e13', '#221c23', '#2f2831', '#3c343e', '#4c434f'); Seed = 952 }
}

function Get-StonePx($M, [int]$X, [int]$Y, [int]$Salt) {
    $n = Get-Hash ($M.Seed + $Salt) ([int]($X / 2)) ([int]($Y / 2))
    $f = Get-Hash ($M.Seed + $Salt + 31) $X $Y
    $t = 0.6 * $n + 0.4 * $f
    if ($t -lt 0.25) { return $M.Ramp[1] }
    if ($t -lt 0.55) { return $M.Ramp[2] }
    if ($t -lt 0.85) { return $M.Ramp[3] }
    return $M.Ramp[4]
}

# Cobbled frame used by the furnace's sides and front: a raised 1px rim plus a mid band.
function Draw-FurnaceBase($C, $M, [int]$Salt) {
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $col = Get-StonePx $M $x $y $Salt
            if ($x -eq 0 -or $y -eq 0) { $col = $M.Ramp[4] }
            elseif ($x -eq 15 -or $y -eq 15) { $col = $M.Ramp[0] }
            Set-Px $C $x $y $col
        }
    }
}

function Draw-FurnaceSide([string]$Name, [string]$Path) {
    $M = $script:FurnaceMaterials[$Name]; $C = New-Canvas 16 16
    Draw-FurnaceBase $C $M 1
    for ($x = 1; $x -lt 15; $x++) { Set-Px $C $x 4 $M.Ramp[0]; Set-Px $C $x 5 $M.Ramp[4] }   # a lip under the top slab
    Save-Canvas $C $Path
}

function Draw-FurnaceTop([string]$Name, [string]$Path) {
    $M = $script:FurnaceMaterials[$Name]; $C = New-Canvas 16 16
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $col = Get-StonePx $M $x $y 2
            $inner = $x -ge 2 -and $x -le 13 -and $y -ge 2 -and $y -le 13
            if (-not $inner) { $col = Shade-Color $col 1.12 }
            if (($x -eq 2 -or $x -eq 13) -and $y -ge 2 -and $y -le 13) { $col = $M.Ramp[0] }
            if (($y -eq 2 -or $y -eq 13) -and $x -ge 2 -and $x -le 13) { $col = $M.Ramp[0] }
            Set-Px $C $x $y $col
        }
    }
    Save-Canvas $C $Path
}

# Front: the cobbled base with a mouth (rows 8..13, cols 3..12). $Fire: none | fire | soul.
function Draw-FurnaceFront([string]$Name, [string]$Fire, [string]$Path) {
    $M = $script:FurnaceMaterials[$Name]; $C = New-Canvas 16 16
    Draw-FurnaceBase $C $M 3
    for ($x = 1; $x -lt 15; $x++) { Set-Px $C $x 4 $M.Ramp[0]; Set-Px $C $x 5 $M.Ramp[4] }
    # mouth frame
    for ($x = 3; $x -le 12; $x++) { Set-Px $C $x 7 $M.Ramp[0]; Set-Px $C $x 14 $M.Ramp[4] }
    for ($y = 7; $y -le 14; $y++) { Set-Px $C 2 $y $M.Ramp[0]; Set-Px $C 13 $y $M.Ramp[4] }
    $flame = switch ($Fire) {
        'fire' { @('#fff3a0', '#ffc02e', '#ff7a12', '#c2410c', '#5c1d06') }
        'soul' { @('#e0ffff', '#7ff3f5', '#2ec6d6', '#127c94', '#0a3540') }
        default { $null }
    }
    for ($y = 8; $y -le 13; $y++) {
        for ($x = 3; $x -le 12; $x++) {
            if (-not $flame) {
                $col = if ($y -eq 13) { '#1a1414' } else { '#0b0909' }
                if ($y -eq 10 -and ($x % 2) -eq 1) { $col = '#2a2626' }               # grate bars
                Set-Px $C $x $y $col
                continue
            }
            # flames rise from the bottom of the mouth; tongues shaped by a seeded height per column
            $hgt = 2 + [int](4 * (Get-Hash ($M.Seed + 77) $x 0))
            $fromBottom = 13 - $y
            if ($fromBottom -gt $hgt) { Set-Px $C $x $y '#0b0909'; continue }
            $t = $fromBottom / [Math]::Max(1, $hgt)
            $idx = [Math]::Min(4, [int](4 * $t))
            if ($y -eq 13) { $idx = [Math]::Max($idx, 1) }
            if ($y -eq 10 -and ($x % 2) -eq 1) { $idx = 4 }
            Set-Px $C $x $y $flame[$idx]
        }
    }
    Save-Canvas $C $Path
}

function Invoke-VariantContainerTextures {
    New-Item -ItemType Directory -Force $script:VcTex | Out-Null
    $written = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $script:ChestMaterials.Keys) {
        foreach ($trapped in $false, $true) {
            $file = if ($trapped) { "trapped_${name}_chest.png" } else { "${name}_chest.png" }
            $p = Join-Path $script:VcTex $file
            Draw-ChestAtlas $name $trapped $p
            $written.Add($p)
        }
    }
    foreach ($name in $script:FurnaceMaterials.Keys) {
        $p = Join-Path $script:VcTex "${name}_furnace_side.png"; Draw-FurnaceSide $name $p; $written.Add($p)
        $p = Join-Path $script:VcTex "${name}_furnace_top.png"; Draw-FurnaceTop $name $p; $written.Add($p)
        $p = Join-Path $script:VcTex "${name}_furnace_front.png"; Draw-FurnaceFront $name 'none' $p; $written.Add($p)
        $p = Join-Path $script:VcTex "${name}_furnace_front_on.png"; Draw-FurnaceFront $name 'fire' $p; $written.Add($p)
        if ($name -eq 'blackstone') {
            $p = Join-Path $script:VcTex "${name}_furnace_front_on_soul.png"; Draw-FurnaceFront $name 'soul' $p; $written.Add($p)
        }
    }
    return $written
}

if ($MyInvocation.InvocationName -ne '.') {
    $files = Invoke-VariantContainerTextures
    Write-Host "[textures] variantcontainers: $($files.Count) files"
}
