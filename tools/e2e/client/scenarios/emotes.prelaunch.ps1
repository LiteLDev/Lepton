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
