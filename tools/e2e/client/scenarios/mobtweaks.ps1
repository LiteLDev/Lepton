# U26 mobtweaks on a real client (local world: integrated server + client mod).
# Screenshots (out/mobtweaks/):
#   inventory.png      the Dragon Scale icon (original pixel art) and a golden pickaxe in the hotbar
#   tooltip-gold.png   the golden pickaxe tooltip with the baked, italic "Fortune II" line
#   tooltip-repair.png the netherite sword tooltip with the DiamondRepair hint (same slot, swapped in)
# Then every mobtweaks selftest runs in the local world.
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 golden_pickaxe'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 netherite_sword'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 lepton:dragon_scale 5'
Start-Sleep -Seconds 1

$mark = Get-LogMark
Open-Inventory
Wait-LogLine 'ui demo: inventory overlay drawn' 20 -Since $mark | Out-Null
Save-Screenshot 'inventory' | Out-Null

# Hover the first hotbar slot (golden pickaxe): the baked enchantment line.
$slotLine = Wait-LogLine 'ui demo: slot .*hotbar.* at' 10 -Since $mark
$sx, $sy = Get-Fraction $slotLine
$m = Get-LogMark
Move-Mouse $sx $sy -Relative
Wait-LogLine 'mobtweaks: tooltip baked .*golden_pickaxe' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-gold' | Out-Null

Close-Screen

# The netherite sword in the same slot: the DiamondRepair hint.
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 netherite_sword'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Open-Inventory
Wait-LogLine 'ui demo: inventory overlay drawn' 20 -Since $mark | Out-Null
Move-Mouse ($sx + 0.002) $sy -Relative
Wait-LogLine 'mobtweaks: tooltip repair .*netherite_sword' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-repair' | Out-Null
Close-Screen

$r = Invoke-SelfTest 'mobtweaks.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobtweaks selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected 14 mobtweaks selftests to pass, got $($r.Pass)" }
