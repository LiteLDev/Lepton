# U1 vslabs gallery: the vslabs.gallery selftest builds every vertical slab (rows alternating
# north / east / double), walls / panes / bars / fences against a slab and a waterlogged slab, and
# moves the player to three viewpoints.
# Screenshots: out/vslabs/{rows,connections,water}.png, then the vslabs.* selftests on the
# integrated server.
$mark = Get-LogMark
Invoke-Chat '/quark selftest "vslabs.gallery"'
foreach ($v in 'rows', 'connections', 'water') {
    Wait-LogLine "VSLABS view $v" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 5
    Save-Screenshot $v | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) vslabs\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'vslabs.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) vslabs selftest(s) failed on the integrated server" }