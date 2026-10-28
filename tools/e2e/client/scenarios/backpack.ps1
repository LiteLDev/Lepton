# U14 backpack on a real client (docs/spec/backpack.md §8). Modes: local, remote-mod, remote-vanilla.
# Screenshots: out/backpack/
#   items.png        the inventory with the backpack, ravager hide, bonded hide and seed pouch icons
#   worn.png         front view of the player wearing the backpack (third person)
#   screen.png       E while wearing it: the "Backpack" chest screen (+ the "Inventory" button for mod clients)
#   inventory.png    (mod clients) after clicking "Inventory": the normal inventory with the "Backpack" button
#   totem.png        a Totem of Holding floating in front of the player
#   pouch.png        a full seed pouch in the hotbar (lore tooltip in the inventory: pouch-tooltip.png)
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}
$vanilla = $script:Mode -eq 'remote-vanilla'
if (-not $vanilla) { Wait-LogLine 'Lepton server handshake' 60 | Out-Null }

Invoke-Chat '/gamemode survival'
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s lepton:backpack'
Invoke-Chat '/give @s lepton:ravager_hide 5'
Invoke-Chat '/give @s lepton:bonded_ravager_hide 3'
Invoke-Chat '/give @s lepton:seed_pouch'
Invoke-Chat '/give @s wheat_seeds 32'
Start-Sleep -Seconds 1
Open-Inventory
Save-Screenshot 'items' | Out-Null
Close-Screen

# Wear it (chest slot) with something inside, look at the player from the front.
Invoke-Chat '/clear @s lepton:backpack'
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 lepton:backpack'
Start-Sleep -Seconds 1
Press-Key F5; Start-Sleep -Milliseconds 300; Press-Key F5
Start-Sleep -Seconds 2
Save-Screenshot 'worn' | Out-Null
Press-Key F5
Start-Sleep -Seconds 1

# E opens the backpack screen.
$m = Get-LogMark
Open-Inventory
Wait-LogLine 'backpack: opened by' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'screen' | Out-Null
if (-not $vanilla) {
    $btn = Wait-LogLine 'backpack client: inventory button at' 10 -Since $m
    $bx, $by = Get-Fraction $btn
    Click $bx $by -Relative
    Wait-LogLine 'backpack: closed by' 10 -Since $m | Out-Null
    Wait-LogLine 'backpack: .* switched to the inventory' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'inventory' | Out-Null
}
Close-Screen
Wait-LogLine 'backpack: closed by' 10 -Since $m | Out-Null

# A Totem of Holding.
$m = Get-LogMark
Invoke-Chat '/quark debug totem'
Wait-LogLine 'totem: debug totem' 10 -Since $m | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'totem' | Out-Null

# A full seed pouch.
Invoke-Chat '/clear @s'
Invoke-Chat '/quark seedpouch wheat_seeds'
Start-Sleep -Seconds 1
Save-Screenshot 'pouch' | Out-Null

if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'backpack.*' -TimeoutSeconds 600
    if ($r.Fail -gt 0) { throw "$($r.Fail) backpack selftest(s) failed on the integrated server" }
}
Invoke-Chat '/clear @s'
