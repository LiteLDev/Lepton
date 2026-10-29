# U7 newwoods: every newwoods.* selftest passes on BDS (blocks, recipe gating, stripping, leaf loot and
# decay, saplings and bone meal, ashen / trumpet / azalea trees, the Enchanted Fruit, loot tables,
# world generation filters, fuel and compost, gallery build).
$r = Invoke-SelfTest 'newwoods.*'
if ($r.Fail -gt 0) { throw "newwoods selftests failed: $($r.Fail)" }
if ($r.Pass -lt 16) { throw "expected 16 newwoods selftests to pass, got $($r.Pass)" }
Wait-LogLine 'newwoods: ancient tree grown' 5 | Out-Null
Wait-LogLine 'newwoods: an azalea tree grew with' 5 | Out-Null
Wait-LogLine 'newwoods: added lepton:ancient_sapling' 5 | Out-Null
