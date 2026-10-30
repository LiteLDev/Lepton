# The client UI toolkit (src/client/ui) on a real client, with the demo on (ui.prelaunch.ps1):
# HUD drawing, an inventory overlay (button, text field, greyed slot), a tooltip line, a keybind
# and a custom (radial) screen. Screenshots: out/ui/{hud,inventory,tooltip,radial}.png
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}

Invoke-Chat '/give @s diamond 3'
Start-Sleep -Seconds 1
Save-Screenshot 'hud' | Out-Null

$mark = Get-LogMark
Open-Inventory
Wait-LogLine 'ui demo: inventory overlay drawn' 20 -Since $mark | Out-Null
Save-Screenshot 'inventory' | Out-Null

# Hover the first hotbar slot (the diamonds): the tooltip shows the extra line.
$slotLine = Wait-LogLine 'ui demo: slot .*hotbar.* at' 10 -Since $mark
$sx, $sy = Get-Fraction $slotLine
Move-Mouse $sx $sy -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip' | Out-Null

# The overlay button.
$btn = Wait-LogLine 'ui demo: button at' 10 -Since $mark
$bx, $by = Get-Fraction $btn
$m = Get-LogMark
Click $bx $by -Relative
Wait-LogLine 'ui demo: button clicked' 10 -Since $m | Out-Null

# A button at the right edge: its tooltip flips to the left and stays on the screen.
$edge = Wait-LogLine 'ui demo: edge button at' 10 -Since $mark
$ex, $ey = Get-Fraction $edge
Move-Mouse $ex $ey -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'edge-tooltip' | Out-Null

# The text field: focus it and type with key presses (E must not close the inventory).
$field = Wait-LogLine 'ui demo: field at' 10 -Since $mark
$fx, $fy = Get-Fraction $field
Click $fx $fy -Relative
$m = Get-LogMark
Press-Key E
Press-Key D
Wait-LogLine "ui demo: search 'ed'" 10 -Since $m | Out-Null
Save-Screenshot 'inventory-typed' | Out-Null
Close-Screen
Close-Screen

# The keybind opens the radial custom screen; a click on cell 1 picks it and closes it.
$m = Get-LogMark
Press-Key J
Wait-LogLine 'ui: custom screen opened' 10 -Since $m | Out-Null
$cell = Wait-LogLine 'ui demo: radial cell 1 at' 10 -Since $m
Start-Sleep -Milliseconds 500
Save-Screenshot 'radial' | Out-Null
$cx, $cy = Get-Fraction $cell
Click $cx $cy -Relative
Wait-LogLine 'ui demo: radial pick 1' 10 -Since $m | Out-Null
Wait-LogLine 'ui: custom screen closed' 10 -Since $m | Out-Null

# Esc closes it too.
$m = Get-LogMark
Press-Key J
Wait-LogLine 'ui: custom screen opened' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 500
Press-Key ESC
Wait-LogLine 'ui demo: radial closed' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'after-radial' | Out-Null

$r = Invoke-SelfTest 'ui.*'
if ($r.Pass -lt 2) { throw "expected the ui selftests to pass, got $($r.Pass)" }
