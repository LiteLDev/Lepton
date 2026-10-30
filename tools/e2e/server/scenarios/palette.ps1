# U5 palette on BDS: every palette.* selftest (blocks, recipes and their disabled-module block,
# placement, thatch fall cushioning and composting, grate physics, ladder support, bamboo mats,
# fortress gold bars, gallery build) plus the module's ready line.
Wait-LogLine 'palette: ready' 60 | Out-Null
$r = Invoke-SelfTest 'palette.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) palette selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected 14 palette selftests to pass, got $($r.Pass)" }
