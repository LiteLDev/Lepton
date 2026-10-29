# Render probe (regression test for the "custom blocks of a sub-chunk vanish" client bug, see the
# block geometry rule in AGENTS.md): every probe gets its own 16x16x16 sub-chunk with one block of a
# model kind (cross planes, potted plants, posts, ladders, hedges, carpets, clusters, chests, ...)
# next to a canary (azalea planks, a plain full lepton block). If a block makes the client drop the
# custom blocks of its sub-chunk from the mesh, the canary vanishes too: the run fails when the
# canary's screen area holds no dark planks pixels. One screenshot per probe:
# out/renderprobe/<n>-<id>.png. $env:LP_RENDERPROBE can list the probes (comma separated block ids).
Add-Type -AssemblyName System.Drawing
$probes = @(
    'lepton:azalea_planks',                 # control: only the canary
    'lepton:red_blossom_sapling',           # U7 cross (nw_cross)
    'lepton:potted_red_blossom_sapling',    # U7 potted sapling (nw_potted), once U7 ships it
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
$probes = @($probes | ForEach-Object { if ($_ -match ':') { $_ } else { "lepton:$_" } })
$blocksDir = Join-Path $PSScriptRoot '..\..\..\..\assets\pack\bp\blocks'
$y = -60

# Share of dark pixels (the planks' grooves and shaded sides) where the canary is drawn; grass has none.
function Get-CanaryDarkShare([string]$png) {
    $b = [System.Drawing.Bitmap]::FromFile($png)
    try {
        $n = 0; $d = 0
        for ($px = [int]($b.Width * 0.56); $px -lt [int]($b.Width * 0.68); $px += 2) {
            for ($py = [int]($b.Height * 0.24); $py -lt [int]($b.Height * 0.47); $py += 2) {
                $c = $b.GetPixel($px, $py); $n++
                if (($c.R + $c.G + $c.B) / 3 -lt 90) { $d++ }
            }
        }
        return $d / $n
    } finally { $b.Dispose() }
}

$missing = @()
Invoke-Chat '/gamerule sendcommandfeedback false'
try {
    for ($i = 0; $i -lt $probes.Count; $i++) {
        $id = $probes[$i]
        if (-not (Test-Path (Join-Path $blocksDir "$($id -replace '^lepton:', '').json"))) { Write-Step "renderprobe: $id not in the pack, skipped"; continue }
        $x0 = 16 * $i
        Invoke-Chat "/tp @s $($x0 + 7.5) $($y + 2) 4.5 facing $($x0 + 7.5) $y 8.5"
        Start-Sleep -Seconds 2
        Invoke-Chat "/setblock $($x0 + 6) $y 8 lepton:azalea_planks"
        Invoke-Chat "/setblock $($x0 + 9) $y 8 $id"
        Start-Sleep -Seconds 3
        $png = Save-Screenshot ('{0:d2}-{1}' -f $i, ($id -replace '^lepton:', ''))
        $share = Get-CanaryDarkShare $png
        Write-Step ("renderprobe: {0} canary {1:n3}" -f $id, $share)
        if ($share -lt 0.15) { $missing += $id }
    }
} finally {
    Invoke-Chat '/gamerule sendcommandfeedback true'
}
if ($missing.Count -gt 0) { throw "the canary block vanished next to: $($missing -join ', ')" }
