# U20 mobsoverworld: crabs, shibas, oretoises and the Resilience effect.
# Runs the unit's selftests and checks the log lines of the main actions.
$mark = Get-LogMark
$r = Invoke-SelfTest 'mobsoverworld.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobsoverworld selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected 15 mobsoverworld selftests to pass, got $($r.Pass)" }
$lines = Get-ServerLog $mark
foreach ($want in @(
        'crab struck by lightning grows to scale 3',
        'crab caught in a bucket',
        'crab starts raving',
        'brewing mixes for the potion of stability added|resilience 1 for',
        'shiba collar dyed 11',
        'shiba picked up an arrow',
        'shiba found a dark spot',
        'oretoise ore \d mined',
        'oretoise regrew its ore',
        'oretoise stomps')) {
    if (-not ($lines | Where-Object { $_ -match $want })) { throw "missing log line: $want" }
}
Write-Host "mobsoverworld: $($r.Pass) selftests passed"
