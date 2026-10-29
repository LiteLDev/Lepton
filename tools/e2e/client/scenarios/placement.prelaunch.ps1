# Turns the experimental VariantSelector (and its Hammer) on for the placement scenario
# (config/placement.json of the deployed mod: the whole section, the other keys at their defaults).
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = [ordered]@{
    version = 1
    enableReacharoundPlacing = $true; leniency = 0.5; whitelist = @(); blacklist = @()
    enableLockRotation = $true; lockRotationRenderLikeCrossHair = $true; showKeybind = $true
    enableVariantSelector = $true; convertVariantItems = $false; enableHammer = $true; showTooltip = $true
    alignHudToHotbar = $false; showSimpleHud = $false; showHud = $true; enableGreenTint = $true
    overrideHeldItemRender = $true; hudOffsetX = 0; hudOffsetY = 0; variantSelectorRenderLikeCrossHair = $true
    smallerArrow = $false
    variants = [ordered]@{
        variantTypes = @('slab', 'stairs', 'wall', 'fence', 'fence_gate', 'vertical_slab')
        testedMods = @('lepton'); printVariantMapToLog = $false; aliases = @('carpet=slab', 'pane=fence')
        stripCandidates = @('_planks', '_wool', '_block', 's')
        manualVariants = @('fence_gate,minecraft:oak_planks,minecraft:fence_gate')
        blacklist = @('minecraft:snow', 'minecraft:bamboo', 'minecraft:bamboo_block')
    }
    enableTrowel = $true; maxDamage = 0; trowelBlacklist = @('minecraft:air'); trowelWhitelist = @()
    enableReplaceScaffolding = $true; maxBounces = 1
    enableLongRangePickBlock = $true
    enableEnhancedLadders = $true; fallSpeed = -0.2; allowFreestanding = $true; allowDroppingDown = $true
    allowSliding = $true; allowInventorySneak = $true
}
$cfg | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $cfgDir 'placement.json') -Encoding UTF8
