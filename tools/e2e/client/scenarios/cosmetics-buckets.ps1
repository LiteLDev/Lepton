# U18 BucketsShowInhabitants only: axolotl and tropical fish buckets in the hotbar and inventory.
# Screenshots: out/cosmetics-buckets/{hotbar,inventory}.png
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics buckets'
Wait-LogLine 'cosmetics: gave \d+ buckets' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'hotbar' | Out-Null
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen
