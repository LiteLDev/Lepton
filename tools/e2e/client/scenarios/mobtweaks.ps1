# U26 mobtweaks on a real client (local world: integrated server + client mod).
# Screenshots (out/mobtweaks/):
#   inventory.png      the Dragon Scale icon (original pixel art) and a golden pickaxe in the hotbar
#   tooltip-gold.png   the golden pickaxe tooltip with the baked, italic "Fortune II" line
#   tooltip-repair.png the netherite sword tooltip with the DiamondRepair hint (same slot, swapped in)
#   craft-elytra.png   elytra + dragon scale in a crafting table, the new elytra in the result slot
#   craft-elytra-done.png  after taking it: the new elytra and the original (damaged) one
#   anvil-rename-{0,1,2}.png  an iron sword renamed three times; anvil-after.png the undamaged anvil
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
$tipMark = $mark # the game builds the hover texts of every slot when the screen opens (logged once per item)
Open-Inventory
Wait-LogLine 'ui demo: inventory overlay drawn' 20 -Since $mark | Out-Null
Save-Screenshot 'inventory' | Out-Null

# Hover the first hotbar slot (golden pickaxe): the baked enchantment line.
$slotLine = Wait-LogLine 'ui demo: slot .*hotbar.* at' 10 -Since $mark
$sx, $sy = Get-Fraction $slotLine
$m = Get-LogMark
Move-Mouse $sx $sy -Relative
Wait-LogLine 'mobtweaks: tooltip baked .*golden_pickaxe' 10 -Since $tipMark | Out-Null
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
Wait-LogLine 'mobtweaks: tooltip repair .*netherite_sword' 10 -Since $tipMark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-repair' | Out-Null
Close-Screen

function Use-Item {
    # a right click where the player looks (no mouse movement: that would turn the camera)
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
    Start-Sleep -Milliseconds 300
}
# Centres the player on its block, looking south (the set-ups below are relative to that).
function Center-Player([int]$pitch = 0) {
    Invoke-Chat "/execute as @s at @s align xyz run tp @s ~0.5 ~ ~0.5 0 $pitch"
    Start-Sleep -Milliseconds 500
}
# Opens the block 2 south (at eye level) and returns its slots: "collection:index" -> (x, y), plus
# "unit" -> the screen fraction of one UI unit (x, y).
function Open-Slots([string]$what) {
    $m = Get-LogMark
    Use-Item
    Wait-LogLine "mobtweaks: screen .* unit" 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 500
    $slot = @{}
    foreach ($l in (Get-LeviLog -Since $m)) {
        if ($l -match 'mobtweaks: slot \S+ (\S+):(\d+) at ([0-9.]+),([0-9.]+)') { $slot["$($Matches[1]):$($Matches[2])"] = @([double]$Matches[3], [double]$Matches[4]) }
        if ($l -match 'mobtweaks: screen \S+ unit ([0-9.]+),([0-9.]+)') { $slot['unit'] = @([double]$Matches[1], [double]$Matches[2]) }
    }
    Write-Host "[mobtweaks] $what slots: $($slot.Keys -join ', ')"
    return $slot
}
function Find-Slot($slots, [string]$pattern, [int]$index = 0) {
    $key = $slots.Keys | Where-Object { $_ -match $pattern -and $_ -match ":$index$" } | Sort-Object | Select-Object -First 1
    if (-not $key) { throw "no slot matching '$pattern' :$index in $($slots.Keys -join ', ')" }
    return $slots[$key]
}
function Click-Slot($at) { Click $at[0] $at[1] -Relative; Start-Sleep -Milliseconds 250 }

$mtFailures = @()
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/xp 30L @s'
Center-Player 6

# DragonScales: elytra + dragon scale in a real crafting table gives a new elytra and the original
# (damaged) one comes back.
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 lepton:dragon_scale 5'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 elytra 1 200'
Invoke-Chat '/setblock ~ ~ ~2 stone'
Invoke-Chat '/setblock ~ ~1 ~2 crafting_table'
Start-Sleep -Milliseconds 800
$slots = Open-Slots 'crafting table'
$grid0 = Find-Slot $slots 'crafting.*input|crafting_items|crafting.*grid' 0
$grid1 = Find-Slot $slots 'crafting.*input|crafting_items|crafting.*grid' 1
$result = Find-Slot $slots 'output|result' 0
$m = Get-LogMark
Click-Slot $slots['hotbar_items:3']; Click-Slot $grid0
Click-Slot $slots['hotbar_items:2']; Click-Slot $grid1
Start-Sleep -Milliseconds 600
Move-Mouse $result[0] $result[1] -Relative
Start-Sleep -Milliseconds 700
Save-Screenshot 'craft-elytra' | Out-Null
Click-Slot $result
Click-Slot $slots['hotbar_items:5']
try { Wait-LogLine 'mobtweaks: elytra duplicated' 15 -Since $m | Out-Null } catch { $mtFailures += 'elytra not duplicated' }
Start-Sleep -Milliseconds 800
Save-Screenshot 'craft-elytra-done' | Out-Null
Close-Screen
Invoke-Chat '/setblock ~ ~1 ~2 air'
Start-Sleep -Milliseconds 500

# NoDurabilityOnCosmetics: renaming in an anvil never damages it (three renames, each logged).
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 4 iron_sword'
Invoke-Chat '/setblock ~ ~1 ~2 anvil'
Start-Sleep -Milliseconds 800
$names = @('mob', 'tweak', 'quark')
for ($i = 0; $i -lt $names.Count; $i++) {
    # a fresh sword in hotbar slot 4 each time (the renamed one lands wherever the game puts it)
    Invoke-Chat '/clear @s iron_sword'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 4 iron_sword'
    Start-Sleep -Milliseconds 500
    $slots = Open-Slots 'anvil'
    # The UI toolkit finds only the hotbar of the anvil screen: its input, result and name field are
    # placed from the first hotbar slot, in UI units measured on the anvil screen (anvil-rename-0.png).
    $u = $slots['unit']
    $h0 = $slots['hotbar_items:0']
    $in = @(($h0[0] + 13.8 * $u[0]), ($h0[1] - 95.9 * $u[1]))
    $out = @(($h0[0] + 121.5 * $u[0]), ($h0[1] - 95.9 * $u[1]))
    $field = @(($h0[0] + 89 * $u[0]), ($h0[1] - 122 * $u[1]))
    # a first harmless click (an empty slot): after a reopened screen the first click can be lost
    Click-Slot $slots['hotbar_items:8']
    Start-Sleep -Milliseconds 300
    Click-Slot $slots['hotbar_items:4']; Click-Slot $in
    Click $field[0] $field[1] -Relative
    Start-Sleep -Milliseconds 300
    Type-Text $names[$i]
    Start-Sleep -Milliseconds 600
    Save-Screenshot "anvil-rename-$i" | Out-Null
    $m = Get-LogMark
    Click-Slot $out
    Click-Slot $slots['hotbar_items:4']
    try { Wait-LogLine 'mobtweaks: anvil spared' 15 -Since $m | Out-Null } catch { $mtFailures += "anvil rename $i not spared" }
    Close-Screen
    Start-Sleep -Milliseconds 1500
}
Save-Screenshot 'anvil-after' | Out-Null
Invoke-Chat '/setblock ~ ~1 ~2 air'
Invoke-Chat '/setblock ~ ~ ~2 air'
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode creative'

$r = Invoke-SelfTest 'mobtweaks.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobtweaks selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected 15 mobtweaks selftests to pass, got $($r.Pass)" }
if ($mtFailures.Count) { throw ($mtFailures -join '; ') }
