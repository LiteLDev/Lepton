# U19 emotes on a real client. Run it in every mode:
#   -Mode local / remote-mod: the emote list arrives over the companion protocol; the emotes play on
#                             the player model (front third-person view), the HUD shows the emote's
#                             icon, the chat screen gets the "Emotes" button and the emote grid.
#   -Mode remote-vanilla:     no client mod: `/quark emote <name>` still plays the animation (from
#                             the resource pack) and `/quark emote` opens the form fallback.
# Screenshots: out/emotes/{salute,cheer,tpose,custom,chat-menu,chat-hover,form}.png (the log line arrives
# about a second after the emote starts, so the shots use emotes of 2.4 s and more).
$vanilla = $script:Mode -eq 'remote-vanilla'
$local   = $script:Mode -eq 'local'

function Get-Fraction([string]$line) {
    if ($line -notmatch 'at ([0-9.]+),([0-9.]+)') { throw "no position in '$line'" }
    return [double]$Matches[1], [double]$Matches[2]
}
function Play-Emote([string]$name, [int]$waitMs, [string]$shot = $name) {
    $m = Get-LogMark
    Invoke-Chat "/quark emote $name"
    Wait-LogLine "emotes: .* plays $name" 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds $waitMs
    Save-Screenshot $shot | Out-Null
}

if (-not $vanilla) { Wait-LogLine 'emotes: \d+ emotes available' 60 | Out-Null }

# Look straight ahead and switch to the front third-person camera to see the model (the camera
# command is reliable where an F5 key press can arrive while the chat is still closing).
Invoke-Chat '/tp @s ~ ~ ~ 0 0'
Invoke-Chat '/camera @s set minecraft:third_person_front'
Start-Sleep -Milliseconds 800

try {
    Play-Emote 'salute' 300   # right hand at the forehead
    Play-Emote 'cheer' 300    # both arms up
    if ($local) {
        Play-Emote 'tpose' 600   # patreon emote, unlocked by emotes.prelaunch.ps1 (defaultEmoteTier 2)
        Play-Emote 'custom:e2e_spin' 300 'custom'   # custom emote from config/quark_emotes
    }
} finally {
    Invoke-Chat '/camera @s clear'   # back to the player's own view
    Start-Sleep -Milliseconds 400
}

if (-not $vanilla) {
    # Chat screen: the toggle button, then the grid.
    $m = Get-LogMark
    Focus-Game
    Press-Key T
    Start-Sleep -Milliseconds 900
    $toggle = Wait-LogLine 'emotes: chat buttons drawn .* toggle at' 15 -Since $m
    $tx, $ty = Get-Fraction $toggle
    Click $tx $ty -Relative
    Wait-LogLine 'emotes: menu shown' 10 -Since $m | Out-Null
    $wave = Wait-LogLine 'emotes: button wave at' 10 -Since $m
    Start-Sleep -Milliseconds 500
    Save-Screenshot 'chat-menu' | Out-Null
    $wx, $wy = Get-Fraction $wave
    Move-Mouse $wx $wy -Relative
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'chat-hover' | Out-Null
    $m2 = Get-LogMark
    Click $wx $wy -Relative
    Wait-LogLine 'emotes: requesting wave' 10 -Since $m2 | Out-Null
    Wait-LogLine 'emotes: .* plays wave' 10 -Since $m2 | Out-Null
    Close-Screen
}

# The form fallback (every client can use it).
$m = Get-LogMark
Invoke-Chat '/quark emote'
Wait-LogLine 'emotes: form sent to' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'form' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500

$r = Invoke-SelfTest 'emotes.*'
$min = if ($local) { 9 } else { 7 }
if ($r.Pass -lt $min) { throw "expected at least $min emotes selftests to pass, got $($r.Pass)" }
