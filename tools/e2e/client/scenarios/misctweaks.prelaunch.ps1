# Remote modes: the client keeps the server's resource packs in its pack cache by UUID and
# version, and every development build of Lepton's RP has the same ones: drop the cached copy so
# the client downloads this build's pack.
$packCache = Join-Path (Split-Path $script:ModsDir) 'Minecraft Bedrock\minecraftpe\packcache\resource'
if (Test-Path $packCache) {
    Get-ChildItem $packCache -Directory | Where-Object {
        $m = Join-Path $_.FullName 'manifest.json'
        (Test-Path $m) -and ((Get-Content $m -Raw) -match 'c8372485-db90-443f-ae4d-caf1c700ca44')
    } | ForEach-Object { Write-Step "dropping the cached Lepton RP $($_.Name)"; Remove-Item -Recurse -Force $_.FullName }
}
if ($script:Mode -eq 'remote-vanilla') { return } # no client mod: nothing to configure

# The deployed mod's config (local worlds use it on both sides; in remote-mod only the client
# parts apply): GameNerfs on (the overworld allowed for elytra), the chat shifted, the narrator
# readout on with keys N (readout) and U (full readout), no recipe book.
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
@'
{
  "version": 1,
  "enableBetterElytraRocket": true,
  "enableCampfiresBoostElytra": true,
  "boostStrength": 0.5,
  "maxSpeed": 1.0,
  "enableMoreNoteBlockSounds": true,
  "enableAmethystSound": true,
  "enableAutomaticRecipeUnlock": true,
  "ignoredRecipes": [],
  "forceLimitedCrafting": false,
  "disableRecipeBook": true,
  "skipEmptyRecipes": true,
  "filterRecipeAdvancements": true,
  "enableArmedArmorStands": true,
  "enableGameNerfs": true,
  "nerfMending": true,
  "noNerfForMendingTwo": false,
  "disableIronFarms": true,
  "disableIceRoads": true,
  "disableWoolDrops": true,
  "enableSelectiveMobGriefing": true,
  "enableDimensionLockedElytra": true,
  "disableFallingBlockDupe": true,
  "disablePistonPhysicsExploits": true,
  "disableMushroomBlockRemoval": true,
  "disableTripwireHookDupe": true,
  "villageSpawnNerf": false,
  "villageSpawnNerfDistance": 7000,
  "nonGriefingEntities": ["minecraft:creeper", "minecraft:enderman"],
  "elytraAllowedDimensions": ["minecraft:the_end", "minecraft:overworld"],
  "enableSpawnerReplacer": false,
  "replaceMobs": [],
  "enableVillagerRerollingRework": false,
  "seedInitialVillagerTrades": true,
  "chanceToRerollWhenRestocking": 0.25,
  "maximumRestocksPerDay": 3,
  "rerollOnAnyRestock": false,
  "rerollEvenIfNotOutOfStock": false,
  "enableMicrocraftingHelper": true,
  "enableAdjustableChat": true,
  "horizontalShift": 60,
  "verticalShift": -40,
  "enableClimateControlRemover": false,
  "disableTemperature": false,
  "disableHumidity": false,
  "disableContinentalness": false,
  "disableErosion": false,
  "disableDepth": false,
  "disableWeirdness": false,
  "disableOffset": false,
  "enableNarratorReadout": true,
  "enableOverlayShader": false,
  "shader": "none"
}
'@ | Set-Content -Path (Join-Path $cfgDir 'misctweaks.json') -Encoding UTF8
'{ "version": 1, "keys": { "narrator_readout": [78], "narrator_full_readout": [85] } }' |
    Set-Content -Path (Join-Path $cfgDir 'keybinds.json') -Encoding UTF8
