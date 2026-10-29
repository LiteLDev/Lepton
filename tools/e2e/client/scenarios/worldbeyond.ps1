# U29 worldbeyond on a real client: the blocks (chorus weeds / twist on end stone, a potted plant,
# dusky myalite, a row of myalite crystals whose colour follows their position), then the real End
# generation (spiral spires and chorus patches) found by the end_gen selftest.
# Screenshots: out/worldbeyond/{blocks,blocks-close,inventory,end-spires,end-chorus}.png
Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/tp @s 0 -59 0 facing 0 -59 6'
Start-Sleep -Seconds 2
Invoke-Chat '/fill -4 -61 3 4 -61 7 end_stone'
Invoke-Chat '/fill -4 -60 3 4 -55 7 air'
Invoke-Chat '/setblock -3 -60 4 lepton:chorus_weeds'
Invoke-Chat '/setblock -2 -60 4 lepton:chorus_weeds'
Invoke-Chat '/setblock -1 -60 4 lepton:chorus_twist'
Invoke-Chat '/setblock 1 -60 5 lepton:dusky_myalite'
Invoke-Chat '/setblock 2 -60 5 lepton:dusky_myalite'
Invoke-Chat '/fill -4 -58 7 4 -58 7 lepton:myalite_crystal'
Invoke-Chat '/fill -4 -57 7 4 -57 7 lepton:myalite_crystal'
Invoke-Chat '/setblock 3 -60 4 flower_pot'
Start-Sleep -Seconds 2
Save-Screenshot 'blocks' | Out-Null
Invoke-Chat '/tp @s -2 -59 2 facing -2 -60 4'
Start-Sleep -Seconds 2
Save-Screenshot 'blocks-close' | Out-Null

Invoke-Chat '/give @s lepton:chorus_weeds'
Invoke-Chat '/give @s lepton:chorus_twist'
Invoke-Chat '/give @s lepton:myalite_crystal'
Invoke-Chat '/give @s lepton:dusky_myalite'
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen

# Generation in the End: the selftest finds an outer highlands chunk, generates it and checks it.
$mark = Get-LogMark
$r = Invoke-SelfTest 'worldbeyond.end_gen' -TimeoutSeconds 300
$line = Wait-LogLine 'End sample: weeds (-?\d+) (-?\d+) (-?\d+) crystal (-?\d+) (-?\d+) (-?\d+)' 10 -Since $mark
if ($line -notmatch 'weeds (-?\d+) (-?\d+) (-?\d+) crystal (-?\d+) (-?\d+) (-?\d+)') { throw "no End sample in '$line'" }
$w = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
$c = @([int]$Matches[4], [int]$Matches[5], [int]$Matches[6])
# a barrier to stand on (creative players fall), the camera a little away from the feature
function Show-From([int[]]$at, [int[]]$off, [int[]]$look) {
    $x = $at[0] + $off[0]; $y = $at[1] + $off[1]; $z = $at[2] + $off[2]
    Invoke-Chat "/execute in the_end run setblock $x $($y - 1) $z barrier"
    Invoke-Chat "/execute in the_end run tp @s $x $y $z facing $($look[0]) $($look[1]) $($look[2])"
}
Show-From $c @(-22, 6, -22) $c
Start-Sleep -Seconds 10
Show-From $c @(-22, 6, -22) $c
Start-Sleep -Seconds 3
Save-Screenshot 'end-spires' | Out-Null
Show-From $w @(3, 2, 3) $w
Start-Sleep -Seconds 6
Show-From $w @(3, 2, 3) $w
Start-Sleep -Seconds 2
Save-Screenshot 'end-chorus' | Out-Null
if ($r.Fail -gt 0) { throw "worldbeyond.end_gen failed on the integrated server" }
$r = Invoke-SelfTest 'worldbeyond.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) worldbeyond selftest(s) failed on the integrated server" }
