# U25 farming on a real client: the farming.gallery selftest builds a small farm (ripe and
# simple-harvested wheat), water pink petals with 1..4 petals facing all four directions, coral fans
# on cacti and a chicken carried by the test player, then moves the player in front of each.
# Screenshots: out/farming/{farm,petals,chicken}.png. Afterwards every farming.* selftest runs
# on the integrated server.
$mark = Get-LogMark
Invoke-Chat '/quark selftest "farming.gallery"'
foreach ($view in 'farm', 'petals', 'chicken') {
    Wait-LogLine "FARMING view $view" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot $view | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) farming\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'farming.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) farming selftest(s) failed on the integrated server" }
