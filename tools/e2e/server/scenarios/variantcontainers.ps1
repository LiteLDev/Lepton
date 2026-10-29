# U9 variantcontainers: every variantcontainers.* selftest passes on BDS (chests, pairing, screens,
# hoppers, comparators, furnaces, donk chests, structure chests).
$r = Invoke-SelfTest 'variantcontainers.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) variantcontainers selftest(s) failed" }
if ($r.Pass -lt 1) { throw "expected the variantcontainers selftests to run, got $($r.Pass)" }