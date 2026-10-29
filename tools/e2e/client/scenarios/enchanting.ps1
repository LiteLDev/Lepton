# U13 enchanting on a real client. Run with -Mode local, remote-mod and remote-vanilla.
#   items:   the Ancient Tome and the Rune template in the hotbar (textures, glint)
#   matrix:  `/quark enchanting demo` builds a matrix enchanter (bookshelves, a candle, a sword with
#            pieces) and opens it: the native matrix screen (mod clients) or the form (vanilla)
#   tooltip: the tome's tooltip line (mod clients)
# Screenshots: out/enchanting/{items,matrix,matrix-hover,after}.png
$vanilla = $script:Mode -eq 'remote-vanilla'

Invoke-Chat '/clear @s'
Invoke-Chat '/give @s lepton:ancient_tome'
Invoke-Chat '/give @s lepton:smithing_template_rune 3'
Invoke-Chat '/quark enchanting tome sharpness'
Invoke-Chat '/give @s lapis_lazuli 32'
Start-Sleep -Seconds 1
Save-Screenshot 'items' | Out-Null

$m = Get-LogMark
Invoke-Chat '/quark enchanting demo'
Wait-LogLine 'enchanting: demo table ready' 20 -Since $m | Out-Null
if ($vanilla) {
    Wait-LogLine 'opened the matrix enchanter at .* \(form\)' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'matrix' | Out-Null
    Press-Key ESC
} else {
    Wait-LogLine 'opened the matrix enchanter at .* \(screen\)' 10 -Since $m | Out-Null
    Wait-LogLine 'enchanting: matrix screen opened' 15 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'matrix' | Out-Null
    # Hover the grid centre: the piece tooltip (or the cell highlight).
    Move-Mouse 0.5 0.36 -Relative
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'matrix-hover' | Out-Null
    $c = Get-LogMark
    Press-Key ESC
    Wait-LogLine 'enchanting: matrix screen closed' 10 -Since $c | Out-Null
}
Start-Sleep -Milliseconds 800
Save-Screenshot 'after' | Out-Null

if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'enchanting.*'
    if ($r.Fail -gt 0) { throw "enchanting selftests failed: $($r.Fail)" }
}
