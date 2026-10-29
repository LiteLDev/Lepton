# U13 enchanting on a real client. Run with -Mode local, remote-mod and remote-vanilla.
#   items:     the Ancient Tome and the Rune template in the hotbar (textures, glint)
#   matrix:    `/quark enchanting demo` builds a matrix enchanter (bookshelves, a lit red candle, a
#              sword with three pieces) and opens it: the native matrix screen (mod clients) or the
#              form (vanilla). Mod clients: select a piece, place it, generate one more, hover the
#              grid (tooltip), take the enchanted sword.
#   anvil:     a real anvil with a Sharpness V sword + Sharpness tome: the craft overlay (mod
#              clients) or the "Apply Ancient Tome?" form (vanilla); applying gives Sharpness VI.
#   smithing:  a real smithing table with the rune template + enchanted sword + lime dye.
# Screenshots: out/enchanting/{items,matrix,matrix-selected,matrix-placed,matrix-hover,matrix-taken,
#              anvil,anvil-done,smithing,smithing-done}.png
$vanilla = $script:Mode -eq 'remote-vanilla'

function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}

# Opens the station placed by `/quark enchanting demo <what>`: look at it and use it.
function Open-Station([string]$what) {
    $m = Get-LogMark
    Invoke-Chat "/quark enchanting demo $what"
    $line = Wait-LogLine 'enchanting: demo station .* at -?\d+ -?\d+ -?\d+' 15 -Since $m
    if ($line -notmatch 'at (-?\d+) (-?\d+) (-?\d+)') { throw "no station position in '$line'" }
    $x, $y, $z = [int]$Matches[1], [int]$Matches[2], [int]$Matches[3]
    Invoke-Chat "/tp @s $($x).5 $y $($z - 2).5 facing $($x).5 $($y).8 $($z).5"
    Start-Sleep -Milliseconds 1200
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 1500
}

Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival @s'
Invoke-Chat '/give @s lepton:ancient_tome'
Invoke-Chat '/give @s lepton:smithing_template_rune 3'
Invoke-Chat '/quark enchanting tome sharpness'
Invoke-Chat '/give @s lapis_lazuli 32'
Start-Sleep -Seconds 1
Save-Screenshot 'items' | Out-Null

# ---- matrix enchanting ---------------------------------------------------------------------------
$m = Get-LogMark
Invoke-Chat '/quark enchanting demo'
Wait-LogLine 'enchanting: demo table ready' 20 -Since $m | Out-Null
if ($vanilla) {
    Wait-LogLine 'opened the matrix enchanter at .* \(form\)' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'matrix' | Out-Null
    Press-Key ESC
    Start-Sleep -Milliseconds 800
} else {
    Wait-LogLine 'opened the matrix enchanter at .* \(screen\)' 10 -Since $m | Out-Null
    Wait-LogLine 'enchanting: matrix screen opened' 15 -Since $m | Out-Null
    $bench = Get-Fraction (Wait-LogLine 'enchanting: matrix bench at' 10 -Since $m)
    $grid  = Get-Fraction (Wait-LogLine 'enchanting: matrix grid at' 10 -Since $m)
    $plus  = Get-Fraction (Wait-LogLine 'enchanting: matrix plus at' 10 -Since $m)
    $out   = Get-Fraction (Wait-LogLine 'enchanting: matrix output at' 10 -Since $m)
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'matrix' | Out-Null

    # Select the first benched piece, rotate it (right click), place it in the middle of the grid.
    Click $bench[0] $bench[1] -Relative
    Start-Sleep -Milliseconds 500
    $c = Get-LogMark
    Click $bench[0] $bench[1] -Relative -Right
    Wait-LogLine 'enchanting: .* rotated a piece' 10 -Since $c | Out-Null
    Move-Mouse $grid[0] $grid[1] -Relative
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'matrix-selected' | Out-Null
    $c = Get-LogMark
    Click $grid[0] $grid[1] -Relative
    Wait-LogLine 'enchanting: .* placed a piece' 10 -Since $c | Out-Null

    # One more piece with "+".
    $c = Get-LogMark
    Click $plus[0] $plus[1] -Relative
    Wait-LogLine 'enchanting: .* generated a piece \(4 pieces\)' 10 -Since $c | Out-Null
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'matrix-placed' | Out-Null

    # Hover the placed piece: Quark's tooltip.
    Move-Mouse $grid[0] $grid[1] -Relative
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'matrix-hover' | Out-Null

    # Take the enchanted sword.
    $c = Get-LogMark
    Click $out[0] $out[1] -Relative
    Wait-LogLine 'enchanting: .* enchanted minecraft:diamond_sword' 10 -Since $c | Out-Null
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'matrix-taken' | Out-Null

    $c = Get-LogMark
    Press-Key ESC
    Wait-LogLine 'enchanting: matrix screen closed' 10 -Since $c | Out-Null
    Start-Sleep -Milliseconds 800
}

# ---- anvil: ancient tome -------------------------------------------------------------------------
# Chat closes screens: arm the fill first, it happens when the station opens.
$m = Get-LogMark
Invoke-Chat '/quark enchanting demo fill'
Open-Station 'anvil'
Wait-LogLine 'enchanting: demo station filled' 10 -Since $m | Out-Null
if ($vanilla) {
    # The vanilla client keeps the station screen open: the player is asked to close it.
    Wait-LogLine 'enchanting: .* was asked to close the station' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'anvil' | Out-Null
    Press-Key ESC
    Wait-LogLine 'enchanting: .* was sent the tome form' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'anvil-form' | Out-Null
    Press-Key ESC
} else {
    $apply = Get-Fraction (Wait-LogLine 'enchanting: craft overlay drawn on .*anvil.*, apply at' 15 -Since $m)
    Start-Sleep -Milliseconds 500
    Save-Screenshot 'anvil' | Out-Null
    $c = Get-LogMark
    Click $apply[0] $apply[1] -Relative
    Wait-LogLine 'enchanting: .* applied tome -> minecraft:diamond_sword' 10 -Since $c | Out-Null
    Wait-LogLine 'enchanting: closed the .* after applying' 10 -Since $c | Out-Null
    Start-Sleep -Milliseconds 1000
    Save-Screenshot 'anvil-done' | Out-Null
}
Start-Sleep -Milliseconds 800

# ---- smithing table: color rune ------------------------------------------------------------------
# Chat closes screens: arm the fill first, it happens when the station opens.
$m = Get-LogMark
Invoke-Chat '/quark enchanting demo fill'
Open-Station 'smithing'
Wait-LogLine 'enchanting: demo station filled' 10 -Since $m | Out-Null
if ($vanilla) {
    # The vanilla client keeps the station screen open: the player is asked to close it.
    Wait-LogLine 'enchanting: .* was asked to close the station' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'smithing' | Out-Null
    Press-Key ESC
    Wait-LogLine 'enchanting: .* was sent the rune form' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'smithing-form' | Out-Null
    Press-Key ESC
} else {
    $apply = Get-Fraction (Wait-LogLine 'enchanting: craft overlay drawn on .*smithing.*, apply at' 15 -Since $m)
    Start-Sleep -Milliseconds 500
    Save-Screenshot 'smithing' | Out-Null
    $c = Get-LogMark
    Click $apply[0] $apply[1] -Relative
    Wait-LogLine 'enchanting: .* applied rune -> minecraft:diamond_sword' 10 -Since $c | Out-Null
    Wait-LogLine 'enchanting: closed the .* after applying' 10 -Since $c | Out-Null
    Start-Sleep -Milliseconds 1000
    Save-Screenshot 'smithing-done' | Out-Null
}
Start-Sleep -Milliseconds 800

if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'enchanting.*'
    if ($r.Fail -gt 0) { throw "enchanting selftests failed: $($r.Fail)" }
}
