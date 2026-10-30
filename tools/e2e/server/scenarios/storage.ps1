# U8 storage: every storage.* selftest on BDS (content, recipes and gates, fuel, compost, block
# properties, beacon base, glass shards, stools incl. piston moves, rope incl. block moving and the
# dispenser, shear vines, potting, and the gallery layout used by the client scenario).
Wait-LogLine 'storage: ready' 60 | Out-Null
$r = Invoke-SelfTest 'storage.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) storage selftest(s) failed" }
if ($r.Pass -lt 26) { throw "expected 26 storage selftests to pass, got $($r.Pass)" }
