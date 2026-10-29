# U28 worldsurface on the client (local world): the worldsurface.gallery selftest builds every block
# of the unit (monster box with its flames, glow shroom, lichen growth, glow shroom block / stem with
# skin and pores, rings, pots, a huge glow shroom) and moves the player in front of it.
# Screenshots: out/worldsurface/{gallery-front,gallery-right,gallery-close,gallery-shroom}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "worldsurface.gallery"'
Wait-LogLine 'WSGALLERY view front' 90 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-front' | Out-Null
Wait-LogLine 'WSGALLERY view right' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-right' | Out-Null
Wait-LogLine 'WSGALLERY view close' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-close' | Out-Null
Wait-LogLine 'WSGALLERY view shroom' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-shroom' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) worldsurface\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'worldsurface.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldsurface selftest(s) failed on the integrated server" }