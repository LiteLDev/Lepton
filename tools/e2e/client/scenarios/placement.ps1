# U17 placement on a real client. Builds a ledge high above the flat world and checks, with
# screenshots:
#   - ReacharoundPlacing: the horizontal and vertical crosshair indicators, and a right click in the
#     air placing the block (also for vanilla clients: the server does it).
#   - LockRotation: the K keybind (mod clients) / `/quark lockrotation` (vanilla clients) and the
#     HUD indicator.
#   - VariantSelector (local mode, enabled by placement.prelaunch.ps1): the radial menu on R, the
#     HUD, placing the chosen variant.
#   - LongRangePickBlock: middle click on a block 40 blocks away.
#   - The trowel and hammer icons in the inventory.
# Screenshots: out/placement/{reacharound-horizontal,reacharound-placed,reacharound-vertical,
#   lock-hud,lock-placed,variant-radial,variant-hud,variant-placed,long-pick,items}.png
$vanilla = $script:Mode -eq 'remote-vanilla'
$local   = $script:Mode -eq 'local'

function Use-Right { Assert-GameForeground; [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true); Start-Sleep -Milliseconds 400 }
function Use-Middle { Assert-GameForeground; [BsE2E.Native]::MouseButton(2, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(2, $true); Start-Sleep -Milliseconds 400 }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s 0.5 101 0.5'
Start-Sleep -Seconds 3
Invoke-Chat '/fill -4 96 -4 4 104 4 air'
Invoke-Chat '/setblock 0 100 0 stone'
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s cobblestone 32'
Press-Key 1

# ---- ReacharoundPlacing ------------------------------------------------------------------------
$m = Get-LogMark
Invoke-Chat '/tp @s 0.7 101 0.5 -90 70'
Start-Sleep -Seconds 1
if (-not $vanilla) { Wait-LogLine 'placement: reacharound indicator horizontal' 15 -Since $m | Out-Null }
Save-Screenshot 'reacharound-horizontal' | Out-Null
$m = Get-LogMark
Use-Right
Wait-LogLine 'placement: reacharound placed minecraft:cobblestone at 1 100 0 \(horizontal\)' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'reacharound-placed' | Out-Null

$m = Get-LogMark
Invoke-Chat '/tp @s 1.2 101 0.5 90 84'
Start-Sleep -Seconds 1
if (-not $vanilla) {
    Wait-LogLine 'placement: reacharound indicator vertical' 15 -Since $m | Out-Null
    Save-Screenshot 'reacharound-vertical' | Out-Null
}

# ---- LockRotation --------------------------------------------------------------------------------
Invoke-Chat '/give @s oak_log 8'
Press-Key 2
Invoke-Chat '/tp @s 0.5 101 0.5 -90 55'
Start-Sleep -Seconds 1
$m = Get-LogMark
if ($vanilla) {
    Invoke-Chat '/quark lockrotation'
    Wait-LogLine 'placement: .+ locked rotation facing' 15 -Since $m | Out-Null
} else {
    Press-Key K
    Wait-LogLine 'placement: client locked rotation facing' 15 -Since $m | Out-Null
    Wait-LogLine 'placement: lock indicator' 15 -Since $m | Out-Null
}
Start-Sleep -Milliseconds 600
Save-Screenshot 'lock-hud' | Out-Null
Use-Right
Start-Sleep -Seconds 1
Save-Screenshot 'lock-placed' | Out-Null
$m = Get-LogMark
if ($vanilla) { Invoke-Chat '/quark lockrotation clear' } else { Press-Key K }
Wait-LogLine 'placement: .+ cleared the rotation lock' 15 -Since $m | Out-Null

# ---- VariantSelector (local: the prelaunch config enables it) --------------------------------------
if ($local) {
    Invoke-Chat '/give @s stone_bricks 16'
    Press-Key 3
    Invoke-Chat '/tp @s 0.5 101 2.5 180 40'
    Start-Sleep -Seconds 1
    $m = Get-LogMark
    Assert-GameForeground
    [BsE2E.Native]::Key([uint16][int][char]'R', $false)
    try {
        Wait-LogLine 'placement: variant selector opened' 15 -Since $m | Out-Null
        $seg = Wait-LogLine "placement: variant segment 2 'stairs' at" 10 -Since $m
        if ($seg -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no segment position in '$seg'" }
        Move-Mouse ([double]$Matches[1]) ([double]$Matches[2]) -Relative
        Start-Sleep -Milliseconds 900
        Save-Screenshot 'variant-radial' | Out-Null
    } finally {
        [BsE2E.Native]::Key([uint16][int][char]'R', $true)
    }
    Wait-LogLine "placement: variant selector picked 'stairs'" 10 -Since $m | Out-Null
    Wait-LogLine 'placement: .+ selected variant .stairs.' 10 -Since $m | Out-Null
    Wait-LogLine 'placement: variant hud minecraft:stone_bricks -> minecraft:stone_brick_stairs' 10 -Since $m | Out-Null
    Save-Screenshot 'variant-hud' | Out-Null
    $m = Get-LogMark
    Use-Right
    Wait-LogLine 'placement: placed variant minecraft:stone_brick_stairs' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'variant-placed' | Out-Null
    Invoke-Chat '/quark variant original'
}

# ---- LongRangePickBlock ------------------------------------------------------------------------
if (-not $vanilla) {
    Invoke-Chat '/setblock 0 101 40 gold_block'
    Invoke-Chat '/tp @s 0.5 101 0.5 0 0'
    Start-Sleep -Seconds 2
    $m = Get-LogMark
    Use-Middle
    Wait-LogLine 'placement: long range pick at 0 101 40' 15 -Since $m | Out-Null
    Wait-LogLine 'placement: picked minecraft:gold_block' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'long-pick' | Out-Null
}

# ---- items -----------------------------------------------------------------------------------
Invoke-Chat '/give @s lepton:trowel'
Invoke-Chat '/give @s lepton:hammer'
Open-Inventory
Save-Screenshot 'items' | Out-Null
Close-Screen

if ($local) {
    $r = Invoke-SelfTest 'placement.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) placement selftest(s) failed on the integrated server" }
}
