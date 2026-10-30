# U21 mobsdeep: Stonelings, Forgotten, Foxhound, Wraith and the Soul Bead (every mobsdeep selftest).
$mark = Get-LogMark
$r = Invoke-SelfTest 'mobsdeep.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobsdeep selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected 13 mobsdeep selftests, got $($r.Pass)" }
foreach ($line in @('mobsdeep: stoneling made \(andesite\)', 'mobsdeep: stoneling tamed', 'mobsdeep: stoneling poofed',
        'mobsdeep: forgotten switched to sword', 'mobsdeep: foxhound tamed by', 'mobsdeep: foxhound speeds up a furnace',
        'mobsdeep: wraith aggroed', 'mobsdeep: soul bead -> ')) {
    Wait-LogLine $line 5 -Since $mark | Out-Null
}
