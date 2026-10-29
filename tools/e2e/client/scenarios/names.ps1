# Item and block names in texts the server builds itself: the `/give` feedback must show
# "Ancient Tome" / "Apple Crate", not `item.lepton:ancient_tome.name` / `tile.lepton:apple_crate.name`
# (on BDS the server's I18n gets the pack's names from PackDeployer).
# Modes: local, remote-mod, remote-vanilla (the case that showed raw keys).
# Screenshots: out/names/{give,chat}.png
Invoke-Chat '/gamemode creative'
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s lepton:ancient_tome'
Invoke-Chat '/give @s lepton:apple_crate 3'
Start-Sleep -Milliseconds 500
Save-Screenshot 'give' | Out-Null
# The chat screen keeps the messages on screen.
Press-Key T; Start-Sleep -Milliseconds 900
Save-Screenshot 'chat' | Out-Null
Press-Key ESC; Start-Sleep -Milliseconds 500
