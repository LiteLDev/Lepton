# Turns the UI toolkit demo on (config/ui.json of the deployed mod).
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
'{ "version": 1, "demo": true }' | Set-Content -Path (Join-Path $cfgDir 'ui.json') -Encoding UTF8
