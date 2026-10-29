# Remote modes: the client keeps the server's resource packs in its pack cache by UUID and
# version, and every development build of Lepton's RP has the same ones: drop the cached copy so
# the client downloads this build's pack (emote animations and icons).
$packCache = Join-Path (Split-Path $script:ModsDir) 'Minecraft Bedrock\minecraftpe\packcache\resource'
if (Test-Path $packCache) {
    Get-ChildItem $packCache -Directory | Where-Object {
        $m = Join-Path $_.FullName 'manifest.json'
        (Test-Path $m) -and ((Get-Content $m -Raw) -match 'c8372485-db90-443f-ae4d-caf1c700ca44')
    } | ForEach-Object { Write-Step "dropping the cached Lepton RP $($_.Name)"; Remove-Item -Recurse -Force $_.FullName }
}
if ($script:Mode -eq 'remote-vanilla') { return } # no client mod: nothing to configure

# Unlocks the patreon emotes for everyone (defaultEmoteTier 2) and adds a custom emote, in the
# deployed mod's config (local worlds use it on both sides). The config loader needs every key.
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force (Join-Path $cfgDir 'quark_emotes') | Out-Null
@'
{
  "version": 1,
  "enableEmotes": true,
  "enabledEmotes": ["no", "yes", "wave", "salute", "cheer", "clap", "think", "point", "shrug", "headbang", "weep", "facepalm"],
  "customEmotes": ["e2e_spin"],
  "customEmoteDebug": false,
  "buttonShiftX": 0,
  "buttonShiftY": 0,
  "emoteTiers": {},
  "defaultEmoteTier": 2,
  "enablePatTheDogs": true,
  "dogsWantLove": -1,
  "petAllMobs": false,
  "pettableDenylist": ["minecraft:ender_dragon", "minecraft:wither", "minecraft:armor_stand"],
  "pettableAllowlist": []
}
'@ | Set-Content -Path (Join-Path $cfgDir 'emotes.json') -Encoding UTF8
@'
name E2E Spin
use head
use right_arm
use left_arm
animation parallel
    move head_y 4000 6.28
    move right_arm_z 500 2.5 yoyo 7 0
    move left_arm_z 500 -2.5 yoyo 7 0
end
'@ | Set-Content -Path (Join-Path $cfgDir 'quark_emotes/e2e_spin.emote') -Encoding UTF8
