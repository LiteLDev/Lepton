# U16 hud: the shared logic of the HUD / tooltip modules (usage ticker counts and animation,
# food, fuel, shulker contents, enchanting targets, attributes, elytra) on BDS.
$r = Invoke-SelfTest 'hud.*'
if ($r.Fail -gt 0) { throw "hud selftests failed: $(($r.Failed | ForEach-Object { "$($_.Name): $($_.Reason)" }) -join '; ')" }
if ($r.Pass -lt 11) { throw "expected at least 11 hud selftests, got $($r.Pass)" }
