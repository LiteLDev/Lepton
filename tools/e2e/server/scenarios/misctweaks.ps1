# U27 misctweaks: every misctweaks selftest (elytra rocket, campfire boost, amethyst notes, recipe
# unlock, armor stand shields, the GameNerfs toggles, spawner replacer, villager trade seeds,
# climate parameters) in one world.
$r = Invoke-SelfTest 'misctweaks.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) misctweaks selftest(s) failed" }
if ($r.Pass -lt 17) { throw "expected 17 misctweaks selftests to pass, got $($r.Pass)" }
Wait-LogLine 'misctweaks: elytra rocket launch' 5 | Out-Null
Wait-LogLine 'misctweaks: unlocked all recipes for' 5 | Out-Null
Wait-LogLine 'misctweaks: shield placed in an armor stand' 5 | Out-Null
Wait-LogLine 'misctweaks: spawner at .* minecraft:pig -> minecraft:cow' 5 | Out-Null
