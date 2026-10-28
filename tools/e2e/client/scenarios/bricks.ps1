# U4 bricks gallery: the bricks.gallery selftest builds a row of every full block of the unit
# (bricks, soul sandstone, mud, midori, duskbound) with some shapes on top, and moves the player in
# front of it. Then the other bricks selftests run on the integrated server.
# Screenshots: out/bricks/{gallery-west,gallery-middle}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "bricks.gallery"'
Wait-LogLine 'BRICKS view west' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-west' | Out-Null
Wait-LogLine 'BRICKS view middle' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'gallery-middle' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) bricks\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'bricks.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) bricks selftest(s) failed on the integrated server" }
