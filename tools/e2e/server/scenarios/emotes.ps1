# U19 emotes: Emotes (validation, animation broadcast, mod-client messages, vanilla form) and
# PatTheDogs (petting wolves and other mobs, dogs wanting love) through the selftests.
$r = Invoke-SelfTest 'emotes.*'
if ($r.Pass -lt 7) { throw "expected 7 emotes selftests to pass, got $($r.Pass)" }

# Key log lines of the actions the tests performed.
$log = Get-ServerLog
foreach ($pattern in 'emotes: .* plays wave', 'emotes: .* plays custom:My_Dance-2', 'emotes: form sent to', 'pat: .* pets a minecraft:wolf', 'pat: .* pets a minecraft:pig') {
    if (-not ($log | Select-String -Pattern $pattern -Quiet)) { throw "missing log line '$pattern'" }
}
