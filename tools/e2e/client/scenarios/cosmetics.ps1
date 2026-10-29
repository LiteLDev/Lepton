# U18 cosmetics on a real client (local world). Screenshots: out/cosmetics/
#   gallery-<n>       rows of animal skins (vanilla + variants + shiny), pride bees, beach/ocean
#                     (zombie) villagers and the bound armor stand (player body)
#   chicken-carried   a carried chicken on the player's head (third person)
#   candles           a candle on soul sand (soul flames) next to one on stone
#   pumpkin-plain / pumpkin-cursed   the pumpkin overlay without / with Curse of Vanishing
#   buckets           axolotl and tropical fish buckets in the inventory
#   camera-*          camera mode: HUD, filters, rulers, borders, overlays
#   grass             greener grass
$script:VK['F5'] = 0x74
$script:VK['F12'] = 0x7B

Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
Invoke-Chat '/weather clear'
Invoke-Chat '/gamemode creative'
Start-Sleep -Seconds 1

# ---- gallery ----------------------------------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics gallery'
$line = Wait-LogLine 'cosmetics: gallery of \d+ mobs in \d+ rows' 30 -Since $mark
$rows = if ($line -match 'in (\d+) rows') { [int]$Matches[1] } else { 8 }
Start-Sleep -Seconds 3
for ($row = 0; $row -lt $rows; $row++) {
    $m = Get-LogMark
    Invoke-Chat "/quark cosmetics view $row"
    Wait-LogLine "cosmetics: viewing gallery row $row" 20 -Since $m | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot "gallery-$row" | Out-Null
}
Invoke-Chat '/tp @s ~ ~ ~-30 0 10'
Start-Sleep -Seconds 1

# ---- carried chicken (third person) -------------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics carry'
Wait-LogLine 'cosmetics: chicken carried: true' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Press-Key F5
Start-Sleep -Seconds 2
Save-Screenshot 'chicken-carried-back' | Out-Null
Press-Key F5
Start-Sleep -Seconds 2
Save-Screenshot 'chicken-carried' | Out-Null
Press-Key F5
Reset-CameraPerspective
Invoke-Chat '/ride @s evict_riders'
Start-Sleep -Seconds 1

# ---- soul candles -------------------------------------------------------------------------------
Invoke-Chat '/time set midnight'
Invoke-Chat '/setblock ~1 ~-1 ~3 soul_sand'
Invoke-Chat '/setblock ~1 ~ ~3 candle ["lit"=true]'
Invoke-Chat '/setblock ~-1 ~-1 ~3 stone'
Invoke-Chat '/setblock ~-1 ~ ~3 candle ["lit"=true]'
Invoke-Chat '/tp @s ~ ~ ~ 0 25'
Start-Sleep -Seconds 3
Save-Screenshot 'candles' | Out-Null
Invoke-Chat '/time set noon'

# ---- pumpkin overlay ----------------------------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics pumpkin false'
Wait-LogLine 'cosmetics: wearing a carved pumpkin' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'pumpkin-plain' | Out-Null
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics pumpkin true'
Wait-LogLine 'cosmetics: wearing a cursed carved pumpkin' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'pumpkin-cursed' | Out-Null
Invoke-Chat '/replaceitem entity @s slot.armor.head 0 air'

# ---- buckets ------------------------------------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/clear @s'
Invoke-Chat '/quark cosmetics buckets'
Wait-LogLine 'cosmetics: gave \d+ buckets' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'buckets-hotbar' | Out-Null
Open-Inventory
Save-Screenshot 'buckets' | Out-Null
Close-Screen

# ---- greener grass ------------------------------------------------------------------------------
Invoke-Chat '/tp @s ~ ~ ~ 180 30'
Start-Sleep -Seconds 2
Save-Screenshot 'grass' | Out-Null

# ---- camera mode --------------------------------------------------------------------------------
Invoke-Chat '/tp @s ~ ~ ~ 0 10'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Press-Key F12
Wait-LogLine 'cosmetics: camera mode on' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'camera-hud' | Out-Null
# 1: grayscale, then sepia
$mark = Get-LogMark
Press-Key 1
Wait-LogLine 'cosmetics: camera filter=grayscale' 10 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'camera-grayscale' | Out-Null
Press-Key 1
Start-Sleep -Seconds 1
Save-Screenshot 'camera-sepia' | Out-Null
# Through the list to Pixelize (index 14).
for ($i = 3; $i -le 14; $i++) { Press-Key 1 }
Start-Sleep -Seconds 1
Save-Screenshot 'camera-pixelize' | Out-Null
# Rulers: rule of thirds; borders: 16:9 then polaroid; overlays: date, postcard.
Press-Key 2
Press-Key 3
Press-Key 3
Press-Key 3
Press-Key 4
Start-Sleep -Seconds 1
Save-Screenshot 'camera-thirds-169-date' | Out-Null
Press-Key 3
Press-Key 3
Press-Key 4
Start-Sleep -Seconds 1
Save-Screenshot 'camera-polaroid-postcard' | Out-Null
$mark = Get-LogMark
Press-Key ENTER
Wait-LogLine 'cosmetics: screenshot saved' 20 -Since $mark | Out-Null
$mark = Get-LogMark
Press-Key F12
Wait-LogLine 'cosmetics: camera mode off' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'camera-off' | Out-Null

$r = Invoke-SelfTest 'cosmetics.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) cosmetics selftest(s) failed on the integrated server" }
