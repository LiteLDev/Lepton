# U24 frames gallery: the frames.gallery selftest builds a wall of glass frames (an apple, a
# diamond sword, a grass block, a banner, a shield, a map, rotated arrows, a torch, a bow, a lantern,
# a compass; glow frames above; empty frames; floor frames), glass frames on standing / wall /
# hanging signs, and dyed item frames in eight colours (one with a map, one with an apple, a floor
# one), then moves the player in front of each part.
# Screenshots: out/frames/{frames-glass,frames-glass-close,frames-signs,frames-dyed}.png
$mark = Get-LogMark
Invoke-Chat '/quark selftest "frames.gallery"'
foreach ($view in 'glass', 'glass-close', 'signs', 'dyed') {
    Wait-LogLine "FRAMES view $view" 90 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot "frames-$view" | Out-Null
}
$line = Wait-LogLine 'SELFTEST (PASS|FAIL) frames\.gallery' 60 -Since $mark
if ($line -match 'FAIL') { throw "gallery selftest failed: $line" }

# every frames selftest on the integrated server (local world)
$r = Invoke-SelfTest 'frames.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) frames selftest(s) failed on the integrated server" }
