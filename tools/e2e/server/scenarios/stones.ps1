# U2 stones: every stones.* selftest on BDS (content, config gates, recipes and creative items loaded
# per gate, sturdy stone vs pistons, lamp light, pillars, 9x smelting, tool-tier drops, gallery).
$r = Invoke-SelfTest 'stones.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) stones selftest(s) failed" }
if ($r.Pass -lt 9) { throw "expected 9 stones selftests to pass, got $($r.Pass)" }
