# U22 throwables on a real client: the throwables.gallery selftest builds a row of skull pikes
# (skeleton / zombie / creeper), clouds of shrinking sizes, hanging pickarang and flamerang models,
# a torch arrow stuck under an overhang and three parrots (adult, hatched baby, kotobirb), fills the
# hotbar with every item of the unit and moves the player in front of the row.
# Screenshots: out/throwables/{left,right,inventory}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "throwables.gallery"'
Wait-LogLine 'THROWABLES view left' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'left' | Out-Null
Wait-LogLine 'THROWABLES view right' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'right' | Out-Null
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) throwables\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }

# The gameplay selftests on the integrated server of the local world.
$r = Invoke-SelfTest 'throwables.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) throwables selftest(s) failed on the integrated server" }
