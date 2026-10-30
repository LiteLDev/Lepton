# U26 mobtweaks: every mobtweaks selftest passes on BDS and each tweak logs its action line.
$mark = Get-LogMark
$r = Invoke-SelfTest 'mobtweaks.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) mobtweaks selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected 15 mobtweaks selftests to pass, got $($r.Pass)" }
foreach ($line in @(
        'mobtweaks: slime of size \d split into \d+ magma cubes',
        'mobtweaks: snow golem head Vazkii',
        'mobtweaks: \d+ vexes died with their master',
        'mobtweaks: villager zombified',
        'mobtweaks: dragon scale dropped',
        'mobtweaks: packed chest',
        'mobtweaks: sponge placed on water',
        'mobtweaks: double door'
    )) {
    Wait-LogLine $line -TimeoutSeconds 5 -Since $mark | Out-Null
}
