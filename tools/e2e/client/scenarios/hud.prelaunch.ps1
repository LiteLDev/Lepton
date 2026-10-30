# U16 hud: binds the auto-walk keybind to G and the back keybind to B (both unbound by default;
# headless input has no side mouse buttons) in config/keybinds.json of the deployed mod.
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
'{ "version": 1, "keys": { "autorun": [71], "back": [66] } }' | Set-Content -Path (Join-Path $cfgDir 'keybinds.json') -Encoding UTF8
