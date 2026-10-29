# U19 emotes: the emote icons (original pixel art, 16x16).
#
#   pwsh tools/textures/emotes.ps1
#
# Writes assets/pack/rp/textures/lepton/emotes/<id>.png (form buttons for vanilla clients) and
# src/client/emotes/EmoteIcons.inc (the same pixels for the client mod's chat-screen buttons and
# HUD, drawn natively). Each icon is a small figure (or face) drawn with lines from a few points,
# posed like its emote.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$outDir = Join-Path $root 'assets/pack/rp/textures/lepton/emotes'

$palette = [ordered]@{
    o = '#1d1a22'   # outline
    s = '#e0a77c'   # skin
    d = '#b97c55'   # skin shade
    h = '#5a3a22'   # hair
    b = '#3f6fc4'   # shirt
    n = '#2b4a8c'   # shirt shade
    l = '#4b3b5c'   # trousers
    w = '#f4f4f4'   # white (marks, eyes)
    y = '#ffd84a'   # yellow (sparks)
    r = '#e0413a'   # red
    t = '#5ec8ff'   # tears, sweat
    g = '#7bb35a'   # zombie skin
    k = '#4d7a39'   # zombie shade
    f = '#ff8a2a'   # flame
    p = '#c06ad8'   # purple
}

function New-Grid { , ([char[, ]]::new(16, 16)) }
function Set-Px($g, [int]$x, [int]$y, [string]$c) { if ($x -ge 0 -and $x -lt 16 -and $y -ge 0 -and $y -lt 16) { $g[$y, $x] = [char]$c } }
function Get-Rows($g) { , @(for ($y = 0; $y -lt 16; $y++) { -join (0..15 | ForEach-Object { if ($g[$y, $_] -eq [char]0) { '.' } else { $g[$y, $_] } }) }) }
function Draw-Line($g, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [string]$c) {
    $dx = [Math]::Abs($x1 - $x0); $dy = -[Math]::Abs($y1 - $y0)
    $sx = if ($x0 -lt $x1) { 1 } else { -1 }; $sy = if ($y0 -lt $y1) { 1 } else { -1 }
    $err = $dx + $dy
    while ($true) {
        Set-Px $g $x0 $y0 $c
        if ($x0 -eq $x1 -and $y0 -eq $y1) { break }
        $e2 = 2 * $err
        if ($e2 -ge $dy) { $err += $dy; $x0 += $sx }
        if ($e2 -le $dx) { $err += $dx; $y0 += $sy }
    }
}
function Draw-Rect($g, [int]$x, [int]$y, [int]$w, [int]$h, [string]$c) { for ($j = 0; $j -lt $h; $j++) { for ($i = 0; $i -lt $w; $i++) { Set-Px $g ($x + $i) ($y + $j) $c } } }

# A standing figure: head at x 6..9, y 1..4; body x 6..9, y 5..10; legs y 11..14.
# Arms are polylines from the shoulders (right arm = viewer's left at x 5, left arm at x 10).
function Draw-Figure($g, [string]$skin = 's', [string]$shade = 'd', [int]$headDx = 0, [switch]$NoLegs, [switch]$KickLeft) {
    Draw-Rect $g 6 5 4 6 'b'; Draw-Rect $g 6 9 4 2 'n'
    if (-not $NoLegs) {
        Draw-Rect $g 6 11 2 4 'l'
        if ($KickLeft) { Draw-Line $g 8 11 11 13 'l'; Draw-Line $g 9 11 12 13 'l' } else { Draw-Rect $g 8 11 2 4 'l' }
    }
    $hx = 6 + $headDx
    Draw-Rect $g $hx 1 4 4 $skin; Draw-Rect $g $hx 1 4 1 'h'; Set-Px $g $hx 2 'h'; Set-Px $g ($hx + 3) 2 'h'
    Set-Px $g ($hx + 1) 3 'o'; Set-Px $g ($hx + 2) 3 'o'; Draw-Rect $g $hx 4 4 1 $shade
}
function Draw-Arm($g, [int[]]$pts, [string]$c = 's') {
    for ($i = 0; $i -lt $pts.Count - 2; $i += 2) { Draw-Line $g $pts[$i] $pts[$i + 1] $pts[$i + 2] $pts[$i + 3] $c }
}
# A big face: head x 3..12, y 2..11.
function Draw-Face($g, [string]$skin = 's', [string]$shade = 'd', [int]$dx = 0, [int]$dy = 0) {
    Draw-Rect $g (3 + $dx) (2 + $dy) 10 10 $skin
    Draw-Rect $g (3 + $dx) (2 + $dy) 10 3 'h'; Set-Px $g (3 + $dx) (5 + $dy) 'h'; Set-Px $g (12 + $dx) (5 + $dy) 'h'
    Draw-Rect $g (5 + $dx) (6 + $dy) 2 2 'w'; Set-Px $g (6 + $dx) (7 + $dy) 'o'
    Draw-Rect $g (9 + $dx) (6 + $dy) 2 2 'w'; Set-Px $g (10 + $dx) (7 + $dy) 'o'
    Draw-Rect $g (6 + $dx) (10 + $dy) 4 1 'o'; Draw-Rect $g (3 + $dx) (11 + $dy) 10 1 $shade
}

$icons = [ordered]@{}

$g = New-Grid; Draw-Face $g; Draw-Line $g 0 13 2 13 'w'; Draw-Line $g 13 13 15 13 'w'; Set-Px $g 0 12 'w'; Set-Px $g 0 14 'w'; Set-Px $g 15 12 'w'; Set-Px $g 15 14 'w'
$icons['no'] = $g
$g = New-Grid; Draw-Face $g; Draw-Line $g 14 3 14 9 'w'; Set-Px $g 13 4 'w'; Set-Px $g 15 4 'w'; Set-Px $g 13 8 'w'; Set-Px $g 15 8 'w'
$icons['yes'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 5, 3, 3, 2, 0); Draw-Arm $g @(10, 6, 11, 10); Set-Px $g 0 1 'w'; Set-Px $g 0 3 'w'; Set-Px $g 4 0 'w'
$icons['wave'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 3, 4, 5, 2); Draw-Arm $g @(10, 6, 11, 10)
$icons['salute'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 5, 3, 1); Draw-Arm $g @(10, 5, 12, 1); Set-Px $g 1 2 'y'; Set-Px $g 14 2 'y'; Set-Px $g 2 0 'y'; Set-Px $g 13 0 'y'
$icons['cheer'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 7, 8); Draw-Arm $g @(10, 6, 8, 8); Set-Px $g 7 10 'y'; Set-Px $g 5 9 'y'; Set-Px $g 10 9 'y'; Set-Px $g 7 12 'y'
$icons['clap'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 4, 8, 6, 5); Draw-Arm $g @(10, 6, 11, 10); Draw-Line $g 12 0 13 0 'w'; Set-Px $g 14 1 'w'; Set-Px $g 13 2 'w'; Set-Px $g 13 4 'w'
$icons['think'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 0, 6); Draw-Arm $g @(10, 6, 11, 10); Set-Px $g 0 5 'o'
$icons['point'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 3, 8, 2, 5); Draw-Arm $g @(10, 6, 12, 8, 13, 5); Set-Px $g 1 3 'w'; Set-Px $g 14 3 'w'
$icons['shrug'] = $g
$g = New-Grid; Draw-Figure $g -headDx 1; Draw-Arm $g @(5, 5, 4, 0); Draw-Line $g 12 1 13 0 'w'; Draw-Line $g 12 3 14 3 'w'; Set-Px $g 3 1 'y'
Draw-Arm $g @(10, 6, 11, 10)
$icons['headbang'] = $g
$g = New-Grid; Draw-Face $g; Draw-Rect $g 4 8 2 4 't'; Draw-Rect $g 10 8 2 4 't'; Draw-Rect $g 5 13 1 2 't'; Draw-Rect $g 10 13 1 2 't'; Draw-Rect $g 6 10 4 1 'r'
$icons['weep'] = $g
$g = New-Grid; Draw-Face $g; Draw-Rect $g 2 5 7 4 'd'; Draw-Rect $g 1 9 4 6 'b'; Draw-Line $g 2 5 8 5 'o'
$icons['facepalm'] = $g
$g = New-Grid; Draw-Figure $g -KickLeft; Draw-Arm $g @(5, 5, 4, 1); Draw-Arm $g @(10, 5, 11, 1); Set-Px $g 1 13 'p'; Set-Px $g 2 11 'p'; Set-Px $g 14 4 'p'; Set-Px $g 1 1 'p'
$icons['dance'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 5, 0, 5); Draw-Arm $g @(10, 5, 15, 5); Draw-Arm $g @(5, 6, 0, 6); Draw-Arm $g @(10, 6, 15, 6)
$icons['tpose'] = $g
$g = New-Grid; Draw-Figure $g -headDx -1; Draw-Arm $g @(5, 6, 9, 2, 10, 1); Draw-Arm $g @(10, 6, 15, 1)
$icons['dab'] = $g
$g = New-Grid; Draw-Figure $g -NoLegs; Draw-Arm $g @(5, 5, 6, 0); Draw-Arm $g @(10, 5, 9, 0); Draw-Rect $g 6 11 4 2 'l'
Draw-Rect $g 6 13 4 1 'f'; Draw-Rect $g 7 14 2 1 'y'; Set-Px $g 5 13 'r'; Set-Px $g 10 13 'r'; Set-Px $g 7 15 'f'
$icons['jet'] = $g
$g = New-Grid; Draw-Face $g; Draw-Rect $g 5 6 6 2 'h'; Draw-Rect $g 3 2 10 4 'h'; Draw-Rect $g 6 10 4 1 'h'
Draw-Line $g 0 4 0 9 'p'; Set-Px $g 1 3 'p'; Set-Px $g 1 10 'p'; Draw-Line $g 15 4 15 9 'p'; Set-Px $g 14 3 'p'; Set-Px $g 14 10 'p'
$icons['exorcist'] = $g
$g = New-Grid; Draw-Figure $g -skin 'g' -shade 'k'; Draw-Arm $g @(5, 6, 15, 6) 'g'; Draw-Arm $g @(10, 7, 15, 7) 'k'
$icons['zombie'] = $g
$g = New-Grid; Draw-Figure $g; Draw-Arm $g @(5, 6, 4, 10); Draw-Arm $g @(10, 6, 11, 10)
Draw-Line $g 12 0 14 0 'y'; Set-Px $g 15 1 'y'; Set-Px $g 14 2 'y'; Set-Px $g 13 3 'y'; Set-Px $g 13 5 'y'
$icons['custom'] = $g
# Patron tier badges, drawn over a patreon emote's icon (top-left corner).
$g = New-Grid; Draw-Line $g 0 0 3 0 'f'; Draw-Line $g 0 1 2 1 'f'; Draw-Line $g 0 2 1 2 'f'; Set-Px $g 0 3 'f'; Set-Px $g 0 0 'y'
$icons['patreon_t1'] = $g
$g = New-Grid; Draw-Line $g 0 0 4 0 'r'; Draw-Line $g 0 1 3 1 'r'; Draw-Line $g 0 2 2 2 'r'; Draw-Line $g 0 3 1 3 'r'; Set-Px $g 0 4 'r'; Set-Px $g 0 0 'y'; Set-Px $g 1 0 'y'
$icons['patreon_t2'] = $g

$pal = @{}
foreach ($k in $palette.Keys) { $pal[$k] = $palette[$k] }

$inc = [System.Text.StringBuilder]::new()
[void]$inc.AppendLine('// Generated by tools/textures/emotes.ps1: the emote icons (16x16 rows, one palette char per pixel).')
[void]$inc.AppendLine('// clang-format off')
[void]$inc.Append('inline constexpr IconPaletteEntry kIconPalette[] = {')
foreach ($k in $palette.Keys) { [void]$inc.Append("{'$k', 0x$($palette[$k].Substring(1))}, ") }
[void]$inc.AppendLine('};')
[void]$inc.AppendLine('inline constexpr IconDef kIcons[] = {')
foreach ($id in $icons.Keys) {
    $rows = Get-Rows $icons[$id]
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $outDir "$id.png")
    [void]$inc.Append("    {`"$id`", {")
    [void]$inc.Append((($rows | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$inc.AppendLine('}},')
}
[void]$inc.AppendLine('};')
[void]$inc.AppendLine('// clang-format on')
$incPath = Join-Path $root 'src/client/emotes/EmoteIcons.inc'
New-Item -ItemType Directory -Force (Split-Path $incPath) | Out-Null
[System.IO.File]::WriteAllText($incPath, $inc.ToString().Replace("`r`n", "`n"))
Write-Host "emotes: $($icons.Count) icons -> $outDir, $incPath"
