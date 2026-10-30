# U15 inventory on a real client. Run it in every mode:
#   -Mode local / remote-mod: the inventory sort button, the container buttons (sort, insert,
#                             extract, shift lock, filter), the search bar greying out slots, and
#                             the hotbar changer HUD (Z, then 1).
#   -Mode remote-vanilla:     no overlay; the command / form fallbacks work (/quark sort,
#                             /quark hotbar 1, /quark transfer needs a looked-at chest: skipped).
# Screenshots: out/inventory/{inventory,chest,chest-search,hotbar}.png (mod), {vanilla-hotbar-form}.png
function Get-Fraction([string]$line, [string]$label = '') {
    if ($line -notmatch "$label at ([0-9.]+),([0-9.]+)") { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}

$vanilla = $script:Mode -eq 'remote-vanilla'
if (-not $vanilla) { Wait-LogLine 'Lepton client mod handshake' 60 | Out-Null }

Invoke-Chat '/clear @s'
Invoke-Chat '/give @s stone 20'
Invoke-Chat '/give @s torch 5'
Invoke-Chat '/give @s diamond_pickaxe 1'
Invoke-Chat '/replaceitem entity @s slot.inventory 20 dirt 7'
Invoke-Chat '/replaceitem entity @s slot.inventory 5 stone 30'
Invoke-Chat '/replaceitem entity @s slot.inventory 0 apple 3'
Start-Sleep -Seconds 1

if ($vanilla) {
    $m = Get-LogMark
    Invoke-Chat '/quark sort'
    Wait-LogLine 'inventory: sorted the inventory of' 15 -Since $m | Out-Null
    $m = Get-LogMark
    Invoke-Chat '/quark hotbar'
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'vanilla-hotbar-form' | Out-Null
    Press-Key ESC
    Start-Sleep -Milliseconds 500
    Invoke-Chat '/quark hotbar 1'
    Wait-LogLine 'inventory: .* swapped the hotbar with row 1' 15 -Since $m | Out-Null
    Start-Sleep -Seconds 3
    if (Get-LeviLog -Since $m | Select-String 'inventory: overlay on') { throw 'a vanilla client must not run the overlay' }
    $r = Invoke-SelfTest 'inventory.*'
    if ($r.Fail -gt 0) { throw "inventory selftests failed: $($r.Fail)" }
    return
}

# Inventory screen: the sort button above the top-right main slot.
$mark = Get-LogMark
Open-Inventory
$line = Wait-LogLine 'inventory: player buttons on inventory_screen: sort at' 20 -Since $mark
Save-Screenshot 'inventory' | Out-Null
$sx, $sy = Get-Fraction $line 'sort'
$m = Get-LogMark
Click $sx $sy -Relative
Wait-LogLine 'inventory: sorted the inventory of' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'inventory-sorted' | Out-Null
Close-Screen

# A 27-slot container (a Lepton virtual container in the vanilla chest screen): every button.
$m = Get-LogMark
Invoke-Chat '/quark debug container'
Wait-LogLine 'virtual container .* opened by' 15 -Since $m | Out-Null
$line = Wait-LogLine 'inventory: container buttons on .*: sort at' 20 -Since $m
Start-Sleep -Milliseconds 800
Save-Screenshot 'chest' | Out-Null
$cx, $cy = Get-Fraction $line 'sort'
$fx, $fy = Get-Fraction $line 'filter'
$ix, $iy = Get-Fraction $line 'insert'
$ex, $ey = Get-Fraction $line 'extract'
# Insert moves the main inventory (stone, dirt, ...) into the container, extract takes it back.
$m2 = Get-LogMark
Click $ix $iy -Relative
Wait-LogLine 'inventory: inserted [1-9]\d* items for' 15 -Since $m2 | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'chest-inserted' | Out-Null
$m2 = Get-LogMark
Click $cx $cy -Relative
Wait-LogLine 'inventory: sorted a container for' 15 -Since $m2 | Out-Null

# Filter: show the search bar and type "sto"; slots without stone are greyed out.
$m2 = Get-LogMark
Click $fx $fy -Relative
Wait-LogLine 'inventory: search bar shown' 10 -Since $m2 | Out-Null
Press-Key S
Press-Key T
Press-Key O
Wait-LogLine "inventory: search 'sto'" 10 -Since $m2 | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'chest-search' | Out-Null
# The filter button hides the bar again; extract takes everything back.
$m2 = Get-LogMark
Click $fx $fy -Relative
Wait-LogLine 'inventory: search bar hidden' 10 -Since $m2 | Out-Null
$m2 = Get-LogMark
Click $ex $ey -Relative
Wait-LogLine 'inventory: extracted [1-9]\d* items for' 15 -Since $m2 | Out-Null
Close-Screen

# ExpandedItemInteractions: right-click a shulker box in the inventory screen opens it (the
# inventory screen is replaced by the box's chest screen); closing it puts the box back.
Invoke-Chat '/replaceitem entity @s slot.inventory 26 red_shulker_box 1'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Open-Inventory
$line = Wait-LogLine 'inventory: player buttons on inventory_screen: sort at .* shulker at' 20 -Since $mark
$hx, $hy = Get-Fraction $line 'shulker'
$m = Get-LogMark
Click $hx $hy -Relative -Right
Wait-LogLine "inventory: interaction 'open' requested" 10 -Since $m | Out-Null
Wait-LogLine 'inventory: .* opened a held shulker box' 15 -Since $m | Out-Null
Wait-LogLine 'virtual container .* opened by' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'held-shulker' | Out-Null
$m = Get-LogMark
Close-Screen
Wait-LogLine 'inventory: .* closed a held shulker box' 15 -Since $m | Out-Null

# Hotbar changer: H slides the rows up, 1 swaps the hotbar with the first row.
$m = Get-LogMark
Press-Key H
Wait-LogLine 'inventory: hotbar changer opened' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'hotbar' | Out-Null
Press-Key 1
Wait-LogLine 'inventory: hotbar row 1 requested' 10 -Since $m | Out-Null
Wait-LogLine 'inventory: .* swapped the hotbar with row 1' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'hotbar-swapped' | Out-Null

$r = Invoke-SelfTest 'inventory.*'
if ($r.Fail -gt 0) { throw "inventory selftests failed: $($r.Fail)" }
