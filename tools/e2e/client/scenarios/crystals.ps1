# U12 crystals: the crystals.gallery selftest builds every block of the unit (corundum, waxed
# corundum, sheets, lamps off / lit, clusters on every face, magnets in every orientation off /
# powered, a redirected tinted beacon beam) and moves the player in front of each part.
# Screenshots: out/crystals/crystals-<view>.png. Then every crystals selftest runs on the
# integrated server.
$mark = Get-LogMark
Invoke-Chat '/quark selftest "crystals.gallery"'
foreach ($v in 'corundum', 'sheets', 'lamps', 'clusters', 'magnets', 'beacon', 'beacon-side') {
    Wait-LogLine "GALLERY view $v" 120 -Since $mark | Out-Null
    Start-Sleep -Seconds 5
    Save-Screenshot "crystals-$v" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) crystals\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'crystals.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) crystals selftest(s) failed on the integrated server" }
