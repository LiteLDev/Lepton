# U12 crystals: every crystals selftest (corundum, lamps, beacon redirection, magnets) plus the
# log lines of the user-visible actions.
$mark = Get-LogMark
$r = Invoke-SelfTest 'crystals.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) crystals selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected at least 16 crystals selftests to pass, got $($r.Pass)" }
Wait-LogLine 'crystals: \d+ magnetic items' 10 -Since $mark | Out-Null
Wait-LogLine 'crystals: beacon at .* redirected \(\d+ segments\)' 10 -Since $mark | Out-Null
