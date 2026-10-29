# U9 variantcontainers gallery: the variantcontainers.gallery selftest builds every chest, trapped
# chest, lid frame, double chest and furnace in front of the player; the chest, double chest and
# furnace screens are opened with real right clicks.
# Screenshots: out/variantcontainers/{chests-left,chests-right,trapped-left,trapped-right,lids,
# lids-back,furnaces,screen-chest,screen-double,screen-furnace}.png
# Modes: local, remote-mod and remote-vanilla (the furnace screen through a stand-in furnace block).
function Use-Block {
    # a right click where the player looks (no mouse movement: that would turn the camera)
    Assert-GameForeground
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
}
# Crafting on the real client: 8 equal planks / stones in a crafting table (real clicks) give the
# variant (spruce, oak, nether brick chests, deepslate furnace); mixed planks give the vanilla chest.
# The client picks the recipe, so this checks the recipe priorities. Screenshots: craft-<case>.png
# (the result slot and the recipe book). The slot positions come from the client mod (local mode)
# and are reused by the vanilla client (same window size).
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}
function Click-Slot($at, [switch]$Right) { Click $at[0] $at[1] -Relative -Right:$Right; Start-Sleep -Milliseconds 250 }
$vanilla = $script:Mode -eq 'remote-vanilla'
$slotFile = Join-Path $PSScriptRoot '../out/variantcontainers-craft-slots.json'
$failures = @()
if (-not $vanilla) {
    Invoke-Chat '/quark selftest "variantcontainers.craft_arm"'
    Start-Sleep -Seconds 2
}
Invoke-Chat '/clear @s'
Invoke-Chat '/execute as @s at @s align xyz run tp @s ~0.5 ~ ~0.5 0 0'
Start-Sleep -Milliseconds 500
Invoke-Chat '/setblock ~ ~ ~2 stone'
Invoke-Chat '/setblock ~ ~1 ~2 crafting_table'
Start-Sleep -Seconds 1
$cases = @(
    @{ Name = 'spruce'; A = 'spruce_planks'; B = 'spruce_planks'; Want = 'lepton:spruce_chest' },
    @{ Name = 'oak'; A = 'oak_planks'; B = 'oak_planks'; Want = 'lepton:oak_chest' },
    @{ Name = 'nether-brick'; A = 'nether_brick'; B = 'nether_brick'; Want = 'lepton:nether_brick_chest' },
    @{ Name = 'deepslate'; A = 'cobbled_deepslate'; B = 'cobbled_deepslate'; Want = 'lepton:deepslate_furnace' },
    @{ Name = 'mixed'; A = 'oak_planks'; B = 'birch_planks'; Want = 'minecraft:chest' }
)
$slots = $null
if ($vanilla) {
    if (-not (Test-Path $slotFile)) { throw "run the local mode first: it records the crafting slot positions ($slotFile)" }
    $slots = Get-Content $slotFile -Raw | ConvertFrom-Json -AsHashtable
}
foreach ($c in $cases) {
    Invoke-Chat '/clear @s'
    Invoke-Chat "/replaceitem entity @s slot.hotbar 0 $($c.A) 7"
    Invoke-Chat "/replaceitem entity @s slot.hotbar 1 $($c.B) 1"
    Start-Sleep -Seconds 1
    $m = Get-LogMark
    Use-Block
    if ($vanilla) {
        Start-Sleep -Seconds 3
    } else {
        Wait-LogLine 'variantcontainers: crafting screen' 15 -Since $m | Out-Null
        if (-not $slots) {
            $slots = @{}
            foreach ($l in (Get-LeviLog -Since $m)) {
                if ($l -match 'variantcontainers: slot (\S+):(\d+) at ([0-9.]+),([0-9.]+)') { $slots["$($Matches[1]):$($Matches[2])"] = @([double]$Matches[3], [double]$Matches[4]) }
            }
            Write-Host "[variantcontainers] crafting slots: $(($slots.Keys | Sort-Object) -join ', ')"
            $slots | ConvertTo-Json | Set-Content $slotFile
        }
        Start-Sleep -Milliseconds 500
    }
    $gridKey = $slots.Keys | Where-Object { $_ -match '^(crafting_input_items|crafting_items|.*input.*):0$' } | Select-Object -First 1
    $resKey = $slots.Keys | Where-Object { $_ -match '(output|result|created)' } | Select-Object -First 1
    if (-not $gridKey -or -not $resKey) { throw "crafting slots not found in $(($slots.Keys | Sort-Object) -join ', ')" }
    $gridColl = $gridKey.Split(':')[0]
    # the 7 + 1 items around the middle: the odd one in the top left
    Click-Slot $slots['hotbar_items:0']
    foreach ($i in 1, 2, 3, 5, 6, 7, 8) { Click-Slot $slots["${gridColl}:$i"] -Right }
    Click-Slot $slots['hotbar_items:0']
    Click-Slot $slots['hotbar_items:1']
    Click-Slot $slots["${gridColl}:0"]
    Start-Sleep -Milliseconds 800
    Move-Mouse $slots[$resKey][0] $slots[$resKey][1] -Relative
    Start-Sleep -Milliseconds 800
    Save-Screenshot "craft-$($c.Name)" | Out-Null
    Click-Slot $slots[$resKey]
    Click-Slot $slots['hotbar_items:5']
    Start-Sleep -Milliseconds 500
    Close-Screen
    $m = Get-LogMark
    Invoke-Chat '/quark selftest "variantcontainers.report"'
    Wait-LogLine 'VCINV done' 30 -Since $m | Out-Null
    $got = @(Get-LeviLog -Since $m | Where-Object { $_ -match 'VCINV 5 (\S+)' } | ForEach-Object { if ($_ -match 'VCINV 5 (\S+)') { $Matches[1] } })
    Write-Host "[variantcontainers] craft $($c.Name): $($got -join ',')"
    if ($got -notcontains $c.Want) { $failures += "$($c.Name) crafted '$($got -join ',')' instead of $($c.Want)" }
}
Invoke-Chat '/clear @s'
Invoke-Chat '/setblock ~ ~1 ~2 air'
if ($failures.Count) { throw ($failures -join '; ') }
$mark = Get-LogMark
Invoke-Chat '/quark selftest "variantcontainers.gallery"'
foreach ($shot in 'chests-left', 'chests-right', 'trapped-left', 'trapped-right', 'lids', 'lids-back', 'furnaces') {
    Wait-LogLine "VCGALLERY $shot" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
}
$blocks = @{ chest = 'lepton:oak_chest'; double = 'lepton:oak_chest'; furnace = 'lepton:deepslate_furnace' }
foreach ($screen in 'chest', 'double', 'furnace') {
    $aim = Wait-LogLine "VCGALLERY aim $screen" 60 -Since $mark
    if (-not $aim) { throw "the gallery never aimed at the $screen" }
    $at = Get-LogMark
    Start-Sleep -Milliseconds 800
    Use-Block
    $opened = Wait-LogLine "variantcontainers: .* opened $($blocks[$screen])" 20 -Since $at
    if (-not $opened) { throw "clicking the $screen did not open it" }
    Start-Sleep -Seconds 3
    Save-Screenshot "screen-$screen" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) variantcontainers\.gallery' 120 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
if ($script:Mode -ne 'remote-vanilla' -and $script:Mode -ne 'remote-mod') {
    $r = Invoke-SelfTest 'variantcontainers.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) variantcontainers selftest(s) failed on the integrated server" }
}