# U6 woodvariants gallery: the woodvariants.gallery selftest builds every block of the unit (vertical
# planks, bookshelves, ladders, posts with chains and lanterns, stripped posts, hedges, leaf carpets,
# hollow logs on each axis) and moves the player in front of each row; then the player is put in
# front of a horizontal hollow log and the scenario holds sneak, so the auto crawl pulls the player
# into the log.
# Screenshots: out/woodvariants/{a-left,a-right,b-left,b-right,c,d-left,d-right,crawl}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "woodvariants.gallery"'
foreach ($v in 'a-left', 'a-right', 'b-left', 'b-right', 'c', 'd-left', 'd-right') {
    Wait-LogLine "GALLERY view $v" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot $v | Out-Null
}
Wait-LogLine 'GALLERY crawl$' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Press-Key SHIFT 1500
$line = Wait-LogLine 'GALLERY crawled (yes|no)' 30 -Since $mark
Save-Screenshot 'crawl' | Out-Null
if ($line -notmatch 'crawled yes') { throw "the player did not crawl into the hollow log: $line" }
Wait-LogLine 'crawled into a hollow log' 5 -Since $mark | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) woodvariants\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
$r = Invoke-SelfTest 'woodvariants.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) woodvariants selftest(s) failed on the integrated server" }
