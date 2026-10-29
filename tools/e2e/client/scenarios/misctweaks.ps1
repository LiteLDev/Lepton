# U27 misctweaks on a real client (config from misctweaks.prelaunch.ps1).
#   local:          everything below; GameNerfs on (mending anvil nerf, "(Was Repaired)" tooltip).
#   remote-mod:     the server parts run on BDS (default config: GameNerfs off), the client parts
#                   (chat shift, narrator readout, recipe toast filter) on the client.
#   remote-vanilla: no client mod: the server parts only (rocket launch, armor stand shield).
# Screenshots: out/misctweaks/{chat-shift,inventory,rocket,armor-stand,anvil-mending,
# anvil-taken,anvil-repaired}.png
$vanilla = $script:Mode -eq 'remote-vanilla'
$local   = $script:Mode -eq 'local'

function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}
function Use-Item {
    # a right click where the player looks (no mouse movement: that would turn the camera)
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
    Start-Sleep -Milliseconds 300
}
# Centres the player on its block, looking south (all set-ups below are relative to that).
function Center-Player([int]$pitch = 0) {
    Invoke-Chat "/execute as @s at @s align xyz run tp @s ~0.5 ~ ~0.5 0 $pitch"
    Start-Sleep -Milliseconds 500
}

# AutomaticRecipeUnlock: every recipe on join, and (client mod) no toast for it.
Wait-LogLine 'misctweaks: unlocked all recipes for' 60 | Out-Null
if (-not $vanilla) { Wait-LogLine 'misctweaks: \d+ recipes unlocked without a toast' 30 | Out-Null }

Invoke-Chat '/gamemode survival'
Invoke-Chat '/xp 100L @s'
Invoke-Chat '/clear @s'
Center-Player

# AdjustableChat: the chat lines move 60 right, 40 up.
if (-not $vanilla) {
    $m = Get-LogMark
    Invoke-Chat 'misctweaks: this chat line is shifted'
    Wait-LogLine 'misctweaks: chat panel shifted by 60,-40' 15 | Out-Null
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'chat-shift' | Out-Null
}

# disableRecipeBook (local config): the survival inventory opens without the recipe book.
if ($local) {
    Open-Inventory
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'inventory' | Out-Null
    Close-Screen
}

# NarratorReadout: look at a block and read it out (N: full readout, B: short, as Quark binds them).
if (-not $vanilla) {
    Invoke-Chat '/setblock ~ ~ ~2 stone'
    Invoke-Chat '/setblock ~ ~1 ~2 stone'
    Center-Player 10
    $m = Get-LogMark
    Focus-Game
    Press-Key N
    Wait-LogLine "misctweaks: narrator \(full\) 'Looking at .*Health \d+, Food \d+'" 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 700
    $m = Get-LogMark
    Press-Key B
    Wait-LogLine "misctweaks: narrator 'Looking at [^,]+'" 10 -Since $m | Out-Null
    Invoke-Chat '/setblock ~ ~1 ~2 air'
    Invoke-Chat '/setblock ~ ~ ~2 air'
}

# ArmedArmorStands: a shield goes into the stand's off hand.
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 shield'
Focus-Game
Press-Key 1
Invoke-Chat '/summon armor_stand ~ ~ ~2'
Invoke-Chat '/tp @s ~ ~ ~ facing ~ ~1 ~2'
Start-Sleep -Milliseconds 800
$m = Get-LogMark
Use-Item
Wait-LogLine 'misctweaks: shield placed in an armor stand' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'armor-stand' | Out-Null
Invoke-Chat '/kill @e[type=armor_stand]'
Invoke-Chat '/clear @s'
Center-Player

# GameNerfs (local): Mending is removed in the anvil; a repaired item shows "(Was Repaired)".
if ($local) {
    Invoke-Chat '/replaceitem entity @s slot.hotbar 0 diamond_sword 1 900'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 1 diamond_sword 1 900'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 2 iron_sword 1 200'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 3 iron_sword 1 200'
    Focus-Game
    Press-Key 1
    Invoke-Chat '/enchant @s mending'
    Invoke-Chat '/setblock ~ ~ ~2 stone'
    Invoke-Chat '/setblock ~ ~1 ~2 anvil'
    Invoke-Chat '/tp @s ~ ~ ~ facing ~ ~1.4 ~2'
    Start-Sleep -Milliseconds 800

    function Use-Anvil([int]$a, [int]$b, [string]$shot) {
        $m = Get-LogMark
        Use-Item
        Wait-LogLine 'misctweaks: anvil slot' 15 -Since $m | Out-Null
        Start-Sleep -Milliseconds 500
        $lines = Get-LeviLog -Since $m | Where-Object { $_ -match 'misctweaks: anvil slot' }
        $slot = @{}
        foreach ($l in $lines) { if ($l -match 'anvil slot (\S+):(\d+) at ([0-9.]+),([0-9.]+)') { $slot["$($Matches[1]):$($Matches[2])"] = @([double]$Matches[3], [double]$Matches[4]) } }
        $inputKey = $slot.Keys | Where-Object { $_ -match 'input' } | Select-Object -First 1
        $materialKey = $slot.Keys | Where-Object { $_ -match 'material' } | Select-Object -First 1
        $resultKey = $slot.Keys | Where-Object { $_ -match 'result' } | Select-Object -First 1
        if (-not ($inputKey -and $materialKey -and $resultKey -and $slot["hotbar_items:$a"])) { throw "anvil slots not found: $($slot.Keys -join ', ')" }
        Click $slot["hotbar_items:$a"][0] $slot["hotbar_items:$a"][1] -Relative
        Click $slot[$inputKey][0] $slot[$inputKey][1] -Relative
        Click $slot["hotbar_items:$b"][0] $slot["hotbar_items:$b"][1] -Relative
        Click $slot[$materialKey][0] $slot[$materialKey][1] -Relative
        Start-Sleep -Milliseconds 600
        Move-Mouse $slot[$resultKey][0] $slot[$resultKey][1] -Relative
        Start-Sleep -Milliseconds 700
        Save-Screenshot $shot | Out-Null
        if (-not $vanilla) {
            # The readout of a hovered slot: the item's tooltip (first line).
            $r = Get-LogMark
            Press-Key B
            Wait-LogLine "misctweaks: narrator '(?!No item)[^']+'" 10 -Since $r | Out-Null
        }
        $t = Get-LogMark
        Click $slot[$resultKey][0] $slot[$resultKey][1] -Relative
        Click $slot["hotbar_items:$a"][0] $slot["hotbar_items:$a"][1] -Relative
        Start-Sleep -Milliseconds 600
        Move-Mouse $slot["hotbar_items:$a"][0] $slot["hotbar_items:$a"][1] -Relative
        Start-Sleep -Milliseconds 800
        return $t
    }

    $t = Use-Anvil 0 1 'anvil-mending'
    Wait-LogLine 'misctweaks: anvil removed Mending for' 10 -Since $t | Out-Null
    Save-Screenshot 'anvil-taken' | Out-Null
    Close-Screen
    Start-Sleep -Milliseconds 500
    Use-Anvil 2 3 'anvil-iron' | Out-Null
    Save-Screenshot 'anvil-repaired' | Out-Null
    Close-Screen
    Invoke-Chat '/setblock ~ ~1 ~2 air'
    Invoke-Chat '/setblock ~ ~ ~2 air'
    Invoke-Chat '/clear @s'
    Center-Player
}

# BetterElytraRocket: a rocket used in the air with an elytra on starts the flight at once.
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 elytra'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 firework_rocket 8'
Focus-Game
Press-Key 1
Invoke-Chat '/tp @s ~ ~30 ~ 0 -10'
Start-Sleep -Milliseconds 400
$m = Get-LogMark
Use-Item
Wait-LogLine 'misctweaks: elytra rocket launch' 10 -Since $m | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'rocket' | Out-Null
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 air'
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode creative'
