# U27 misctweaks world generation (see misctweaks-worldgen.prelaunch.ps1): the biome tables were
# built with the disabled climate parameters neutralised, and village placement goes through the
# spawn nerf (a structure search near the spawn finds suppressed villages).
Wait-LogLine 'misctweaks: climate control remover adjusted [1-9]\d* biome targets' 10 | Out-Null
$mark = Get-LogMark
Send-Console 'locate structure village'
Wait-LogLine 'misctweaks: village at -?\d+ -?\d+ suppressed' 120 -Since $mark | Out-Null
$r = Invoke-SelfTest 'misctweaks.climate'
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw 'misctweaks.climate failed' }
