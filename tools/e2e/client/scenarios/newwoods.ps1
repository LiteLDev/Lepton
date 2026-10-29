# U7 newwoods gallery: the newwoods.gallery selftest builds every block of the unit (the azalea,
# trumpet and ashen wood sets with their variants, the leaves / saplings / hedges / leaf carpets /
# potted saplings of the six trees) and grows the ashen tree and the five trumpet trees, then moves
# the player in front of each part. The trumpet leaves shed falling leaf particles.
# Screenshots: out/newwoods/{azalea,blossom,ancient}-{left,right}.png, trees-rows-{a,b,c}.png,
# trees-{a,b}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "newwoods.gallery"'
$views = @('azalea-left', 'azalea-right', 'blossom-left', 'blossom-right', 'ancient-left', 'ancient-right',
    'trees-rows-a', 'trees-rows-b', 'trees-rows-c', 'trees-a', 'trees-b')
foreach ($v in $views) {
    Wait-LogLine "GALLERY view $v" 180 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot $v | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) newwoods\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
# the items: planks / logs / slabs are items with Java's item tags (replace_block_item, no icon): they
# must show the block like any block item; saplings, doors and the fruit have flat icons
foreach ($id in 'azalea_planks', 'ancient_log', 'stripped_blossom_wood', 'blossom_planks_slab', 'vertical_ancient_planks',
    'red_blossom_sapling', 'ancient_fruit', 'azalea_door', 'lavender_blossom_leaves') {
    Invoke-Chat "/give @s lepton:$id 8"
    Start-Sleep -Milliseconds 300
}
Start-Sleep -Seconds 2
Save-Screenshot 'hotbar' | Out-Null
Press-Key 'E' 100
Start-Sleep -Seconds 3
Save-Screenshot 'inventory' | Out-Null
Press-Key 'ESCAPE' 100
Start-Sleep -Seconds 1
$r = Invoke-SelfTest 'newwoods.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) newwoods selftest(s) failed on the integrated server" }
