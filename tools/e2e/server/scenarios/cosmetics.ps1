# U18 cosmetics on BDS: every cosmetics.* selftest (animal skins, armor stand binding, villager
# types, fisherman boats, carried chicken flags, config and rules).
$r = Invoke-SelfTest 'cosmetics.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) cosmetics selftest(s) failed" }
if ($r.Pass -lt 9) { throw "expected 9 cosmetics selftests to pass, got $($r.Pass)" }
