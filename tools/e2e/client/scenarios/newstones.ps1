# U3 newstones on a real client: the newstones.gallery selftest lays out every block of the unit and
# moves the player in front of each part. Screenshots: out/newstones/gallery-<stone>-{1,2}.png
# (limestone, jasper, shale, myalite: raw, polished, bricks with their stairs/slabs/vertical
# slabs/walls, chiseled bricks, pillar), gallery-permafrost-1.png (permafrost and its bricks with
# shapes) and gallery-myalite-tints.png (all 16 myalite tints and a wall of position-tinted myalite
# bricks); then all newstones.* selftests run on the integrated server.
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
$mark = Get-LogMark
Invoke-Chat '/quark selftest "newstones.gallery"'
$views = @('limestone-1', 'limestone-2', 'jasper-1', 'jasper-2', 'shale-1', 'shale-2', 'myalite-1', 'myalite-2', 'permafrost-1', 'myalite-tints')
foreach ($view in $views) {
    # each view is held for 100 ticks (5 s)
    Wait-LogLine "NEWSTONES GALLERY view $view" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot "gallery-$view" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) newstones\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }

$r = Invoke-SelfTest 'newstones.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) newstones selftest(s) failed on the integrated server" }
