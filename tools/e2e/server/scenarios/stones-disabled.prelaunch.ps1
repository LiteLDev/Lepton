# Config for the stones-disabled scenario: SturdyStone and the MoreStoneVariants bricks off.
$cfgDir = Join-Path $script:SlotDir "plugins/$script:ModName/config"
New-Item -ItemType Directory -Force $cfgDir | Out-Null
[ordered]@{
    version = 1; enableMoreStoneVariants = $true; enableBricks = $false; enableChiseledBricks = $true; enablePillar = $true
    enableRawMetalBricks = $true; enableSturdyStone = $false; enableCelebratoryLamps = $true; lightLevel = 15
} | ConvertTo-Json | Set-Content -Path (Join-Path $cfgDir 'stones.json') -Encoding utf8NoBOM
