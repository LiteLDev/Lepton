# U8 storage on a real client: the storage.gallery selftest lays out every block of the unit and
# moves the player in front of each part while the test's simulated player sits on a stool.
# Screenshots (out/storage/): gallery-compressed-{1,2}.png (crates, sacks, bundles, charcoal block,
# blaze lantern, bonded leather / hide, dirty glass and panes, from the north), gallery-stools.png
# (the 16 stools, one as a table, a seated player), gallery-rope-vine.png (a rope column under a
# stone beam and cut vines on a wall), gallery-potted-{1,2}.png (the 25 potted plants); then the
# glass shard / rope items in the hotbar (items.png) and all storage.* selftests on the integrated
# server.
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
$mark = Get-LogMark
Invoke-Chat '/quark selftest "storage.gallery"'
foreach ($view in 'compressed-1', 'compressed-2', 'stools', 'rope-vine', 'potted-1', 'potted-2') {
    # each view is held for 100 ticks (5 s)
    Wait-LogLine "STORAGE GALLERY view $view" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot "gallery-$view" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) storage\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }

# item icons: shards, rope, a stool and a crate in the hotbar
Invoke-Chat '/clear @s'
foreach ($id in 'lepton:clear_shard', 'lepton:red_shard', 'lepton:rope', 'lepton:red_stool', 'lepton:charcoal_block',
    'lepton:apple_crate', 'lepton:stick_block', 'lepton:glowberry_sack', 'lepton:dirty_glass') {
    Invoke-Chat "/give @s $id 1"
}
Start-Sleep -Seconds 2
Save-Screenshot 'items' | Out-Null

$r = Invoke-SelfTest 'storage.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) storage selftest(s) failed on the integrated server" }
