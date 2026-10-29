# U21 mobsdeep: the mobsdeep.gallery selftest builds a row of every mob and variant (stonelings
# holding items, one with lichen; the Forgotten; black / blue / red foxhounds and a resting tame one;
# a wraith; a soul bead; a simulated player wearing the Forgotten Hat), puts the Heart of Diamond,
# the Forgotten Hat and Soul Beads in the hotbar and moves the player in front of the row.
# Screenshots: out/mobsdeep/{stonelings,mobs}.png. Then every mobsdeep selftest runs on the
# integrated server.
$mark = Get-LogMark
Invoke-Chat '/quark selftest "mobsdeep.gallery"'
Wait-LogLine 'GALLERY view stonelings' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'stonelings' | Out-Null
Wait-LogLine 'GALLERY view mobs' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'mobs' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) mobsdeep\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'mobsdeep.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobsdeep selftest(s) failed on the integrated server" }
