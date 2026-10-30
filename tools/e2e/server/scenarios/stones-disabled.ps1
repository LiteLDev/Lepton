# U2 stones with content off (stones-disabled.prelaunch.ps1: bricks and SturdyStone disabled): the
# world loads without their recipes and creative entries (stones.recipes checks "loaded iff
# enabled"), the rest stays.
Wait-LogLine 'stones: 69 blocks, .* bricks=off .* sturdyStone=off' 5 | Out-Null
$r = Invoke-SelfTest 'stones.recipes'
if ($r.Pass -ne 1) { throw "stones.recipes failed with content disabled" }
$r = Invoke-SelfTest 'stones.content'
if ($r.Pass -ne 1) { throw "stones.content failed with content disabled" }
