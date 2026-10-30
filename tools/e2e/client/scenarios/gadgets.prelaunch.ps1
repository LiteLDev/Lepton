# The gadgets screenshots need the vanilla HUD (hotbar, crosshair): make sure it is not hidden (F1)
# in the game slot's options.
$opts = Get-ChildItem (Get-GameDataDir) -Recurse -Filter options.txt -ErrorAction SilentlyContinue
foreach ($o in $opts) {
    $text = Get-Content $o.FullName -Raw
    if ($text -match 'gfx_hidehud:1') {
        ($text -replace 'gfx_hidehud:1', 'gfx_hidehud:0') | Set-Content -Path $o.FullName -NoNewline
        Write-Host "[e2e] gadgets: un-hid the HUD in $($o.FullName)"
    }
}
