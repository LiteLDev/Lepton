# U3 newstones world generation in a generated (DEFAULT) overworld: big stone clusters exist around
# the deterministic cluster sources near the spawn, and `/quark newstones sources` lists them.
#   pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario newstones-worldgen -Property level-type=DEFAULT
$r = Invoke-SelfTest 'newstones.worldgen_live' -TimeoutSeconds 900
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw 'newstones.worldgen_live failed' }
$log = Get-ServerLog
if (-not ($log -match 'not a generated overworld')) {
    if (-not ($log -match 'NEWSTONES source \w+ .*: [1-9]\d* blocks')) { throw 'no cluster blocks found (is level-type DEFAULT?)' }
}
# the generator ran for the chunks of this world
if (-not ($log -match 'generator: chunks=[1-9]')) { throw 'the generator did not run' }
