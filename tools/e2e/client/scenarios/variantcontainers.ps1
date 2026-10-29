# U9 variantcontainers gallery: the variantcontainers.gallery selftest builds every chest, trapped
# chest, lid frame, double chest and furnace in front of the player; the chest, double chest and
# furnace screens are opened with real right clicks.
# Screenshots: out/variantcontainers/{chests-left,chests-right,trapped-left,trapped-right,lids,
# lids-back,furnaces,screen-chest,screen-double,screen-furnace}.png
# Modes: local, remote-mod and remote-vanilla (the furnace screen through a stand-in furnace block).
function Use-Block {
    # a right click where the player looks (no mouse movement: that would turn the camera)
    Assert-GameForeground
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
}
$mark = Get-LogMark
Invoke-Chat '/quark selftest "variantcontainers.gallery"'
foreach ($shot in 'chests-left', 'chests-right', 'trapped-left', 'trapped-right', 'lids', 'lids-back', 'furnaces') {
    Wait-LogLine "VCGALLERY $shot" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
}
$blocks = @{ chest = 'lepton:oak_chest'; double = 'lepton:oak_chest'; furnace = 'lepton:deepslate_furnace' }
foreach ($screen in 'chest', 'double', 'furnace') {
    $aim = Wait-LogLine "VCGALLERY aim $screen" 60 -Since $mark
    if (-not $aim) { throw "the gallery never aimed at the $screen" }
    $at = Get-LogMark
    Start-Sleep -Milliseconds 800
    Use-Block
    $opened = Wait-LogLine "variantcontainers: .* opened $($blocks[$screen])" 20 -Since $at
    if (-not $opened) { throw "clicking the $screen did not open it" }
    Start-Sleep -Seconds 3
    Save-Screenshot "screen-$screen" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) variantcontainers\.gallery' 120 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
if ($script:Mode -ne 'remote-vanilla' -and $script:Mode -ne 'remote-mod') {
    $r = Invoke-SelfTest 'variantcontainers.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) variantcontainers selftest(s) failed on the integrated server" }
}