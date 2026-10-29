# U9 variantcontainers: every variantcontainers.* selftest passes on BDS (registry and recipes,
# pairing, placement, screens, lids and trapped power, hoppers, comparators, furnaces, soul state,
# donk chests, structure chests, names, config gates).
$r = Invoke-SelfTest 'variantcontainers.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) variantcontainers selftest(s) failed" }
if ($r.Pass -lt 17) { throw "expected the 17 variantcontainers selftests to pass, got $($r.Pass)" }
foreach ($line in 'structure chest at .* became lepton:birch_chest', 'a dead animal dropped lepton:birch_chest', 'opened lepton:deepslate_furnace') {
    if (-not (Get-ServerLog | Where-Object { $_ -match $line })) { throw "missing log line: $line" }
}