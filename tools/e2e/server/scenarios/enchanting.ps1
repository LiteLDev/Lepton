# U13 enchanting: matrix enchanting rules, table power/influence, the table flow (mod client and
# form fallback), contents dropping, ancient tomes in the anvil and loot, rune smithing and duplication.
$r = Invoke-SelfTest 'enchanting.*'
if ($r.Fail -gt 0) { throw "enchanting selftests failed: $($r.Fail)" }
if ($r.Pass -lt 18) { throw "expected 18 enchanting selftests to pass, got $($r.Pass)" }
# Quark enables one rune duplication recipe: with corundum on, the amethyst one is taken out
Wait-LogLine 'enchanting: disabled 1 rune recipe' -TimeoutSeconds 5 | Out-Null
Wait-LogLine 'enchanting: .* generated a piece' -TimeoutSeconds 5 | Out-Null
Wait-LogLine 'enchanting: .* enchanted minecraft:diamond_sword' -TimeoutSeconds 5 | Out-Null
