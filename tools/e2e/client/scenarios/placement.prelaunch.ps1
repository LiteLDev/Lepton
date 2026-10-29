# Turns the experimental VariantSelector (and its Hammer) on for the placement scenario
# (config/placement.json of the deployed mod; the other keys keep their defaults).
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
'{ "version": 1, "enableVariantSelector": true, "enableHammer": true }' |
    Set-Content -Path (Join-Path $cfgDir 'placement.json') -Encoding UTF8
