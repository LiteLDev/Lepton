#include "feature/variantcontainers/VariantContainers.h"

#include <array>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"

namespace lp::feature::variantcontainers {

LP_REGISTER_CONFIG("variantcontainers", Config);

Config const& cfg() { return config::get<Config>(); }

namespace bs = game::blockstates;

namespace {
constexpr std::array<char const*, 4> kDirNames{"south", "west", "north", "east"};
constexpr char const*                kCardinal = "minecraft:cardinal_direction";
} // namespace

char const* dirName(int f) { return kDirNames[static_cast<size_t>(f & 3)]; }

int dirIndex(std::string_view name) {
    for (int i = 0; i < 4; ++i)
        if (name == kDirNames[static_cast<size_t>(i)]) return i;
    return -1;
}

BlockPos step(BlockPos const& pos, int f) {
    auto [dx, dz] = game::facingOffset(f);
    return {pos.x + dx, pos.y, pos.z + dz};
}

// ---- chest states -----------------------------------------------------------------------------------

namespace {
HashedString const& tagChest() {
    static HashedString const h("lepton:variant_chest");
    return h;
}
HashedString const& tagTrapped() {
    static HashedString const h("lepton:variant_trapped_chest");
    return h;
}
HashedString const& tagFurnace() {
    static HashedString const h("lepton:variant_furnace");
    return h;
}
} // namespace

bool isVariantChest(Block const& block) { return block.hasTag(tagChest()) || block.hasTag(tagTrapped()); }

std::optional<ChestState> chestState(Block const& block) {
    if (!isVariantChest(block)) return std::nullopt;
    auto look = bs::getString(block, kCardinal);
    auto type = bs::getString(block, "lepton:type");
    if (!look || !type) return std::nullopt;
    int l = dirIndex(*look);
    if (l < 0) return std::nullopt;
    ChestState s;
    s.id      = block.getTypeName();
    s.facing  = opp(l);
    s.type    = *type == "left" ? ChestType::Left : *type == "right" ? ChestType::Right : ChestType::Single;
    s.lid     = bs::getInt(block, "lepton:lid").value_or(0);
    s.trapped = block.hasTag(tagTrapped());
    return s;
}

Block const* withChest(Block const& block, std::optional<int> facing, std::optional<ChestType> type, std::optional<int> lid,
    std::optional<int> power) {
    bs::Changes ch;
    if (facing) ch.emplace_back(kCardinal, std::string(dirName(opp(*facing))));
    if (type) ch.emplace_back("lepton:type", std::string(*type == ChestType::Left ? "left" : *type == ChestType::Right ? "right" : "single"));
    if (lid) ch.emplace_back("lepton:lid", *lid);
    if (power) ch.emplace_back("lepton:power", *power);
    return bs::with(block, ch);
}

int connectedDir(ChestState const& s) { return s.type == ChestType::Left ? cw(s.facing) : ccw(s.facing); }

std::optional<BlockPos> partnerOf(BlockSource& region, BlockPos const& pos) {
    auto s = chestState(region.getBlock(pos));
    if (!s || s->type == ChestType::Single) return std::nullopt;
    BlockPos q  = step(pos, connectedDir(*s));
    auto     ps = chestState(region.getBlock(q));
    if (!ps || ps->id != s->id || ps->facing != s->facing || ps->type == ChestType::Single || ps->type == s->type)
        return std::nullopt;
    if (step(q, connectedDir(*ps)) != pos) return std::nullopt;
    return q;
}

std::optional<ChestStorage> chestStorage(BlockSource& region, BlockPos const& pos) {
    auto s = chestState(region.getBlock(pos));
    if (!s) return std::nullopt;
    ChestStorage st;
    auto         partner = partnerOf(region, pos);
    if (!partner) {
        st.key = game::globalPos(region, pos);
        return st;
    }
    // Java: the half of type right is the first (top) part of the double inventory
    bool first = s->type == ChestType::Right;
    st.key     = game::globalPos(region, first ? pos : *partner);
    st.second  = first ? *partner : pos;
    st.size    = 2 * kChestSlots;
    return st;
}

// ---- helpers ----------------------------------------------------------------------------------------

void playSoundAt(BlockSource& region, float x, float y, float z, std::string const& sound, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p   = static_cast<PlaySoundPacket&>(*pkt);
    p.mName   = sound;
    p.mPos    = BlockPos{static_cast<int>(x * 8), static_cast<int>(y * 8), static_cast<int>(z * 8)};
    p.mVolume = volume;
    p.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(game::blockPosOf(Vec3{x, y, z}), p, nullptr);
}

void spawnItem(BlockSource& region, ItemStack const& stack, float x, float y, float z) {
    auto level = ll::service::getLevel();
    if (!level || game::isEmpty(stack)) return;
    level->getSpawner().spawnItem(region, stack, nullptr, Vec3{x, y, z}, 10);
}

std::vector<std::string> const& chestMaterials() {
    static std::vector<std::string> const v{"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove",
        "bamboo", "cherry", "crimson", "warped", "nether_brick", "purpur", "prismarine", "azalea", "blossom", "ancient"};
    return v;
}

bool isWoodenMaterial(std::string_view m) { return m != "nether_brick" && m != "purpur" && m != "prismarine"; }

std::vector<std::string> const& chestIds() {
    static std::vector<std::string> const v = [] {
        std::vector<std::string> out;
        for (auto const& m : chestMaterials()) {
            out.push_back("lepton:" + m + "_chest");
            out.push_back("lepton:trapped_" + m + "_chest");
        }
        return out;
    }();
    return v;
}

std::vector<std::string> const& furnaceIds() {
    static std::vector<std::string> const v{"lepton:deepslate_furnace", "lepton:blackstone_furnace"};
    return v;
}

bool isVariantFurnace(Block const& block) { return block.hasTag(tagFurnace()); }

} // namespace lp::feature::variantcontainers
