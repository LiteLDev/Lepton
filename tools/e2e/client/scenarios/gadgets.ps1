# U23 gadgets on a real client (every mode: local, remote-mod, remote-vanilla).
# Screenshots: out/gadgets/
#   hotbar.png    item icons: abacus, pathfinder's quill, two ambience discs, Endermosh, tiny potato
#   grid.png      the 25 tiny potato looks (gadgets.gallery)
#   closeup.png   the "Dinnerbone" potato up close, with its name (HUD label / action bar)
#   items.png     held items on a potato (display entities): diamond, apple, stick, stone, a son
#   compass.png   a compass in the End pointing at the exit portal (0, 0)
#   abacus.png    a bound abacus measuring to a diamond block: count + outline
#   quill.png     a searching pathfinder's quill: "Searching..." HUD (mod) / lore (vanilla)
$vanilla = $script:Mode -eq 'remote-vanilla'
if (-not $vanilla) { Wait-LogLine 'Lepton server handshake' 60 | Out-Null }

Invoke-Chat '/clear @s'
foreach ($item in 'lepton:abacus', 'lepton:pathfinders_quill', 'lepton:music_disc_rain', 'lepton:music_disc_crickets',
    'lepton:music_disc_endermosh', 'lepton:tiny_potato') {
    Invoke-Chat "/give @s $item"
}
Start-Sleep -Seconds 2
Save-Screenshot 'hotbar' | Out-Null
Invoke-Chat '/clear @s'

$mark = Get-LogMark
Invoke-Chat '/quark selftest "gadgets.gallery"'
foreach ($view in 'grid', 'closeup', 'items', 'items_back', 'abacus', 'quill', 'compass') {
    Wait-LogLine "GALLERY view $view" 120 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot $view | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) gadgets\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }

$r = Invoke-SelfTest 'gadgets.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) gadgets selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected at least 18 gadgets selftests to pass, got $($r.Pass)" }
