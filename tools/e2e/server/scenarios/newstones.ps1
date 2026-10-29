# U3 newstones on BDS (flat e2e world): every newstones.* selftest (content, gates, noise, myalite
# tint, cluster shapes and placement, permafrost style, ore pockets, trades, recipes, gallery;
# worldgen_live is skipped on a flat world, see newstones-worldgen).
$r = Invoke-SelfTest 'newstones.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) newstones selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected 14 newstones selftests to pass, got $($r.Pass)" }
if (-not ((Get-ServerLog) -match 'newstones: 74 blocks, 201 recipes')) { throw 'newstones load line missing' }
