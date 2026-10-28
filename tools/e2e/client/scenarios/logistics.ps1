# U11 logistics on a real client: the showcase selftest builds a chute fed by a hopper, a copper
# pipe line (with an encased pipe) carrying items into a chest, a feeding trough with a cow and a
# storage crate, then opens the trough and crate screens for the player.
# Screenshots: out/logistics/{blocks,pipes,trough,crate,crate-scrolled}.png
# Modes: local / remote-mod (crate overlay: count, scroll bar and buttons) and remote-vanilla
# (no overlay; the crate with more than 54 stacks first shows the row picker form).
function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
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
Wait-LogLine 'LOGISTICS screen trough' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'trough' | Out-Null
Wait-LogLine 'LOGISTICS screen crate' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 2
if ($vanilla) {
    # the row picker (65 stacks): pick the first rows, then the window opens
    Save-Screenshot 'crate-form' | Out-Null
    Press-Key ESC
} else {
    $down = Wait-LogLine 'crate overlay: down button at' 20 -Since $mark
    Save-Screenshot 'crate' | Out-Null
    $dx, $dy = Get-Fraction $down
    $m = Get-LogMark
    Click $dx $dy -Relative
    Wait-LogLine 'crate overlay: scroll down' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'crate-scrolled' | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) logistics\.showcase' 60 -Since $mark
if ($line -match 'FAIL') { throw "showcase selftest failed: $line" }
