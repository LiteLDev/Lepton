# U14 backpack: Backpack, Totem of Holding and Seed Pouch selftests (docs/spec/backpack.md §8).
$mark = Get-LogMark
$r = Invoke-SelfTest 'backpack.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) backpack selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected 15 backpack selftests to pass, got $($r.Pass)" }
# The user-visible actions are logged once each.
foreach ($line in 'backpack: opened by', 'backpack: closed by', "backpack: .* can't take off a backpack with items",
    'backpack: a dropped backpack spilled its contents', 'backpack: .* swapped backpacks with an armor stand',
    'totem: .* spawned for', 'totem: .* collected', 'totem: .* faded', 'totem: .* shattered', 'seed pouch: .* used') {
    $null = Wait-LogLine $line -TimeoutSeconds 5 -Since $mark
}
