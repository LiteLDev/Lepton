# Render probe (regression test for the "custom blocks of a sub-chunk vanish" client bug): every
# probe gets its own 16x16x16 sub-chunk with one block of a model kind (cross planes, potted plants,
# posts, ladders, hedges, carpets, clusters, chests, ...) next to a canary (azalea planks, a plain
# full lepton block). If a block's geometry makes the client drop the custom blocks of the sub-chunk
# from its mesh, the canary vanishes too. One screenshot per probe: out/renderprobe/<n>-<id>.png.
# $env:LP_RENDERPROBE can list the probes to run (comma separated block ids).
$probes = @(
    'lepton:azalea_planks',                 # control: only the canary
    'lepton:red_blossom_sapling',           # U7 cross (nw_cross)
    'lepton:potted_red_blossom_sapling',    # U7 potted sapling (nw_potted)
    'lepton:lavender_blossom_leaves',       # U7 leaves
    'lepton:blue_blossom_leaf_carpet',      # U6 leaf carpet
    'lepton:ancient_hedge',                 # U6 hedge
    'lepton:acacia_ladder',                 # U6 ladder
    'lepton:acacia_post',                   # U6 post
    'lepton:potted_carrot',                 # U8 potted plant
    'lepton:chorus_weeds',                  # U29 cross
    'lepton:glow_shroom',                   # worldsurface cross
    'lepton:potted_glow_shroom',            # worldsurface potted
    'lepton:blue_corundum_cluster',         # crystals cluster
    'lepton:tiny_potato',                   # gadgets
    'lepton:acacia_chest'                   # variantcontainers
)
if ($env:LP_RENDERPROBE) { $probes = @('lepton:azalea_planks') + ($env:LP_RENDERPROBE -split ',' | ForEach-Object { $_.Trim() }) }
$blocksDir = Join-Path $PSScriptRoot '..\..\..\..\assets\pack\bp\blocks'
$y = -60
Invoke-Chat '/gamerule sendcommandfeedback false'
for ($i = 0; $i -lt $probes.Count; $i++) {
    $id = $probes[$i]
    if (-not (Test-Path (Join-Path $blocksDir "$($id -replace '^lepton:', '').json"))) { Write-Step "renderprobe: $id not in the pack, skipped"; continue }
    $x0 = 16 * $i
    Invoke-Chat "/tp @s $($x0 + 7.5) $($y + 2) 4.5 facing $($x0 + 7.5) $y 8.5"
    Start-Sleep -Seconds 1
    Invoke-Chat "/setblock $($x0 + 6) $y 8 lepton:azalea_planks"
    Invoke-Chat "/setblock $($x0 + 9) $y 8 $id"
    Start-Sleep -Seconds 3
    Save-Screenshot ('{0:d2}-{1}' -f $i, ($id -replace '^lepton:', '')) | Out-Null
}
Invoke-Chat '/gamerule sendcommandfeedback true'
