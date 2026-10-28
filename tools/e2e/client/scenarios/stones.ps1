# U2 stones on a real client: the stones.gallery selftest lays out every block of the unit and
# moves the player in front of it. Screenshots: out/stones/{gallery-south,gallery-east,gallery-north,
# gallery-night}.png (night: the lamps light up their surroundings), then all stones.* selftests run
# on the integrated server.
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
$mark = Get-LogMark
Invoke-Chat '/quark selftest "stones.gallery"'
Wait-LogLine 'STONES GALLERY view south' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 5
Save-Screenshot 'gallery-south' | Out-Null
Wait-LogLine 'STONES GALLERY view top' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-east' | Out-Null
Wait-LogLine 'STONES GALLERY view north' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-north' | Out-Null
Invoke-Chat '/time set midnight'
Start-Sleep -Seconds 3
Save-Screenshot 'gallery-night' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) stones\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
Invoke-Chat '/time set noon'
$r = Invoke-SelfTest 'stones.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) stones selftest(s) failed on the integrated server" }
