# U25 farming: every farming.* selftest on BDS, plus the log lines of the user-visible actions.
$mark = Get-LogMark
$r = Invoke-SelfTest 'farming.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) farming selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected 14 farming selftests, got $($r.Pass)" }
$log = Get-ServerLog -Since $mark
foreach ($pattern in @(
        'farming: .* harvested \d+ block',
        'farming: .* hoe-harvested \d+ extra block',
        'farming: .* placed pink petals on water',
        'farming: .* poisoned a baby minecraft:pig',
        'farming: .* fed a golden carrot to a pig',
        'farming: pig litter of 5',
        'farming: .* picked up a chicken',
        'farming: .* put down 1 chicken'
    )) {
    if (-not ($log | Where-Object { $_ -match $pattern })) { throw "missing log line /$pattern/" }
}
