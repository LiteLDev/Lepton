# U18 cosmetics on a real client (local world). Screenshots: out/cosmetics/
#   gallery-<n>       rows of animal skins (vanilla + variants + shiny), pride bees, beach/ocean
#                     (zombie) villagers and the bound armor stand (player body)
#   chicken-carried   a carried chicken on the player's head (third person)
#   candles           a candle on soul sand (soul flames) next to one on stone
#   pumpkin-plain / pumpkin-cursed   the pumpkin overlay without / with Curse of Vanishing
#   buckets           axolotl and tropical fish buckets in the inventory
#   camera-*          camera mode: HUD, filters, rulers, borders, overlays
#   grass             greener grass
# Remote-vanilla: no client mod, so only the resource-pack parts (skins, villagers, armor stand,
# carried chicken) are checked; the client-mod parts are skipped.
$vanilla = $script:Mode -eq 'remote-vanilla'
$script:VK['F5'] = 0x74
$script:VK['F12'] = 0x7B

Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
Invoke-Chat '/weather clear'
Invoke-Chat '/gamemode creative'
Start-Sleep -Seconds 1

# ---- gallery ----------------------------------------------------------------------------------
# Hostile mobs (slime, zombie villagers) need a difficulty above peaceful.
Invoke-Chat '/difficulty easy'
$mark = Get-LogMark
Invoke-Chat '/quark cosmetics gallery'
$line = Wait-LogLine 'cosmetics: gallery of \d+ mobs in \d+ rows' 30 -Since $mark
$rows = if ($line -match 'in (\d+) rows') { [int]$Matches[1] } else { 8 }
Start-Sleep -Seconds 2
# The villager row first: the zombie villagers burn in daylight after a few seconds.
$order = @($rows - 1) + (0..($rows - 2))
foreach ($row in $order) {
    $m = Get-LogMark
    Invoke-Chat "/quark cosmetics view $row"
    Wait-LogLine "cosmetics: viewing gallery row $row" 20 -Since $m | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot "gallery-$row" | Out-Null
}
Invoke-Chat '/tp @s ~ ~ ~-30 0 10'
Invoke-Chat '/difficulty peaceful'
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

if (-not $vanilla) {
    # ---- quiet minecarts: two carts launched by powered rails. Track A runs over wool, then stone;
    # track B (control) only over stone. The loop sounds of the cart on A are muted over the wool and
    # audible again on the stone; the cart on B is never muted (client log lines with positions).
    $mark = Get-LogMark
    foreach ($t in @(@{ z = 3; under = 'white_wool' }, @{ z = 6; under = 'stone' })) {
        $z = $t.z
        # The cart starts on a powered rail over the tested block (powered by a redstone block next
        # to it, launched away from the stone stopper behind it).
        Invoke-Chat "/fill ~2 ~-1 ~$z ~16 ~-1 ~$z stone"
        Invoke-Chat "/fill ~3 ~-1 ~$z ~8 ~-1 ~$z $($t.under)"
        Invoke-Chat "/setblock ~3 ~ ~$($z + 1) redstone_block"
        Invoke-Chat "/setblock ~2 ~ ~$z stone"
        Invoke-Chat "/fill ~4 ~ ~$z ~16 ~ ~$z rail"
        Invoke-Chat "/setblock ~3 ~ ~$z golden_rail"
    }
    Invoke-Chat '/summon minecart ~3 ~ ~3'
    Invoke-Chat '/summon minecart ~3 ~ ~6'
    Wait-LogLine "cosmetics: minecart loop '.*' muted over wool" 30 -Since $mark | Out-Null
    # The cart rolls on to the stone part; replacing the wool too makes sure it ends up off wool even
    # if it stopped early.
    Start-Sleep -Seconds 3
    Invoke-Chat '/fill ~3 ~-1 ~3 ~8 ~-1 ~3 stone'
    Wait-LogLine "cosmetics: minecart loop '.*' audible" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    $muted = @(Get-LeviLog -Since $mark | Select-String "minecart loop '.*' muted over wool at [-0-9.]+ [-0-9.]+ ([-0-9.]+)")
    $zs = @($muted | ForEach-Object { [double]$_.Matches[0].Groups[1].Value } | ForEach-Object { [Math]::Floor($_) } | Sort-Object -Unique)
    if ($zs.Count -ne 1) { throw "expected only the cart on the wool track to be muted, muted at z = $($zs -join ', ')" }
    Invoke-Chat '/kill @e[type=minecart]'
    Write-Step "quiet minecarts: $($muted.Count) loop(s) muted over wool (z $($zs[0])), audible again on stone; the stone-only cart stayed audible"

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
}

$r = Invoke-SelfTest 'cosmetics.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) cosmetics selftest(s) failed on the integrated server" }
