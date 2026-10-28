# U11 logistics on BDS: chutes, copper pipes, feeding troughs and storage crates (selftests with
# SimulatedPlayers, hoppers, a cow) and the log lines of the actions they trigger.
$mark = Get-LogMark
$r = Invoke-SelfTest 'logistics.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) logistics selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 logistics selftests to pass, got $($r.Pass)" }
Wait-LogLine 'feeding trough .* fed an animal' 5 -Since $mark | Out-Null
Wait-LogLine 'crate .* opened by' 5 -Since $mark | Out-Null
Wait-LogLine 'crate .* spilled' 5 -Since $mark | Out-Null
Wait-LogLine 'pipe .* broke under too many items' 5 -Since $mark | Out-Null
Wait-LogLine 'pipe .* encased' 5 -Since $mark | Out-Null
