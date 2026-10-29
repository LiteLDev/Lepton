# U10 redstone gallery: the redstone.gallery selftest builds a row of every redstone block (iron rods in
# three facings, gravisand, an idle and a watched ender watcher, an idle and a powered randomizer, iron
# and gold buttons, the obsidian pressure plate, a randomizer outputting right) and moves the player.
# Screenshots: out/redstone/{watcher,row,floating}.png
#   watcher  - close-up of the watched ender watcher (open eye, red dust) next to the closed one
#   row      - the whole row from above (rod models, randomizer torches/top textures, buttons, plate)
#   floating - a gravisand entity floating up towards a stone block
$mark = Get-LogMark
Invoke-Chat '/quark selftest "redstone.gallery"'
Wait-LogLine 'REDSTONE GALLERY watcher' 90 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'watcher' | Out-Null
Wait-LogLine 'REDSTONE GALLERY row' 60 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'row' | Out-Null
Wait-LogLine 'REDSTONE GALLERY floating' 60 -Since $mark | Out-Null
Save-Screenshot 'floating' | Out-Null
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) redstone\.gallery' 90 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }
# every redstone selftest on the integrated server
$r = Invoke-SelfTest 'redstone.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) redstone selftest(s) failed on the integrated server" }
