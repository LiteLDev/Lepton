# U4 bricks: every brick / soul sandstone / mud / midori / duskbound block and item is registered,
# the config gate loads the right recipes, and the tag behaviours work (soul fire, soul speed,
# infiniburn, dragon immunity). The gallery test only places blocks here (no viewer on BDS).
$r = Invoke-SelfTest 'bricks.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) bricks selftest(s) failed" }
if ($r.Pass -lt 10) { throw "expected 10 bricks selftests to pass, got $($r.Pass)" }
Wait-LogLine 'bricks: \d+ recipes and \d+ creative entries disabled by config' 10 | Out-Null
