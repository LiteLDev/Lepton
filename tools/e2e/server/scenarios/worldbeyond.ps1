# U29 worldbeyond: every selftest (block behaviour on test structures in the sky over the test
# strip, plus generation checks in the real End and Nether), and the generation log lines.
$mark = Get-LogMark
$r = Invoke-SelfTest 'worldbeyond.*' -TimeoutSeconds 900
if ($r.Pass -lt 22) { throw "expected 22 worldbeyond selftests to pass, got $($r.Pass) (fail $($r.Fail))" }
Wait-LogLine 'worldbeyond: decorating chunks of dimension 2' 5 -Since $mark | Out-Null
Wait-LogLine 'worldbeyond: decorating chunks of dimension 1' 5 -Since $mark | Out-Null
Wait-LogLine 'worldbeyond: viaduct of 6 crystals' 5 -Since $mark | Out-Null
Wait-LogLine 'worldbeyond: End generation checked' 5 -Since $mark | Out-Null
Wait-LogLine 'worldbeyond: Nether generation checked' 5 -Since $mark | Out-Null
