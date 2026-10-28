# Shape gallery: the game.shapes.gallery selftest builds a row of every generated probe shape
# (stairs, slabs, vertical slabs, walls, fences, panes, gates, door, trapdoors, button, plate, logs)
# next to a vanilla stairs reference and moves the player in front of it.
# Screenshots: out/shapes/{gallery-south,gallery-east,gallery-north}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "game.shapes.gallery"'
Wait-LogLine 'GALLERY view south' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-south' | Out-Null
Wait-LogLine 'GALLERY view east' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-east' | Out-Null
Wait-LogLine 'GALLERY view north' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-north' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) game\.shapes\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'game.shapes.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) shape selftest(s) failed on the integrated server" }
