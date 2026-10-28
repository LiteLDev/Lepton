# U22 throwables: every throwables.* selftest (pickarang mining/hits/return, torch arrows, parrot
# eggs, skull pikes, bottled clouds, slime buckets) on the dedicated server.
$r = Invoke-SelfTest 'throwables.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) throwables selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected 11 throwables selftests to pass, got $($r.Pass)" }
