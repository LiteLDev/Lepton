# U15 inventory: every inventory selftest (sorting, transfer, quick move, hotbar changer, tool
# restock, quick armour swapping, item interactions, held shulker boxes, search, sharing).
$mark = Get-LogMark
$r = Invoke-SelfTest 'inventory.*'
if ($r.Fail -gt 0) { throw "inventory selftests failed: $($r.Fail)" }
if ($r.Pass -lt 12) { throw "expected 12 inventory selftests to pass, got $($r.Pass)" }
Wait-LogLine 'inventory: sorted a container for' 10 -Since $mark | Out-Null
Wait-LogLine 'inventory: .* closed a held shulker box' 10 -Since $mark | Out-Null
