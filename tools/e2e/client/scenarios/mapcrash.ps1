# Regression check: using an empty map in a local world creates a filled map, and the game keeps
# running while the new map is held, shown in the inventory and hovered (tooltip; with Lepton, the
# hud map preview). Reading the map id through MapItem::getMapId used to corrupt the new map's
# data and crash the game a moment later. Works with and without Lepton (run.ps1 -WithoutMod is the
# vanilla baseline). Screenshots: out/mapcrash/*.png
$crashDir  = Join-Path (Split-Path $script:ModsDir) 'logs\crash'
$startedAt = Get-Date
$withMod   = -not $WithoutMod
function Assert-StillRunning([string]$step) {
    Start-Sleep -Milliseconds 500
    $new  = @(Get-ChildItem $crashDir -Filter 'trace_*.log' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $startedAt })
    $game = Get-GameProcess
    if ($game -and -not $game.Responding) {
        Start-Sleep -Seconds 5
        $game.Refresh()
        if (-not $game.Responding) { throw "the game stopped responding $step" }
    }
    if (-not $game -or $new.Count -gt 0) {
        throw "the game crashed $step$(if ($new) { " (crash log: $($new[-1].FullName))" })"
    }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 empty_map'
Invoke-Chat '/tp @s ~ ~ ~ 0 0'
Press-Key 1
Start-Sleep -Seconds 1
Save-Screenshot 'empty-map' | Out-Null

# Use the empty map (right click): it becomes a filled map.
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 3
Assert-StillRunning 'after using the empty map'
Save-Screenshot 'map-held' | Out-Null

$m = Get-LogMark
Open-Inventory
Assert-StillRunning 'opening the inventory with the new map'
# Hotbar slot 0 of the inventory screen: with Lepton the position the hud unit logs for it,
# otherwise the one measured at the harness's fixed 1600x900 client area.
$hoverAt = @(0.5056, 0.7300)
if ($withMod) {
    $line = Wait-LogLine 'hud: inventory_screen hotbar slots at' 20 -Since $m
    if ($line -match 'slots at ([0-9.]+),([0-9.]+)') { $hoverAt = @([double]$Matches[1], [double]$Matches[2]) }
}
# The game sometimes misses a single hover move: try again until the hud draws the map preview.
for ($try = 1; ; $try++) {
    Move-Mouse 0.5 0.05 -Relative
    Start-Sleep -Milliseconds 300
    Move-Mouse $hoverAt[0] $hoverAt[1] -Relative
    Start-Sleep -Seconds 2
    Assert-StillRunning 'hovering the new map'
    if (-not $withMod) { break }
    try { Wait-LogLine 'hud: tooltip drawn' 3 -Since $m | Out-Null; break } catch { if ($try -ge 4) { throw } }
}
Save-Screenshot 'map-hover' | Out-Null
Close-Screen
Start-Sleep -Seconds 2
Assert-StillRunning 'after closing the inventory'
