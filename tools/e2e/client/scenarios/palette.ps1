# U5 palette gallery: the palette.gallery selftest builds rows of every palette block (17 shingle
# blocks + stairs, 17 framed glass blocks + panes, iron plates / slabs, iron pillars, gold bars,
# iron ladders, paper lanterns and walls, bamboo mats, nether brick fence gate, thatch, a
# waterlogged grate) and moves the player in front of them.
# Screenshots: out/palette/{shingles,glass,misc,misc-close}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "palette.gallery"'
foreach ($view in 'shingles', 'glass', 'misc', 'misc-close') {
    Wait-LogLine "PALETTE view $view" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot $view | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) palette\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
# every palette selftest on the integrated server (local world)
$r = Invoke-SelfTest 'palette.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) palette selftest(s) failed on the integrated server" }
