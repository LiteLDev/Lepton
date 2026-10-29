# Textures of U17 placement (original pixel art): the Trowel and the Hammer.
# Run: pwsh tools/textures/placement.ps1
. "$PSScriptRoot/lib.ps1"

$root  = Resolve-Path "$PSScriptRoot/../.."
$items = Join-Path $root 'assets/pack/rp/textures/items'

# Pads (or cuts) every row to 16 so hand-drawn rows need no exact counting.
function Fit-Rows([string[]]$Rows, [int]$Width = 16, [int]$Height = 16) {
    $out = foreach ($r in $Rows) { if ($r.Length -ge $Width) { $r.Substring(0, $Width) } else { $r + ('.' * ($Width - $r.Length)) } }
    $out = @($out)
    while ($out.Count -lt $Height) { $out += ('.' * $Width) }
    return , $out
}

# Shared tool colours: dark outline, iron ramp, wood ramp.
$palette = @{
    k = '#1e1a17'   # outline
    i = '#d8d8d8'   # iron light
    m = '#a9a9a9'   # iron mid
    s = '#6e6e6e'   # iron shade
    h = '#ffffff'   # iron highlight
    w = '#8f6236'   # wood
    l = '#b98a53'   # wood light
    d = '#5a3a1c'   # wood dark
}

# ---- trowel: a pointed mason's blade on a short neck, wooden handle to the lower left ----------
$trowel = Fit-Rows @(
    '..........kkk',
    '.........khiik',
    '........khiimk',
    '.......khiimmsk',
    '......khiimmssk',
    '.....khiimmsskk',
    '.....kiimmsskk',
    '......kmmsskk',
    '.....kkksskk',
    '....kslkkk',
    '...klwk',
    '..klwdk',
    '.klwdk',
    'klwdk',
    'kdk',
    '.k'
)
Save-PixelArt -Rows $trowel -Palette $palette -Path "$items/trowel.png"

# ---- hammer: an iron block head across the top of a diagonal stick handle ----------------------
# Drawn in a rotated frame: u runs along the head (lower-left to upper-right is the handle, v),
# then outlined.
function New-HammerRows {
    $cx = 9; $cy = 6
    $grid = New-Object 'char[,]' 16, 16
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $grid[$x, $y] = '.' } }
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $u = ($x - $cx) + ($y - $cy)   # along the head
            $v = ($x - $cx) - ($y - $cy)   # along the handle (up-right is positive)
            if ([Math]::Abs($u) -le 6 -and $v -ge -1 -and $v -le 4) {
                $grid[$x, $y] = if ($v -le 0) { 's' } elseif ($u -le -3) { 'h' } elseif ($u -le 1) { 'i' } elseif ($u -le 4) { 'm' } else { 's' }
            } elseif ([Math]::Abs($u) -le 1 -and $v -lt -1 -and $v -ge -14) {
                $grid[$x, $y] = if ($u -lt 0) { 'l' } elseif ($u -eq 0) { 'w' } else { 'd' }
            }
        }
    }
    $out = for ($y = 0; $y -lt 16; $y++) {
        -join (0..15 | ForEach-Object {
            $x = $_
            if ($grid[$x, $y] -ne '.') { return [string]$grid[$x, $y] }
            foreach ($d in @(@(1, 0), @(-1, 0), @(0, 1), @(0, -1))) {
                $nx = $x + $d[0]; $ny = $y + $d[1]
                if ($nx -ge 0 -and $ny -ge 0 -and $nx -lt 16 -and $ny -lt 16 -and $grid[$nx, $ny] -ne '.') { return 'k' }
            }
            return '.'
        })
    }
    return , @($out)
}
Save-PixelArt -Rows (New-HammerRows) -Palette $palette -Path "$items/hammer.png"

Write-Host "placement textures written to $items"
