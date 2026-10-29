# U28 worldsurface on the flat fixture world: every worldsurface selftest (generators on built
# structures, monster box activation, Glimmering Weald blocks, config). World generation itself
# is covered by the `worldsurface-gen` scenario (normal world).
$r = Invoke-SelfTest 'worldsurface.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldsurface selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected 16 worldsurface selftests to pass, got $($r.Pass)" }
Wait-LogLine 'worldsurface: monster box at .* broke and spawned \d+ mobs' 5 | Out-Null