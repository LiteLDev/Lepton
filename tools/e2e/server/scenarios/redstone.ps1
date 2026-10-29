# U10 redstone: every redstone.* selftest on BDS (blocks, recipes, piston structure with iron rods /
# chains / push limit / block entity rules, gravisand falling up and down + comparator, metal buttons,
# obsidian plate, randomizer, ender watcher, block-placing dispensers), plus the key log lines.
$mark = Get-LogMark
$r = Invoke-SelfTest 'redstone.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) redstone selftest(s) failed" }
if ($r.Pass -lt 22) { throw "expected 22 redstone selftests to pass, got $($r.Pass)" }
Wait-LogLine 'redstone: gravisand comparator output installed' 5 | Out-Null
Wait-LogLine 'redstone: gravisand at .* falls up' 5 -Since $mark | Out-Null
Wait-LogLine 'redstone: randomizer at .* -> left' 5 -Since $mark | Out-Null
Wait-LogLine 'redstone: ender watcher at .* watched' 5 -Since $mark | Out-Null
Wait-LogLine 'redstone: dispenser placed minecraft:wheat' 5 -Since $mark | Out-Null
