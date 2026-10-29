# U24 frames: glass item frames (placing, items, rotation, punching, survival, containers behind,
# signs, maps), dyed item frames (cauldron dyeing and washing, placing, overlay, dyed drops),
# map washing and the banner layer limit, through the selftests.
$r = Invoke-SelfTest 'frames.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) frames selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected 14 frames selftests to pass, got $($r.Pass)" }

# Key log lines of the actions the tests performed.
$log = Get-ServerLog
foreach ($pattern in 'frames: installed', 'frames: .* put minecraft:diamond into a glass frame',
    'frames: .* knocked the item out of a glass frame', 'frames: a glass frame at .* lost its support',
    'frames: .* used minecraft:chest through a glass frame', 'frames: .* dyed a minecraft:frame',
    'frames: .* washed a dyed frame', 'frames: .* washed 3 map', 'frames: placed a dyed frame',
    'frames: a dyed frame at .* dropped its dyed item') {
    if (-not ($log | Select-String -Pattern $pattern -Quiet)) { throw "missing log line '$pattern'" }
}
