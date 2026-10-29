# U11 logistics on a real client: the showcase selftest builds a chute fed by a hopper, a copper
# pipe line (with an encased pipe) carrying items into a chest, a feeding trough with a cow and a
# storage crate (64 stacks), then puts the player in front of each block. This scenario uses the
# trough and the crate with real right clicks, takes screenshots, scrolls the crate and closes the screens.
# Screenshots: out/logistics/{blocks,pipes,trough,crate,crate-scrolled}.png (+ crate-form for vanilla)
# Modes: local / remote-mod (crate overlay: count, scroll bar and buttons) and remote-vanilla
# (no overlay; the crate with more than 54 stacks first shows the row picker form).
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}
function Use-Block {
    # a right click where the player looks (no mouse movement: that would turn the camera)
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
}
$vanilla = $script:Mode -eq 'remote-vanilla'

$mark = Get-LogMark
Invoke-Chat '/quark selftest "logistics.showcase"'
Wait-LogLine 'LOGISTICS view blocks' 90 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'blocks' | Out-Null
Wait-LogLine 'LOGISTICS view pipes' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'pipes' | Out-Null

Wait-LogLine 'LOGISTICS aim trough' 30 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Use-Block
Wait-LogLine 'LOGISTICS screen trough' 15 -Since $mark | Out-Null
$problems = @()
$troughShown = $true
if (-not $vanilla) {
    # the client mod logs the dispenser screen it shows
    try { Wait-LogLine 'logistics: dispenser screen shown' 10 -Since $mark | Out-Null }
    catch { $troughShown = $false; $problems += 'the trough screen did not show on the client' }
}
Start-Sleep -Seconds 2
Save-Screenshot 'trough' | Out-Null
# without a screen, Esc would open the pause menu: the showcase closes the trough itself then
if ($troughShown) { Close-Screen }

Wait-LogLine 'LOGISTICS aim crate' 60 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Use-Block
if ($vanilla) {
    # 64 stacks: the row picker comes first; dismiss it, then the crate (now 54 stacks) opens directly
    Wait-LogLine 'LOGISTICS form crate' 15 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'crate-form' | Out-Null
    Press-Key ESC
    Wait-LogLine 'LOGISTICS aim crate again' 15 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Use-Block
    Wait-LogLine 'LOGISTICS screen crate' 15 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'crate' | Out-Null
} else {
    Wait-LogLine 'LOGISTICS screen crate' 15 -Since $mark | Out-Null
    $down = Wait-LogLine 'crate overlay: down button at' 20 -Since $mark
    Start-Sleep -Seconds 1
    Save-Screenshot 'crate' | Out-Null
    $dx, $dy = Get-Fraction $down
    Click $dx $dy -Relative
    Wait-LogLine 'LOGISTICS crate scrolled to 9' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'crate-scrolled' | Out-Null
}
Close-Screen
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) logistics\.showcase' 60 -Since $mark
if ($line -match 'FAIL') { throw "showcase selftest failed: $line" }
if ($problems) { throw ($problems -join '; ') }
