# U20 mobsoverworld: the gallery selftest spawns every crab colour (with and without mould), a
# raving crab, a giant crab, shibas (coats, blue collar, wolf armour, a sword in the mouth, one
# sitting) and oretoises with each ore, gives the player the unit's items and moves the camera.
# Screenshots: out/mobsoverworld/{crabs,shibas,oretoises,inventory}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "mobsoverworld.gallery"'
Wait-LogLine 'GALLERY view crabs' 90 -Since $mark | Out-Null
Start-Sleep -Seconds 5
Save-Screenshot 'crabs' | Out-Null
Wait-LogLine 'GALLERY view shibas' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'shibas' | Out-Null
Wait-LogLine 'GALLERY view oretoises' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'oretoises' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) mobsoverworld\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen
$r = Invoke-SelfTest 'mobsoverworld.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) mobsoverworld selftest(s) failed on the integrated server" }
