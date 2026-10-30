# U12 crystals world generation: a generated (DEFAULT) overworld; the corundum caves exist around
# the deterministic cave sources near the spawn.
#   pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario crystals-worldgen -Property level-type=DEFAULT
$r = Invoke-SelfTest 'crystals.worldgen_live' -TimeoutSeconds 600
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "crystals.worldgen_live failed" }
if (-not ((Get-ServerLog) -match 'source .* [1-9]\d* crystals')) { throw "no corundum found (is level-type DEFAULT?)" }
