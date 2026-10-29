# U28 worldsurface world generation on a normal (non-flat) world. BDS enables mods after the spawn
# area exists, so the scenario loads new chunks with ticking areas: far away (monster boxes and
# the surface features), then at the Glimmering Weald that /quark locate finds.
function Get-GenStats {
    $mark = Get-LogMark
    Send-Console 'quark worldgen stats'
    $line = Wait-LogLine 'worldsurface gen: chunks=' 60 -Since $mark
    Write-Step "stats: $line"
    $s = @{}
    foreach ($m in [regex]::Matches($line, '(\w+)=(\d+)')) { $s[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
    return $s
}
Send-Console 'tickingarea add circle 4000 0 4000 4 lp_far true'
Wait-Seconds 30
$s = Get-GenStats
if ($s.chunks -lt 1) { throw 'no chunk was decorated by Lepton' }
if ($s.monsterBoxes -lt 1) { throw 'no monster box was generated' }
Send-Console 'tickingarea remove lp_far'

# a forest: fallen logs (1 in 5 chunks)
$mark = Get-LogMark
Send-Console 'locate biome minecraft:forest'
$hit = Wait-LogLine 'forest.*?(-?\d+), ?(-?\d+), ?(-?\d+)|forest.*?(-?\d+) \(y\?\), ?(-?\d+)' 60 -Since $mark
Write-Step "forest: $hit"
$nums = @([regex]::Matches(($hit -replace '^.*?forest', ''), '-?\d+') | ForEach-Object { [int]$_.Value })
if ($nums.Count -ge 2) {
    $fx = $nums[0]; $fz = $nums[$nums.Count - 2]
    if ($nums.Count -ge 4) { $fz = $nums[2] }
    $before = (Get-GenStats).fallenLogs
    Send-Console "tickingarea add circle $fx 64 $fz 4 lp_forest true"
    Wait-Seconds 30
    $s = Get-GenStats
    if ($s.fallenLogs -le $before) { throw "no fallen log generated around the forest at $fx $fz" }
    Send-Console 'tickingarea remove lp_forest'
} else { Write-Step 'no forest located; fallen logs not checked' }

$mark = Get-LogMark
Send-Console 'quark locate glimmering_weald'
$hit = Wait-LogLine 'worldsurface: located glimmering_weald at (-?\d+) (-?\d+) (-?\d+)' 180 -Since $mark
if ($hit -notmatch 'at (-?\d+) (-?\d+) (-?\d+)') { throw 'no Glimmering Weald located' }
$x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]
Write-Step "Glimmering Weald at $x $y $z"
Send-Console "tickingarea add circle $x $y $z 4 lp_gw true"
Wait-Seconds 30
$s = Get-GenStats
if ($s.glimmeringChunks -lt 1) { throw 'no Glimmering Weald chunk was decorated' }
if ($s.glowShrooms + $s.hugeShrooms -lt 1) { throw 'no glow shroom was generated' }
Send-Console 'tickingarea remove lp_gw'

$r = Invoke-SelfTest 'worldsurface.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldsurface selftest(s) failed" }