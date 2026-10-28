# Companion protocol and fallbacks on a real client. Run it in every mode:
#   -Mode local / remote-mod: the client mod completes the handshake, answers a ping, and gets the
#                             virtual container screen and a form.
#   -Mode remote-vanilla:     no handshake (the server never sends Lepton packets); the player still
#                             gets the vanilla container screen and the form fallback.
# Screenshots: out/net/{container,form}.png
$vanilla = $script:Mode -eq 'remote-vanilla'
if ($vanilla) {
    Start-Sleep -Seconds 5
    if (Get-LeviLog | Select-String 'Lepton server handshake|Lepton client mod handshake') { throw 'a vanilla client must not complete the handshake' }
} else {
    Wait-LogLine 'Lepton server handshake' 60 | Out-Null
    Wait-LogLine 'Lepton client mod handshake' 30 | Out-Null
}

$m = Get-LogMark
Invoke-Chat '/quark net'
$summary = Wait-LogLine 'Lepton net: \d+ of \d+ player' 15 -Since $m
$expect = if ($vanilla) { 'Lepton net: 0 of 1' } else { 'Lepton net: 1 of 1' }
if ($summary -notmatch [regex]::Escape($expect)) { throw "unexpected net status: $summary" }

if (-not $vanilla) {
    $m = Get-LogMark
    Invoke-Chat '/quark net ping'
    Wait-LogLine 'net: pong \d+ from' 15 -Since $m | Out-Null
}

# Virtual container: the vanilla chest screen, for mod and vanilla clients alike.
$m = Get-LogMark
Invoke-Chat '/quark debug container'
Wait-LogLine 'virtual container .* opened by' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'container' | Out-Null
Close-Screen

# Form (fallback UI): shown, then closed with Esc.
$m = Get-LogMark
Invoke-Chat '/quark debug form'
Wait-LogLine 'form test: sent to' 15 -Since $m | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'form' | Out-Null
Press-Key ESC
Wait-LogLine 'form test: (closed|clicked)' 15 -Since $m | Out-Null

$r = Invoke-SelfTest 'net.*'
if ($r.Fail -gt 0) { throw "net selftests failed" }
if (-not $vanilla) { Wait-LogLine 'SELFTEST INFO net.client_roundtrip: ping/pong round trip ok' 5 | Out-Null }
