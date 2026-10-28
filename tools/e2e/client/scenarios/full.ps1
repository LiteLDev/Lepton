# Every selftest of every unit in the local world (integrated server + client mod in one
# process), plus the companion protocol handshake with the real client.
# Works in every mode: in remote-mod the selftests run on the BDS host (chat command).
Wait-LogLine 'Lepton server handshake' 60 | Out-Null
Save-Screenshot 'world' | Out-Null
$r = Invoke-SelfTest -TimeoutSeconds 1800
if ($r.Fail -gt 0) { throw "$($r.Fail) selftest(s) failed" }
# net.client_roundtrip pinged this client (the only real player) and got the pong back.
Wait-LogLine 'SELFTEST INFO net.client_roundtrip: ping/pong round trip ok' 5 | Out-Null
Save-Screenshot 'selftest' | Out-Null
