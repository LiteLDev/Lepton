# U1 vslabs: every vslabs.* selftest except the client gallery (placement, merging, connections,
# waterlogging, copper tools / weathering / ticks / lightning, fuel, recipes, disabling).
$mark = Get-LogMark
$r = Invoke-SelfTest 'vslabs.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) vslabs selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected at least 11 vslabs selftests to pass, got $($r.Pass)" }
# the recipe crawl ran at world load and logged its count
$line = Wait-LogLine 'vslabs: \d+ slabs-to-blocks recipes' 5
if (-not $line) { throw 'no slabs-to-blocks recipe line' }
Wait-LogLine 'vslabs: lepton:exposed_cut_copper_vertical_slab -> lepton:waxed_exposed_cut_copper_vertical_slab' 5 -Since $mark | Out-Null