# U9 variantcontainers gallery: the variantcontainers.gallery selftest builds every chest, trapped
# chest, lid frame, double chest and furnace in front of the player and opens the screens.
# Screenshots: out/variantcontainers/{chests-left,chests-right,trapped-left,trapped-right,lids,
# lids-back,furnaces,chest-screen,double-screen,furnace-screen}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "variantcontainers.gallery"'
foreach ($shot in 'chests-left', 'chests-right', 'trapped-left', 'trapped-right', 'lids', 'lids-back', 'furnaces', 'chest-screen', 'double-screen', 'furnace-screen') {
    Wait-LogLine "VCGALLERY $shot" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) variantcontainers\.gallery' 120 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'variantcontainers.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) variantcontainers selftest(s) failed on the integrated server" }