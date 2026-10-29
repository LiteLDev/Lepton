# U23 gadgets: every gadgets.* selftest (abacus, pathfinder maps, ambience discs, Endermosh loot,
# compasses, tiny potato) on BDS, plus the log lines of the actions they perform.
$mark = Get-LogMark
$r = Invoke-SelfTest 'gadgets.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) gadgets selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected 16 gadgets selftests to pass, got $($r.Pass)" }
Wait-LogLine 'gadgets: abacus bound' 5 -Since $mark | Out-Null
Wait-LogLine 'gadgets: quill found' 5 -Since $mark | Out-Null
Wait-LogLine 'gadgets: a skeleton killed a minecraft:spider' 5 -Since $mark | Out-Null
Wait-LogLine 'gadgets: ambience disc rain looped' 5 -Since $mark | Out-Null
Wait-LogLine 'gadgets: Endermosh added to an End City chest' 5 -Since $mark | Out-Null
Wait-LogLine 'gadgets: tiny potato placed' 5 -Since $mark | Out-Null