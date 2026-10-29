# Unlocks the patreon emotes for everyone (defaultEmoteTier 2) and adds a custom emote, in the
# deployed mod's config (local worlds use it on both sides).
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force (Join-Path $cfgDir 'quark_emotes') | Out-Null
'{ "version": 1, "defaultEmoteTier": 2, "customEmotes": ["e2e_spin"] }' | Set-Content -Path (Join-Path $cfgDir 'emotes.json') -Encoding UTF8
@'
name E2E Spin
use head
use right_arm
use left_arm
animation parallel
    move head_y 1000 6.28
    move right_arm_z 500 1.57 yoyo 1 0
    move left_arm_z 500 -1.57 yoyo 1 0
end
'@ | Set-Content -Path (Join-Path $cfgDir 'quark_emotes/e2e_spin.emote') -Encoding UTF8
