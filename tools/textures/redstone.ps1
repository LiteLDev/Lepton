# U10 redstone textures: original pixel art (procedural + hand-drawn rows), deterministic.
#
#   pwsh -NoProfile -File tools/textures/redstone.ps1      # also run by tools/pack/redstone.ps1
#
# Writes into assets/pack/rp/textures/{blocks,items,entity}. Returns the written paths.
. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$items = Join-Path $root 'assets/pack/rp/textures/items'
$written = [System.Collections.Generic.List[string]]::new()

# Draws a w x h image from a scriptblock (x, y) -> colour ('#rrggbb', '#aarrggbb', $null = clear).
function Save-Pixels([int]$W, [int]$H, [scriptblock]$Pixel, [string]$Path) {
    $bmp = [System.Drawing.Bitmap]::new($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt $H; $y++) {
            for ($x = 0; $x -lt $W; $x++) {
                $c = & $Pixel $x $y
                $bmp.SetPixel($x, $y, (ConvertTo-Color $c))
            }
        }
        New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
    $written.Add($Path)
}

function Get-Noise([int]$Seed, [int]$Cell = 4, [int]$Octaves = 3) { return , (New-NoiseGrid -Seed $Seed -Size 16 -Cell $Cell -Octaves $Octaves) }

# ---- gravisand: pale sand with violet ender grains -------------------------------------------
$sand = New-Ramp '#b7ac7e', '#d6cc9c', '#ece4bd' 5
$grains = @{}
$rng = [System.Random]::new(1001)
for ($i = 0; $i -lt 22; $i++) { $grains["$($rng.Next(16)),$($rng.Next(16))"] = $rng.Next(3) }
$gn = Get-Noise 1002 4 3
Save-Pixels 16 16 {
    param($x, $y)
    $k = "$x,$y"
    if ($grains.ContainsKey($k)) { return @('#4b2a6b', '#6d3d99', '#2f8f83')[$grains[$k]] }
    $t = $gn[$x, $y]
    return Get-RampColor $sand ([Math]::Min(1.0, $t * 1.25))
} (Join-Path $blocks 'gravisand.png')

# ---- iron rod: a 2 px bar (columns 7-8) with rivet bands; the rest is clear -------------------
Save-Pixels 16 16 {
    param($x, $y)
    if ($x -ne 7 -and $x -ne 8) { return $null }
    $band = ($y % 5) -eq 2
    if ($x -eq 7) { if ($band) { return '#9a9a9a' } return @('#f2f2f2', '#e2e2e2', '#d8d8d8')[$y % 3] }
    if ($band) { return '#6e6e6e' }
    return @('#b4b4b4', '#a8a8a8', '#bcbcbc')[$y % 3]
} (Join-Path $blocks 'iron_rod.png')

# ---- ender watcher: an obsidian-like frame around an eye (closed / open) ----------------------
$obs = New-Ramp '#0f0b16', '#1f1630', '#3a2a57' 4
$on = Get-Noise 1003 4 3
function Get-WatcherPixel([int]$x, [int]$y, [bool]$Open) {
    $base = Get-RampColor $obs $on[$x, $y]
    if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { return '#09070d' }
    if ($x -eq 1 -or $y -eq 1 -or $x -eq 14 -or $y -eq 14) { return '#5b4a7a' }
    # the eye: an almond 10 x 6 centred, iris 4 x 4, pupil 2 x 2
    $dx = [Math]::Abs($x - 7.5); $dy = [Math]::Abs($y - 7.5)
    $inEye = ($dx / 5.2) * ($dx / 5.2) + ($dy / 3.2) * ($dy / 3.2) -le 1.0
    if (-not $inEye) { return $base }
    $edge = ($dx / 5.2) * ($dx / 5.2) + ($dy / 3.2) * ($dy / 3.2) -gt 0.62
    if (-not $Open) {
        if ($dy -lt 1) { return '#1c4f3a' }
        return $(if ($edge) { '#2a1f3d' } else { '#3c2c58' })
    }
    if ($dx -le 1 -and $dy -le 1) { return '#05140d' }
    if ($dx -le 2 -and $dy -le 2) { return $(if ($x + $y -lt 15) { '#7ff5b8' } else { '#2fcf86' }) }
    if ($edge) { return '#1d7a52' }
    return '#c9f7df'
}
Save-Pixels 16 16 { param($x, $y) Get-WatcherPixel $x $y $false } (Join-Path $blocks 'ender_watcher.png')
Save-Pixels 16 16 { param($x, $y) Get-WatcherPixel $x $y $true } (Join-Path $blocks 'ender_watcher_on.png')

# ---- redstone randomizer -------------------------------------------------------------------
# Top textures per state and facing (the face is mapped in world coordinates, so each facing has
# its own rotation). Base layout = Java FACING south: input (back torch) at the south edge, left
# output at the west edge, right output at the east edge, a prismarine crystal in the middle.
$stone = New-Ramp '#8f8f8f', '#a6a6a6', '#b8b8b8' 4
$sn = Get-Noise 1004 4 2
function Get-RandomizerTop([int]$x, [int]$y, [string]$State) {
    $lit = '#ff3a2a'; $dim = '#5e0b06'
    # crystal 6..9
    if ($x -ge 6 -and $x -le 9 -and $y -ge 6 -and $y -le 9) {
        if ($x -eq 6 -or $y -eq 6) { return '#9fe0d0' }
        if ($x -eq 9 -or $y -eq 9) { return '#3c7f70' }
        return '#62b3a0'
    }
    $back = ($x -ge 7 -and $x -le 8 -and $y -ge 10)                 # input trace (south)
    $left = ($y -ge 7 -and $y -le 8 -and $x -le 5)                  # west trace
    $right = ($y -ge 7 -and $y -le 8 -and $x -ge 10)                # east trace
    if ($back) { return $(if ($State -ne 'off') { $lit } else { $dim }) }
    if ($left) { return $(if ($State -eq 'left') { $lit } else { $dim }) }
    if ($right) { return $(if ($State -eq 'right') { $lit } else { $dim }) }
    if ($x -eq 0 -or $y -eq 0) { return '#c4c4c4' }
    if ($x -eq 15 -or $y -eq 15) { return '#7a7a7a' }
    return Get-RampColor $stone $sn[$x, $y]
}
# rotate base (FACING south) coordinates to FACING f: south identity, north 180, east, west
function Get-BaseCoords([int]$x, [int]$y, [string]$Facing) {
    switch ($Facing) {
        'south' { return @($x, $y) }
        'north' { return @((15 - $x), (15 - $y)) }
        # FACING east: base south edge (y=15) -> east edge (x=15); base west (x=0) -> south (y=15)
        'east' { return @((15 - $y), $x) }
        'west' { return @($y, (15 - $x)) }
    }
}
foreach ($state in 'off', 'left', 'right') {
    foreach ($f in 'north', 'south', 'east', 'west') {
        Save-Pixels 16 16 {
            param($x, $y)
            $b = Get-BaseCoords $x $y $f
            Get-RandomizerTop $b[0] $b[1] $state
        } (Join-Path $blocks "redstone_randomizer_${state}_$f.png")
    }
}
# item icon: the diode seen from above at an angle, like the repeater item
$icon = @(
    '................',
    '................',
    '................',
    '.......tt.......',
    '.......ff.......',
    '..tt...ss...tt..',
    '..ff...ss...ff..',
    '..ss..cccc..ss..',
    '.hhhhhcCCchhhhh.',
    'hrrrrhcCCchrrrrh',
    'hhhhhhhrrhhhhhhh',
    'gggggggrrggggggg',
    'gGGGGGGGGGGGGGGg',
    'gGGGGGGGGGGGGGGg',
    'kkkkkkkkkkkkkkkk',
    '................'
)
# palette keys are case-insensitive in PowerShell: give the light crystal and light stone own keys
$icon = $icon | ForEach-Object { $_.Replace('C', 'x').Replace('G', 'q') }
$pal = @{ t = '#ffd25c'; f = '#ff3a2a'; s = '#6b4a2b'; c = '#3c7f70'; x = '#9fe0d0'; h = '#b8b8b8'; r = '#8a1a10'; g = '#c4c4c4'; q = '#a6a6a6'; k = '#6e6e6e' }
$iconPath = Join-Path $items 'redstone_randomizer.png'
Save-PixelArt -Rows $icon -Palette $pal -Path $iconPath
$written.Add($iconPath)

return $written
