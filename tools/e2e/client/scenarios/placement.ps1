# U17 placement on a real client. Builds a ledge high above the flat world and checks, with
# screenshots:
#   - ReacharoundPlacing: the game's own lenient edge placement, then the crosshair indicator and a
#     right click in the air that the server's reacharound turns into a placement (server log line,
#     also for vanilla clients).
#   - LockRotation: the K keybind (mod clients) / `/quark lockrotation` (vanilla clients) and the
#     HUD indicator.
#   - VariantSelector (local mode, enabled by placement.prelaunch.ps1): the radial menu on R, the
#     HUD, placing the chosen variant.
#   - LongRangePickBlock: middle click on a block 40 blocks away.
#   - The trowel and hammer icons in the inventory.
# Screenshots: out/placement/{edge-lenient,edge-lenient-placed,reacharound-horizontal,reacharound-placed,
#   lock-hud,lock-placed,variant-radial,variant-hud,variant-placed,long-pick,items}.png
$vanilla = $script:Mode -eq 'remote-vanilla'
$local   = $script:Mode -eq 'local'

function Use-Right { Assert-GameForeground; [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true); Start-Sleep -Milliseconds 400 }
function Use-Middle { Assert-GameForeground; [BsE2E.Native]::MouseButton(2, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(2, $true); Start-Sleep -Milliseconds 400 }

# The server checks a block and logs through `/quark net` only when it is there.
function Test-BlockAt([int]$x, [int]$y, [int]$z, [string]$block) {
    $mm = Get-LogMark
    Invoke-Chat "/execute if block $x $y $z $block run quark net"
    try { Wait-LogLine 'Lepton net: \d+ of \d+ player' 6 -Since $mm | Out-Null; return $true } catch { return $false }
}
function Assert-BlockAt([int]$x, [int]$y, [int]$z, [string]$block) {
    if (-not (Test-BlockAt $x $y $z $block)) { throw "no $block at $x $y $z" }
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s 0.5 101 0.5'
Invoke-Chat '/tp @s 0.5 101 0.5'   # the first chat line after joining is sometimes lost
Start-Sleep -Seconds 3
Invoke-Chat '/fill -4 96 -4 4 104 8 air'
Invoke-Chat '/setblock 0 100 0 stone'
Invoke-Chat '/setblock 0 100 2 stone'
Invoke-Chat '/setblock 0 100 6 stone'
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s cobblestone 32'
Press-Key 1
Save-Screenshot 'setup' | Out-Null

# ---- ReacharoundPlacing ------------------------------------------------------------------------
# Close to the ledge's edge, Bedrock's own (lenient) hit detection already targets the ledge's
# side: the crosshair shows no reacharound indicator (Quark hides it while the game targets a block)
# and a click is the game's normal placement against that face.
$m = Get-LogMark
Invoke-Chat '/tp @s 0.7 101 0.5 -90 70'
Start-Sleep -Seconds 1
Save-Screenshot 'edge-lenient' | Out-Null
Use-Right
Start-Sleep -Milliseconds 800
Assert-BlockAt 1 100 0 'cobblestone'
Save-Screenshot 'edge-lenient-placed' | Out-Null

# Reacharound proper: on the third ledge's east edge, looking down past it, far enough out that
# the game targets nothing (the click is a "use item" in the air) while the ray moved back by
# `leniency` hits the ledge: the server places the block east of the ledge. The client's eye is a
# little higher than the server's, so a few poses are tried; the indicator (mod clients) shows
# which ones have a target.
$served = $null
foreach ($pose in @(@(1.06, 80), @(1.08, 80), @(1.04, 80), @(1.06, 84), @(1.08, 84), @(1.10, 84), @(1.06, 76), @(1.10, 80))) {
    Invoke-Chat '/setblock 1 100 6 air'
    $m = Get-LogMark
    Invoke-Chat "/tp @s $($pose[0]) 101 6.5 -90 $($pose[1])"
    Start-Sleep -Seconds 1
    if (-not $vanilla) {
        try { Wait-LogLine 'placement: reacharound indicator horizontal' 4 -Since $m | Out-Null } catch { Write-Step "pose $($pose -join ' '): no indicator"; continue }
        Save-Screenshot 'reacharound-horizontal' | Out-Null
    }
    $m = Get-LogMark
    Use-Right
    try {
        Wait-LogLine 'placement: reacharound placed minecraft:cobblestone at 1 100 6 \(horizontal\)' 5 -Since $m | Out-Null
        $served = $pose
        break
    } catch { Write-Step "pose $($pose -join ' '): not placed by the server" }
}
if (-not $served) { throw 'no pose made the server place a reacharound block' }
Write-Step "server reacharound placed the block from pose $($served -join ' ')"
Assert-BlockAt 1 100 6 'cobblestone'
Start-Sleep -Milliseconds 800
Save-Screenshot 'reacharound-placed' | Out-Null

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
    Invoke-Chat '/setblock 0 102 40 gold_block'
    Invoke-Chat '/tp @s 0.5 101 2.5 0 0'
    Start-Sleep -Seconds 2
    $m = Get-LogMark
    Use-Middle
    Wait-LogLine 'placement: long range pick at 0 102 40' 15 -Since $m | Out-Null
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