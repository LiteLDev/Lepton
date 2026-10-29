# U1 vslabs: the vertical slab of every vanilla slab material (Quark's VerticalSlabs module).
# Behaviour reference: Quark 4.1-488 VerticalSlabsModule / VerticalSlabBlock (docs/spec/vslabs.md).
#
#   pwsh -NoProfile -File tools/pack/vslabs.ps1      # regenerates and overwrites the committed output
#
# Every vertical slab uses the vanilla textures of its slab (the game's own textures, so resource
# packs apply), the shared geometry of New-ShapeSet, and these additions on top of the generator:
#   - waterlogging (minecraft:liquid_detection; a double vertical slab holds no water),
#   - wall / pane / bar / fence connections on the full back face and (Quark's
#     allowSideConnections) on the two side faces, per lepton:type (minecraft:connection_rule),
# Recipes: 3 slabs in a column -> 3 vertical slabs, 1 vertical slab -> 1 slab (generator), and the
# stonecutter recipes of Quark 4.1-488 (inputs below). SlabsToBlocks recipes are added natively at
# world load (src/feature/vslabs/Recipes.cpp), for every slab including other units'.
. "$PSScriptRoot/lib.ps1"

Start-PackUnit 'vslabs'

# A copy of $Base with the keys of $Over replaced.
function With([hashtable]$Base, [hashtable]$Over) {
    $h = $Base.Clone()
    foreach ($k in $Over.Keys) { $h[$k] = $Over[$k] }
    return $h
}

$wood = @{ Sound = 'wood'; Hardness = 2; Resistance = 3; Tool = 'axe'; Flammable = $true }
$stone = @{ Sound = 'stone'; Hardness = 2; Resistance = 6; Tool = 'pickaxe' }

# Path: Quark's vertical slab path (lepton:<path>_vertical_slab). Slab: the Bedrock slab id.
# Cut: stonecutter inputs (Bedrock ids; each gives 2, copper blocks give 8 as in Quark).
$materials = @(
    # --- wood ---
    @{ Path = 'oak'; Slab = 'oak_slab'; Tex = 'textures/blocks/planks_oak'; En = 'Oak'; Zh = '橡木'; P = $wood; Color = '#8f7748' }
    @{ Path = 'spruce'; Slab = 'spruce_slab'; Tex = 'textures/blocks/planks_spruce'; En = 'Spruce'; Zh = '云杉木'; P = $wood; Color = '#815631' }
    @{ Path = 'birch'; Slab = 'birch_slab'; Tex = 'textures/blocks/planks_birch'; En = 'Birch'; Zh = '白桦木'; P = $wood; Color = '#f7e9a3' }
    @{ Path = 'jungle'; Slab = 'jungle_slab'; Tex = 'textures/blocks/planks_jungle'; En = 'Jungle'; Zh = '丛林木'; P = $wood; Color = '#976d4d' }
    @{ Path = 'acacia'; Slab = 'acacia_slab'; Tex = 'textures/blocks/planks_acacia'; En = 'Acacia'; Zh = '金合欢木'; P = $wood; Color = '#d87f33' }
    @{ Path = 'dark_oak'; Slab = 'dark_oak_slab'; Tex = 'textures/blocks/planks_big_oak'; En = 'Dark Oak'; Zh = '深色橡木'; P = $wood; Color = '#664c33' }
    @{ Path = 'mangrove'; Slab = 'mangrove_slab'; Tex = 'textures/blocks/mangrove_planks'; En = 'Mangrove'; Zh = '红树木'; P = $wood; Color = '#993333' }
    @{ Path = 'cherry'; Slab = 'cherry_slab'; Tex = 'textures/blocks/cherry_planks'; En = 'Cherry'; Zh = '樱花木'; P = (With $wood @{ Sound = 'cherry_wood' }); Color = '#d1b1a1' }
    @{ Path = 'pale_oak'; Slab = 'pale_oak_slab'; Tex = 'textures/blocks/pale_oak_planks'; En = 'Pale Oak'; Zh = '苍白橡木'; P = $wood; Color = '#e4d9d4' }
    @{ Path = 'bamboo'; Slab = 'bamboo_slab'; Tex = 'textures/blocks/bamboo_planks'; En = 'Bamboo'; Zh = '竹'; P = (With $wood @{ Sound = 'bamboo_wood' }); Color = '#e5e533' }
    @{ Path = 'bamboo_mosaic'; Slab = 'bamboo_mosaic_slab'; Tex = 'textures/blocks/bamboo_mosaic'; En = 'Bamboo Mosaic'; Zh = '竹马赛克'; P = (With $wood @{ Sound = 'bamboo_wood' }); Color = '#e5e533' }
    @{ Path = 'crimson'; Slab = 'crimson_slab'; Tex = 'textures/blocks/huge_fungus/crimson_planks'; En = 'Crimson'; Zh = '绯红木'; P = (With $wood @{ Sound = 'nether_wood'; Flammable = $false }); Color = '#943f61' }
    @{ Path = 'warped'; Slab = 'warped_slab'; Tex = 'textures/blocks/huge_fungus/warped_planks'; En = 'Warped'; Zh = '诡异木'; P = (With $wood @{ Sound = 'nether_wood'; Flammable = $false }); Color = '#3a8e8c' }
    @{ Path = 'poplar'; Slab = 'poplar_slab'; Tex = 'textures/blocks/poplar_planks'; En = 'Poplar'; Zh = '杨木'; P = $wood; Color = '#b8a77a' }
    # --- stone ---
    @{ Path = 'stone'; Slab = 'normal_stone_slab'; Tex = 'textures/blocks/stone'; En = 'Stone'; Zh = '石'; P = $stone; Color = '#707070'; Cut = @('stone') }
    @{ Path = 'smooth_stone'; Slab = 'smooth_stone_slab'; Tex = @{ side = 'textures/blocks/stone_slab_side'; top = 'textures/blocks/stone_slab_top' }; En = 'Smooth Stone'; Zh = '平滑石'; P = $stone; Color = '#707070'; Cut = @('smooth_stone') }
    @{ Path = 'cobblestone'; Slab = 'cobblestone_slab'; Tex = 'textures/blocks/cobblestone'; En = 'Cobblestone'; Zh = '圆石'; P = $stone; Color = '#707070'; Cut = @('cobblestone') }
    @{ Path = 'mossy_cobblestone'; Slab = 'mossy_cobblestone_slab'; Tex = 'textures/blocks/cobblestone_mossy'; En = 'Mossy Cobblestone'; Zh = '苔石'; P = $stone; Color = '#707070'; Cut = @('mossy_cobblestone') }
    @{ Path = 'stone_brick'; Slab = 'stone_brick_slab'; Tex = 'textures/blocks/stonebrick'; En = 'Stone Brick'; Zh = '石砖'; P = $stone; Color = '#707070'; Cut = @('stone_bricks', 'stone') }
    @{ Path = 'mossy_stone_brick'; Slab = 'mossy_stone_brick_slab'; Tex = 'textures/blocks/stonebrick_mossy'; En = 'Mossy Stone Brick'; Zh = '苔石砖'; P = (With $stone @{ Hardness = 1.5 }); Color = '#707070'; Cut = @('mossy_stone_bricks') }
    @{ Path = 'granite'; Slab = 'granite_slab'; Tex = 'textures/blocks/stone_granite'; En = 'Granite'; Zh = '花岗岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#976d4d'; Cut = @('granite') }
    @{ Path = 'polished_granite'; Slab = 'polished_granite_slab'; Tex = 'textures/blocks/stone_granite_smooth'; En = 'Polished Granite'; Zh = '磨制花岗岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#976d4d'; Cut = @('polished_granite', 'granite') }
    @{ Path = 'diorite'; Slab = 'diorite_slab'; Tex = 'textures/blocks/stone_diorite'; En = 'Diorite'; Zh = '闪长岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#fffcf5'; Cut = @('diorite') }
    @{ Path = 'polished_diorite'; Slab = 'polished_diorite_slab'; Tex = 'textures/blocks/stone_diorite_smooth'; En = 'Polished Diorite'; Zh = '磨制闪长岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#fffcf5'; Cut = @('polished_diorite', 'diorite') }
    @{ Path = 'andesite'; Slab = 'andesite_slab'; Tex = 'textures/blocks/stone_andesite'; En = 'Andesite'; Zh = '安山岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#707070'; Cut = @('andesite') }
    @{ Path = 'polished_andesite'; Slab = 'polished_andesite_slab'; Tex = 'textures/blocks/stone_andesite_smooth'; En = 'Polished Andesite'; Zh = '磨制安山岩'; P = (With $stone @{ Hardness = 1.5 }); Color = '#707070'; Cut = @('polished_andesite', 'andesite') }
    @{ Path = 'cobbled_deepslate'; Slab = 'cobbled_deepslate_slab'; Tex = 'textures/blocks/deepslate/cobbled_deepslate'; En = 'Cobbled Deepslate'; Zh = '深板岩圆石'; P = (With $stone @{ Sound = 'deepslate'; Hardness = 3.5 }); Color = '#646464'; Cut = @('cobbled_deepslate') }
    @{ Path = 'polished_deepslate'; Slab = 'polished_deepslate_slab'; Tex = 'textures/blocks/deepslate/polished_deepslate'; En = 'Polished Deepslate'; Zh = '磨制深板岩'; P = (With $stone @{ Sound = 'deepslate'; Hardness = 3.5 }); Color = '#646464'; Cut = @('polished_deepslate', 'cobbled_deepslate') }
    @{ Path = 'deepslate_brick'; Slab = 'deepslate_brick_slab'; Tex = 'textures/blocks/deepslate/deepslate_bricks'; En = 'Deepslate Brick'; Zh = '深板岩砖'; P = (With $stone @{ Sound = 'deepslate_bricks'; Hardness = 3.5 }); Color = '#646464'; Cut = @('deepslate_bricks', 'polished_deepslate', 'cobbled_deepslate') }
    @{ Path = 'deepslate_tile'; Slab = 'deepslate_tile_slab'; Tex = 'textures/blocks/deepslate/deepslate_tiles'; En = 'Deepslate Tile'; Zh = '深板岩瓦'; P = (With $stone @{ Sound = 'deepslate_bricks'; Hardness = 3.5 }); Color = '#646464'; Cut = @('deepslate_tiles', 'deepslate_bricks', 'polished_deepslate', 'cobbled_deepslate') }
    @{ Path = 'tuff'; Slab = 'tuff_slab'; Tex = 'textures/blocks/tuff'; En = 'Tuff'; Zh = '凝灰岩'; P = (With $stone @{ Sound = 'tuff'; Hardness = 1.5 }); Color = '#393029'; Cut = @('tuff') }
    @{ Path = 'polished_tuff'; Slab = 'polished_tuff_slab'; Tex = 'textures/blocks/polished_tuff'; En = 'Polished Tuff'; Zh = '磨制凝灰岩'; P = (With $stone @{ Sound = 'polished_tuff'; Hardness = 1.5 }); Color = '#393029'; Cut = @('polished_tuff', 'tuff') }
    @{ Path = 'tuff_brick'; Slab = 'tuff_brick_slab'; Tex = 'textures/blocks/tuff_bricks'; En = 'Tuff Brick'; Zh = '凝灰岩砖'; P = (With $stone @{ Sound = 'tuff_bricks'; Hardness = 1.5 }); Color = '#393029'; Cut = @('tuff_bricks') }
    @{ Path = 'brick'; Slab = 'brick_slab'; Tex = 'textures/blocks/brick'; En = 'Brick'; Zh = '砖'; P = $stone; Color = '#993333'; Cut = @('brick_block') }
    @{ Path = 'mud_brick'; Slab = 'mud_brick_slab'; Tex = 'textures/blocks/mud_bricks'; En = 'Mud Brick'; Zh = '泥砖'; P = (With $stone @{ Sound = 'mud_bricks'; Hardness = 1.5; Resistance = 3 }); Color = '#87674f'; Cut = @('mud_bricks') }
    @{ Path = 'resin_brick'; Slab = 'resin_brick_slab'; Tex = 'textures/blocks/resin_bricks'; En = 'Resin Brick'; Zh = '树脂砖'; P = (With $stone @{ Sound = 'resin_brick'; Hardness = 1.5 }); Color = '#d87f33' }
    @{ Path = 'sandstone'; Slab = 'sandstone_slab'; Tex = @{ side = 'textures/blocks/sandstone_normal'; top = 'textures/blocks/sandstone_top'; bottom = 'textures/blocks/sandstone_bottom' }; En = 'Sandstone'; Zh = '砂岩'; P = $stone; Color = '#f7e9a3'; Cut = @('sandstone') }
    @{ Path = 'cut_sandstone'; Slab = 'cut_sandstone_slab'; Tex = @{ side = 'textures/blocks/sandstone_smooth'; top = 'textures/blocks/sandstone_top' }; En = 'Cut Sandstone'; Zh = '切制砂岩'; P = $stone; Color = '#f7e9a3'; Cut = @('cut_sandstone', 'sandstone') }
    @{ Path = 'smooth_sandstone'; Slab = 'smooth_sandstone_slab'; Tex = 'textures/blocks/sandstone_top'; En = 'Smooth Sandstone'; Zh = '平滑砂岩'; P = $stone; Color = '#f7e9a3'; Cut = @('smooth_sandstone') }
    @{ Path = 'red_sandstone'; Slab = 'red_sandstone_slab'; Tex = @{ side = 'textures/blocks/red_sandstone_normal'; top = 'textures/blocks/red_sandstone_top'; bottom = 'textures/blocks/red_sandstone_bottom' }; En = 'Red Sandstone'; Zh = '红砂岩'; P = $stone; Color = '#d87f33'; Cut = @('red_sandstone') }
    @{ Path = 'cut_red_sandstone'; Slab = 'cut_red_sandstone_slab'; Tex = @{ side = 'textures/blocks/red_sandstone_smooth'; top = 'textures/blocks/red_sandstone_top' }; En = 'Cut Red Sandstone'; Zh = '切制红砂岩'; P = $stone; Color = '#d87f33'; Cut = @('cut_red_sandstone', 'red_sandstone') }
    @{ Path = 'smooth_red_sandstone'; Slab = 'smooth_red_sandstone_slab'; Tex = 'textures/blocks/red_sandstone_top'; En = 'Smooth Red Sandstone'; Zh = '平滑红砂岩'; P = $stone; Color = '#d87f33'; Cut = @('smooth_red_sandstone') }
    @{ Path = 'quartz'; Slab = 'quartz_slab'; Tex = @{ side = 'textures/blocks/quartz_block_side'; top = 'textures/blocks/quartz_block_top' }; En = 'Quartz'; Zh = '石英'; P = $stone; Color = '#fffcf5'; Cut = @('quartz_block') }
    @{ Path = 'smooth_quartz'; Slab = 'smooth_quartz_slab'; Tex = 'textures/blocks/quartz_block_bottom'; En = 'Smooth Quartz'; Zh = '平滑石英'; P = $stone; Color = '#fffcf5'; Cut = @('smooth_quartz') }
    @{ Path = 'nether_brick'; Slab = 'nether_brick_slab'; Tex = 'textures/blocks/nether_brick'; En = 'Nether Brick'; Zh = '下界砖'; P = (With $stone @{ Sound = 'nether_brick' }); Color = '#700200'; Cut = @('nether_brick') }
    @{ Path = 'red_nether_brick'; Slab = 'red_nether_brick_slab'; Tex = 'textures/blocks/red_nether_brick'; En = 'Red Nether Brick'; Zh = '红色下界砖'; P = (With $stone @{ Sound = 'nether_brick' }); Color = '#700200'; Cut = @('red_nether_brick') }
    @{ Path = 'blackstone'; Slab = 'blackstone_slab'; Tex = 'textures/blocks/blackstone'; En = 'Blackstone'; Zh = '黑石'; P = (With $stone @{ Hardness = 1.5 }); Color = '#191919'; Cut = @('blackstone') }
    @{ Path = 'polished_blackstone'; Slab = 'polished_blackstone_slab'; Tex = 'textures/blocks/polished_blackstone'; En = 'Polished Blackstone'; Zh = '磨制黑石'; P = $stone; Color = '#191919'; Cut = @('polished_blackstone', 'blackstone') }
    @{ Path = 'polished_blackstone_brick'; Slab = 'polished_blackstone_brick_slab'; Tex = 'textures/blocks/polished_blackstone_bricks'; En = 'Polished Blackstone Brick'; Zh = '磨制黑石砖'; P = (With $stone @{ Hardness = 1.5 }); Color = '#191919'; Cut = @('polished_blackstone_bricks', 'polished_blackstone', 'blackstone') }
    @{ Path = 'end_stone_brick'; Slab = 'end_stone_brick_slab'; Tex = 'textures/blocks/end_bricks'; En = 'End Stone Brick'; Zh = '末地石砖'; P = (With $stone @{ Hardness = 3; Resistance = 9 }); Color = '#f7e9a3'; Cut = @('end_bricks', 'end_stone') }
    @{ Path = 'purpur'; Slab = 'purpur_slab'; Tex = 'textures/blocks/purpur_block'; En = 'Purpur'; Zh = '紫珀'; P = $stone; Color = '#b24cd8'; Cut = @('purpur_block') }
    @{ Path = 'prismarine'; Slab = 'prismarine_slab'; Tex = 'textures/blocks/prismarine_rough'; En = 'Prismarine'; Zh = '海晶石'; P = (With $stone @{ Hardness = 1.5 }); Color = '#4c7f99'; Cut = @('prismarine') }
    @{ Path = 'prismarine_brick'; Slab = 'prismarine_brick_slab'; Tex = 'textures/blocks/prismarine_bricks'; En = 'Prismarine Brick'; Zh = '海晶石砖'; P = (With $stone @{ Hardness = 1.5 }); Color = '#5cdbd5'; Cut = @('prismarine_bricks') }
    @{ Path = 'dark_prismarine'; Slab = 'dark_prismarine_slab'; Tex = 'textures/blocks/prismarine_dark'; En = 'Dark Prismarine'; Zh = '暗海晶石'; P = (With $stone @{ Hardness = 1.5 }); Color = '#5cdbd5'; Cut = @('dark_prismarine') }
    # 1.26 materials (vanilla slabs that came after Quark 4.1-488; same rule: every vanilla slab)
    @{ Path = 'cinnabar'; Slab = 'cinnabar_slab'; Tex = 'textures/blocks/cinnabar'; En = 'Cinnabar'; Zh = '朱砂'; P = (With $stone @{ Sound = 'cinnabar'; Hardness = 1.5 }); Color = '#993333'; Cut = @('cinnabar') }
    @{ Path = 'polished_cinnabar'; Slab = 'polished_cinnabar_slab'; Tex = 'textures/blocks/polished_cinnabar'; En = 'Polished Cinnabar'; Zh = '磨制朱砂'; P = (With $stone @{ Sound = 'cinnabar'; Hardness = 1.5 }); Color = '#993333'; Cut = @('polished_cinnabar', 'cinnabar') }
    @{ Path = 'cinnabar_brick'; Slab = 'cinnabar_brick_slab'; Tex = 'textures/blocks/cinnabar_bricks'; En = 'Cinnabar Brick'; Zh = '朱砂砖'; P = (With $stone @{ Sound = 'cinnabar'; Hardness = 1.5 }); Color = '#993333'; Cut = @('cinnabar_bricks', 'polished_cinnabar', 'cinnabar') }
    @{ Path = 'sulfur'; Slab = 'sulfur_slab'; Tex = 'textures/blocks/sulfur'; En = 'Sulfur'; Zh = '硫磺'; P = (With $stone @{ Sound = 'sulfur'; Hardness = 1.5 }); Color = '#e5e533'; Cut = @('sulfur') }
    @{ Path = 'polished_sulfur'; Slab = 'polished_sulfur_slab'; Tex = 'textures/blocks/polished_sulfur'; En = 'Polished Sulfur'; Zh = '磨制硫磺'; P = (With $stone @{ Sound = 'sulfur'; Hardness = 1.5 }); Color = '#e5e533'; Cut = @('polished_sulfur', 'sulfur') }
    @{ Path = 'sulfur_brick'; Slab = 'sulfur_brick_slab'; Tex = 'textures/blocks/sulfur_bricks'; En = 'Sulfur Brick'; Zh = '硫磺砖'; P = (With $stone @{ Sound = 'sulfur'; Hardness = 1.5 }); Color = '#e5e533'; Cut = @('sulfur_bricks', 'polished_sulfur', 'sulfur') }
)

# Copper: four weathering stages, each with a waxed twin (Quark: WeatheringCopperVerticalSlabBlock).
$copper = @(
    @{ Stage = ''; En = ''; Zh = ''; Block = 'copper_block'; Waxed = 'waxed_copper'; Color = '#d87f33' }
    @{ Stage = 'exposed_'; En = 'Exposed '; Zh = '斑驳的'; Block = 'exposed_copper'; Waxed = 'waxed_exposed_copper'; Color = '#a0a0a0' }
    @{ Stage = 'weathered_'; En = 'Weathered '; Zh = '锈蚀的'; Block = 'weathered_copper'; Waxed = 'waxed_weathered_copper'; Color = '#4c9999' }
    @{ Stage = 'oxidized_'; En = 'Oxidized '; Zh = '氧化的'; Block = 'oxidized_copper'; Waxed = 'waxed_oxidized_copper'; Color = '#5cdbd5' }
)
$copperP = @{ Sound = 'copper'; Hardness = 3; Resistance = 6; Tool = 'pickaxe' }
foreach ($c in $copper) {
    $cut = "$($c.Stage)cut_copper"
    $tex = "textures/blocks/$cut"
    $materials += @{ Path = $cut; Slab = "$($c.Stage)cut_copper_slab"; Tex = $tex; En = "$($c.En)Cut Copper"; Zh = "$($c.Zh)切制铜"
        P = $copperP; Color = $c.Color; Cut = @($cut); Cut8 = @($c.Block); Weathering = ($c.Stage -ne 'oxidized_') }
    $materials += @{ Path = "waxed_$cut"; Slab = "waxed_$($c.Stage)cut_copper_slab"; Tex = $tex; En = "Waxed $($c.En)Cut Copper"; Zh = "涂蜡$($c.Zh)切制铜"
        P = $copperP; Color = $c.Color; Cut = @("waxed_$cut"); Cut8 = @($c.Waxed) }
}

# Concrete and wool: Bedrock 1.26 has a slab of every dye colour of both (same rule: every vanilla
# slab). Textures are the vanilla slabs' ("silver" is light gray in the texture file names).
$dyes = @(
    @{ Id = 'white'; Tex = 'white'; En = 'White'; Zh = '白色'; Color = '#ffffff' }
    @{ Id = 'orange'; Tex = 'orange'; En = 'Orange'; Zh = '橙色'; Color = '#d87f33' }
    @{ Id = 'magenta'; Tex = 'magenta'; En = 'Magenta'; Zh = '品红色'; Color = '#b24cd8' }
    @{ Id = 'light_blue'; Tex = 'light_blue'; En = 'Light Blue'; Zh = '淡蓝色'; Color = '#6699d8' }
    @{ Id = 'yellow'; Tex = 'yellow'; En = 'Yellow'; Zh = '黄色'; Color = '#e5e533' }
    @{ Id = 'lime'; Tex = 'lime'; En = 'Lime'; Zh = '黄绿色'; Color = '#7fcc19' }
    @{ Id = 'pink'; Tex = 'pink'; En = 'Pink'; Zh = '粉红色'; Color = '#f27fa5' }
    @{ Id = 'gray'; Tex = 'gray'; En = 'Gray'; Zh = '灰色'; Color = '#4c4c4c' }
    @{ Id = 'light_gray'; Tex = 'silver'; En = 'Light Gray'; Zh = '淡灰色'; Color = '#999999' }
    @{ Id = 'cyan'; Tex = 'cyan'; En = 'Cyan'; Zh = '青色'; Color = '#4c7f99' }
    @{ Id = 'purple'; Tex = 'purple'; En = 'Purple'; Zh = '紫色'; Color = '#7f3fb2' }
    @{ Id = 'blue'; Tex = 'blue'; En = 'Blue'; Zh = '蓝色'; Color = '#334cb2' }
    @{ Id = 'brown'; Tex = 'brown'; En = 'Brown'; Zh = '棕色'; Color = '#664c33' }
    @{ Id = 'green'; Tex = 'green'; En = 'Green'; Zh = '绿色'; Color = '#667f33' }
    @{ Id = 'red'; Tex = 'red'; En = 'Red'; Zh = '红色'; Color = '#993333' }
    @{ Id = 'black'; Tex = 'black'; En = 'Black'; Zh = '黑色'; Color = '#191919' }
)
$concreteP = @{ Sound = 'stone'; Hardness = 1.8; Resistance = 1.8; Tool = 'pickaxe' }
$woolP = @{ Sound = 'cloth'; Hardness = 0.8; Resistance = 0.8; Tool = '' }
foreach ($d in $dyes) {
    $materials += @{ Path = "$($d.Id)_concrete"; Slab = "$($d.Id)_concrete_slab"; Tex = "textures/blocks/concrete_$($d.Tex)"
        En = "$($d.En) Concrete"; Zh = "$($d.Zh)混凝土"; P = $concreteP; Color = $d.Color }
}
foreach ($d in $dyes) {
    # wool catches fire like wool (catch 30, destroy 60)
    $materials += @{ Path = "$($d.Id)_wool"; Slab = "$($d.Id)_wool_slab"; Tex = "textures/blocks/wool_colored_$($d.Tex)"
        En = "$($d.En) Wool"; Zh = "$($d.Zh)羊毛"; P = $woolP; Color = $d.Color; Fire = @(30, 60) }
}

$sides = @{ north = @('south', 'east', 'west'); south = @('north', 'east', 'west'); west = @('east', 'north', 'south'); east = @('west', 'north', 'south') }

foreach ($m in $materials) {
    $p = $m.P
    $tags = @()
    if ($p.Tool -eq 'axe') { $tags += 'lepton:wooden_vertical_slabs' }
    $args_ = @{
        Material = $m.Path; BaseBlock = "minecraft:$($m.Slab)"; Slab = "minecraft:$($m.Slab)"; Shapes = 'vertical_slab'
        Textures = $m.Tex; Name = @{ en = $m.En; zh = $m.Zh }; Names = @{ vertical_slab = @{ en = "$($m.En) Vertical Slab"; zh = "$($m.Zh)竖直台阶" } }
        Sound = $p.Sound; Hardness = $p.Hardness; Resistance = $p.Resistance; MapColor = $m.Color; Tool = $p.Tool
        Flammable = [bool]$p.Flammable; Tags = $tags
    }
    $ids = New-ShapeSet @args_
    $vid = $ids.vertical_slab
    $vpath = Get-Path $vid

    # --- additions to the generated block JSON ---
    $file = Join-Path $script:BP "blocks/$vpath.json"
    $json = Get-Content $file -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $blk = $json['minecraft:block']
    $c = $blk.components
    if ($m.Fire) { $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = $m.Fire[0]; destroy_chance_modifier = $m.Fire[1] } }
    $c['minecraft:liquid_detection'] = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
    $sorted = [ordered]@{}
    foreach ($k in ($c.Keys | Sort-Object { $_ } -CaseSensitive)) { $sorted[$k] = $c[$k] }
    $blk.components = $sorted
    foreach ($perm in $blk.permutations) {
        $type = if ($perm.condition -match "'(north|south|west|east|double)'") { $Matches[1] } else { throw "unexpected permutation $($perm.condition)" }
        $connDirs = if ($type -eq 'double') { @('north', 'south', 'west', 'east') } else { $sides[$type] }
        $perm.components['minecraft:connection_rule'] = [ordered]@{ accepts_connections_from = 'all'; enabled_directions = @($connDirs) }
        if ($type -eq 'double') {
            $perm.components['minecraft:liquid_detection'] = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $false; on_liquid_touches = 'no_reaction' }) }
        }
        $pc = [ordered]@{}
        foreach ($k in ($perm.components.Keys | Sort-Object { $_ } -CaseSensitive)) { $pc[$k] = $perm.components[$k] }
        $perm.components = $pc
    }
    Write-PackJson $file $json

    # --- stonecutter (Quark 4.1-488: data/quark/recipe/building/stonecutting/vertslabs) ---
    foreach ($in in @($m.Cut)) {
        if ($in) { Write-ShapelessRecipe "$($vpath)_from_$($in)_stonecutting" @("minecraft:$in") $vid 2 'stonecutter' }
    }
    foreach ($in in @($m.Cut8)) {
        if ($in) { Write-ShapelessRecipe "$($vpath)_from_$($in)_stonecutting" @("minecraft:$in") $vid 8 'stonecutter' }
    }
}

Complete-PackUnit
