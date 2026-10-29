# U7 newwoods world generation: a generated (DEFAULT) overworld; trumpet trees stand at the columns
# the deterministic generator rolled near the spawn.
#   pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario newwoods-worldgen -Property level-type=DEFAULT
$r = Invoke-SelfTest 'newwoods.worldgen_live' -TimeoutSeconds 900
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "newwoods.worldgen_live failed" }
if (-not ((Get-ServerLog) -match 'candidate \w+_blossom at .*: [1-9]\d* logs')) { throw "no trumpet tree found (is level-type DEFAULT?)" }
