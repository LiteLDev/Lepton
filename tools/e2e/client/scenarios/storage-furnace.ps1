# U8 storage: can a client put a Lepton fuel block (Block of Charcoal) into a furnace's fuel slot?
# Run it locally (client mod) and with -Mode remote-vanilla (no client mod). The player opens a
# furnace in front of them and shift-clicks the charcoal block from hotbar slot 1 (quick move: the
# furnace puts fuel into its fuel slot). storage.furnace_report then logs the furnace's slots.
# Screenshots: furnace-open.png, furnace-after.png.
if (-not $script:FurnaceHotbarX) { $script:FurnaceHotbarX = 0.5055; $script:FurnaceHotbarY = 0.7306 }
$vanilla = $script:Mode -eq 'remote-vanilla'
Invoke-Chat '/clear @s'
Invoke-Chat '/time set noon'
Invoke-Chat '/setblock ~ ~ ~2 air'
Invoke-Chat '/setblock ~ ~ ~2 furnace'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 lepton:charcoal_block 4'
Invoke-Chat '/tp @s ~ ~ ~ 0 40'
Start-Sleep -Seconds 2
Focus-Game
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'furnace-open' | Out-Null
# hotbar slot 0 of the furnace screen at the harness's fixed 1280x720 client area
Move-Mouse $script:FurnaceHotbarX $script:FurnaceHotbarY -Relative
[BsE2E.Native]::Key([uint16]0x10, $false)
Start-Sleep -Milliseconds 150
Click $script:FurnaceHotbarX $script:FurnaceHotbarY -Relative
Start-Sleep -Milliseconds 150
[BsE2E.Native]::Key([uint16]0x10, $true)
Start-Sleep -Seconds 1
Save-Screenshot 'furnace-after' | Out-Null
Close-Screen
$mark = Get-LogMark
$r = Invoke-SelfTest 'storage.furnace_report'
$line = Wait-LogLine 'STORAGE FURNACE' 30 -Since $mark
Write-Step "furnace ($($script:Mode)): $line"
if ($line -notmatch 'fuel=lepton:charcoal_block') {
    if ($vanilla) { Write-Step 'vanilla client: the charcoal block did not reach the fuel slot' }
    else { throw "the charcoal block did not reach the fuel slot: $line" }
}
