# U17 placement: every placement selftest on BDS (reacharound, rotation lock, variants, hammer,
# trowel, scaffolding, long-range pick, enhanced ladders) and the log lines of the actions.
$mark = Get-LogMark
$r = Invoke-SelfTest 'placement.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) placement selftest(s) failed" }
if ($r.Pass -lt 20) { throw "expected at least 20 placement selftests to pass, got $($r.Pass)" }
Wait-LogLine 'placement: reacharound placed minecraft:cobblestone' 5 -Since $mark | Out-Null
Wait-LogLine 'placement: trowel placed minecraft:mossy_cobblestone' 5 -Since $mark | Out-Null
Wait-LogLine 'placement: replaced scaffolding' 5 -Since $mark | Out-Null
Wait-LogLine 'placement: dropped minecraft:ladder down' 5 -Since $mark | Out-Null
Wait-LogLine 'placement: variant map built' 5 -Since $mark | Out-Null
