# U16 hud on a real client: usage ticker, elytra indicator, improved tooltips (attributes, food,
# shulker preview, enchanted book, map, fuel), auto-walk and the back keybind (hud.prelaunch.ps1
# binds autorun to G and back to B). Screenshots: out/hud/*.png
function Get-Slots([string]$line) {
    if ($line -notmatch 'slots at (.+)$') { throw "no slots in '$line'" }
    return @($Matches[1].Trim().Split(';') | ForEach-Object { $p = $_.Split(','); , @([double]$p[0], [double]$p[1]) })
}
function Hover-Slot($slots, [int]$i, [string]$shot) {
    Move-Mouse 0.5 0.05 -Relative
    Start-Sleep -Milliseconds 300
    Move-Mouse $slots[$i][0] $slots[$i][1] -Relative
    Start-Sleep -Milliseconds 900
    Save-Screenshot $shot | Out-Null
}

# ---- usage ticker -----------------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s cobblestone 64'
Invoke-Chat '/give @s cobblestone 64'
Invoke-Chat '/give @s cobblestone 10'
Wait-LogLine 'hud: usage ticker shows' 20 | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'ticker' | Out-Null

# Armour (elytra in the chest slot) + the elytra indicator on the armour bar.
Invoke-Chat '/replaceitem entity @s slot.armor.head 0 iron_helmet'
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 elytra'
Invoke-Chat '/replaceitem entity @s slot.armor.legs 0 iron_leggings'
Invoke-Chat '/replaceitem entity @s slot.armor.feet 0 iron_boots'
Start-Sleep -Milliseconds 500
Save-Screenshot 'ticker-armor' | Out-Null
Wait-LogLine 'hud: elytra indicator drawn' 20 | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'elytra' | Out-Null

# ---- tooltips -----------------------------------------------------------------------------------------
$m = Get-LogMark
Invoke-Chat '/quark debug hud'
Wait-LogLine 'hud: debug kit given' 20 -Since $m | Out-Null
# Turn the empty map (slot 6) into a filled map of the area.
Press-Key 6
Start-Sleep -Milliseconds 300
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Press-Key 1
Start-Sleep -Milliseconds 500

$m = Get-LogMark
Open-Inventory
$slots = Get-Slots (Wait-LogLine 'hud: inventory_screen hotbar slots at' 20 -Since $m)
Hover-Slot $slots 0 'tooltip-sword'
Wait-LogLine 'hud: tooltip drawn' 10 -Since $m | Out-Null
Hover-Slot $slots 1 'tooltip-helmet'
Hover-Slot $slots 2 'tooltip-food'
Hover-Slot $slots 3 'tooltip-shulker'
Hover-Slot $slots 4 'tooltip-book'
Hover-Slot $slots 5 'tooltip-map'
Hover-Slot $slots 6 'tooltip-poison'
# Shift shows the vanilla attribute lines instead of the icons.
[BsE2E.Native]::Key([uint16]0x10, $false)
Hover-Slot $slots 0 'tooltip-sword-shift'
[BsE2E.Native]::Key([uint16]0x10, $true)

# ---- back keybind: closes the inventory -----------------------------------------------------------------
$m = Get-LogMark
Press-Key B
Wait-LogLine 'hud: back button on inventory_screen' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'back' | Out-Null

# ---- fuel in a furnace screen ---------------------------------------------------------------------------
Invoke-Chat '/setblock ~2 ~ ~ furnace'
Invoke-Chat '/tp @s ~ ~ ~ facing ~2 ~0.5 ~'
Start-Sleep -Milliseconds 800
$m = Get-LogMark
Click 0.5 0.5 -Relative -Right
$line = Wait-LogLine 'hud: \S*furnace\S* hotbar slots at' 20 -Since $m
$fslots = Get-Slots $line
Hover-Slot $fslots 7 'tooltip-fuel'
Close-Screen

# ---- auto-walk ------------------------------------------------------------------------------------------
Invoke-Chat '/setblock ~2 ~ ~ air'
Invoke-Chat '/tp @s ~ ~ ~ facing ~10 ~1.6 ~'
Start-Sleep -Milliseconds 500
$m = Get-LogMark
Press-Key G
Wait-LogLine 'hud: auto-walk on' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'autowalk' | Out-Null
Press-Key W
Wait-LogLine 'hud: auto-walk off \(forward key\)' 10 -Since $m | Out-Null
$walked = Wait-LogLine 'hud: auto-walked' 10 -Since $m
if ($walked -notmatch 'auto-walked ([0-9.]+) blocks' -or [double]$Matches[1] -lt 2) { throw "auto-walk did not move the player: $walked" }

# ---- selftests (logic, in the local world) -------------------------------------------------------------
$r = Invoke-SelfTest 'hud.*'
if ($r.Fail -gt 0) { throw "hud selftests failed: $(($r.Failed | ForEach-Object { "$($_.Name): $($_.Reason)" }) -join '; ')" }
if ($r.Pass -lt 11) { throw "expected at least 11 hud selftests, got $($r.Pass)" }
