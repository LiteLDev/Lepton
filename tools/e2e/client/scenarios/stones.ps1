# U2 stones on a real client: the stones.gallery selftest lays out every block of the unit and
# moves the player in front of each part of it. Screenshots: out/stones/gallery-bricks-{1,2,3}.png
# (stone bricks, chiseled bricks, pillars, from the south), gallery-line2-{1,2,3}.png (polished
# calcite/dripstone, raw calcite/dripstone shapes, raw metal bricks, sturdy stone, lamps, from the
# north), gallery-lamps-night.png (the lamps light up the grass at midnight) and lamp-tooltip.png
# (the CelebratoryLamps tooltip line); then all stones.* selftests run on the integrated server.
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
$mark = Get-LogMark
Invoke-Chat '/quark selftest "stones.gallery"'
foreach ($view in 'bricks-1', 'bricks-2', 'bricks-3', 'line2-1', 'line2-2', 'line2-3', 'lamps-night') {
    # each view is held for 100 ticks (5 s)
    Wait-LogLine "STONES GALLERY view $view" 60 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot "gallery-$view" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) stones\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
Invoke-Chat '/time set noon'

# The lamp's tooltip line (CelebratoryLamps): hover hotbar slot 0 in the inventory screen (the
# position the ui scenario's demo logs for that slot at the harness's fixed 1600x900 client area).
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s lepton:stone_lamp 1'
Start-Sleep -Seconds 1
Open-Inventory
Start-Sleep -Milliseconds 800
Move-Mouse 0.5056 0.7300 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'lamp-tooltip' | Out-Null
Close-Screen

$r = Invoke-SelfTest 'stones.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) stones selftest(s) failed on the integrated server" }
