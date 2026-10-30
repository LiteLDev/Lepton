# U0 test-only probe blocks (ids lepton:u0_probe_*): one shape set and one wood set, used by the
# game.shapes.* selftests (src/game/Shapes.cpp). Hidden from the creative inventory and commands.
#
#   pwsh -NoProfile -File tools/pack/u0.ps1      # regenerates and overwrites the committed output
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/../textures/lib.ps1"

Start-PackUnit 'u0probe'

$tex = Join-Path $script:RP 'textures/blocks'
$items = Join-Path $script:RP 'textures/items'
function Tex([string]$Name) { Add-PackFile (Join-Path $tex "$Name.png") }

# --- textures (procedural, original) ---
New-StoneTexture -Path (Tex 'u0_probe_stone') -Ramp (New-Ramp '#4d5a66', '#9fb2c2' 5) -Seed 11
New-PlankTexture -Path (Tex 'u0_probe_planks') -Ramp (New-Ramp '#3d2a4a', '#9c7ab0' 5) -Seed 12
New-LogSideTexture -Path (Tex 'u0_probe_log') -Ramp (New-Ramp '#2a1f33', '#5a4466' 4) -Seed 13
New-LogTopTexture -Path (Tex 'u0_probe_log_top') -BarkRamp (New-Ramp '#2a1f33', '#5a4466' 3) -WoodRamp (New-Ramp '#7a5c8c', '#b89acb' 4) -Seed 14
New-LogSideTexture -Path (Tex 'u0_probe_stripped_log') -Ramp (New-Ramp '#7a5c8c', '#b89acb' 4) -Seed 15
New-LogTopTexture -Path (Tex 'u0_probe_stripped_log_top') -BarkRamp (New-Ramp '#7a5c8c', '#9a7cac' 2) -WoodRamp (New-Ramp '#8a6c9c', '#c8aadb' 4) -Seed 16

# door / trapdoor: a frame with a window, drawn from rows
$frame = @{ o = '#3d2a4a'; m = '#7a5c8c'; l = '#9c7ab0'; g = '#cfe8ff80' }
$doorTop = @(
    'oooooooooooooooo', 'ommmmmmmmmmmmmmo', 'omggggggggggggmo', 'omggggggggggggmo', 'omgggggmmgggggmo', 'omgggggmmgggggmo',
    'omggggggggggggmo', 'omggggggggggggmo', 'ommmmmmmmmmmmmmo', 'omlllllllllllllo', 'omlmmmmmmmmmmlmo', 'omlmlllllllmmlmo',
    'omlmlllllllmmlmo', 'omlmmmmmmmmmmlmo', 'omlllllllllllllo', 'omlllllllllllllo')
$doorBottom = @(
    'omlllllllllllllo', 'omlmmmmmmmmmmlmo', 'omlmlllllllmmlmo', 'omlmlllllllmmlmo', 'omlmlllllllmmlmo', 'omlmmmmmmmmmmlmo',
    'omllllllllllllmo', 'omlllllllllolllo', 'omlllllllllolllo', 'omlmmmmmmmmmmlmo', 'omlmlllllllmmlmo', 'omlmlllllllmmlmo',
    'omlmmmmmmmmmmlmo', 'omlllllllllllllo', 'ommmmmmmmmmmmmmo', 'oooooooooooooooo')
$trap = @(
    'oooooooooooooooo', 'ommmmmmmmmmmmmmo', 'omgggmmggmmgggmo', 'omgggmmggmmgggmo', 'ommmmmmmmmmmmmmo', 'omllllllllllllmo',
    'omllllllllllllmo', 'ommmmmmmmmmmmmmo', 'ommmmmmmmmmmmmmo', 'omllllllllllllmo', 'omllllllllllllmo', 'ommmmmmmmmmmmmmo',
    'omgggmmggmmgggmo', 'omgggmmggmmgggmo', 'ommmmmmmmmmmmmmo', 'oooooooooooooooo')
$doorItem = @(
    '................', '....oooooooo....', '....ommmmmmo....', '....omggggmo....', '....omggggmo....', '....ommmmmmo....',
    '....omllllmo....', '....omllllmo....', '....ommmmmmo....', '....omllllmo....', '....omllllmo....', '....omlllomo....',
    '....omllllmo....', '....ommmmmmo....', '....oooooooo....', '................')
Save-PixelArt -Rows $doorTop -Palette $frame -Path (Tex 'u0_probe_door_top')
Save-PixelArt -Rows $doorBottom -Palette $frame -Path (Tex 'u0_probe_door_bottom')
Save-PixelArt -Rows $trap -Palette $frame -Path (Tex 'u0_probe_trapdoor')
Save-PixelArt -Rows $doorItem -Palette $frame -Path (Add-PackFile (Join-Path $items 'u0_probe_door.png'))

# --- blocks ---
$stone = @{
    Material = 'u0_probe_stone'; Textures = 'textures/blocks/u0_probe_stone'; Name = @{ en = 'Probe Stone (test)'; zh = '探针石（测试）' }
    Hardness = 1.5; Resistance = 6; MapColor = '#7f8f9f'; Tool = 'pickaxe'; Hidden = $true
}
$null = New-ShapeSet @stone -Shapes block, stairs, slab, vertical_slab, wall, fence, fence_gate -Stonecutter
# a pane of the same material would share the wall's recipe (6 blocks): no crafting for it
$null = New-ShapeSet @stone -Shapes pane -NoCrafting

$null = New-WoodSet -Wood 'u0_probe' -Name @{ en = 'Probe (test)'; zh = '探针（测试）' } -MapColor '#8a6c9c' -Hidden -Textures @{
    log_side = 'textures/blocks/u0_probe_log'; log_top = 'textures/blocks/u0_probe_log_top'
    stripped_log_side = 'textures/blocks/u0_probe_stripped_log'; stripped_log_top = 'textures/blocks/u0_probe_stripped_log_top'
    planks = 'textures/blocks/u0_probe_planks'
    door_top = 'textures/blocks/u0_probe_door_top'; door_bottom = 'textures/blocks/u0_probe_door_bottom'
    door_item = 'textures/items/u0_probe_door'; trapdoor = 'textures/blocks/u0_probe_trapdoor'
}

Complete-PackUnit
