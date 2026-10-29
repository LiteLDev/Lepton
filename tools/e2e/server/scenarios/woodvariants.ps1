# U6 woodvariants: every woodvariants.* selftest passes on BDS (blocks, recipe gating, posts,
# hedges, ladders, leaf carpets, bookshelf power, hollow logs and auto crawl, fuel).
$r = Invoke-SelfTest 'woodvariants.*'
if ($r.Fail -gt 0) { throw "woodvariants selftests failed: $($r.Fail)" }
if ($r.Pass -lt 16) { throw "expected 16 woodvariants selftests to pass, got $($r.Pass)" }
