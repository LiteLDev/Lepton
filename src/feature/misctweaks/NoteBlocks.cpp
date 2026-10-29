// MoreNoteBlockSounds: the amethyst chime for note blocks on amethyst.

#include <atomic>
#include <cmath>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/NoteBlock.h"
#include "mc/comprehensive/ParticleType.h"

#include "feature/misctweaks/Internal.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

/// Count of amethyst notes played (selftests).
std::atomic<int> gAmethystNotes{0};

bool isAmethyst(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:amethyst_block" || n == "minecraft:budding_amethyst" || n == "minecraft:amethyst_cluster"
        || n == "minecraft:small_amethyst_bud" || n == "minecraft:medium_amethyst_bud"
        || n == "minecraft:large_amethyst_bud";
}

namespace {

constexpr int kHarp = 0;

void playChime(BlockSource& region, BlockPos const& pos, int note) {
    float pitch = static_cast<float>(std::pow(2.0, (note - 12) / 12.0));
    auto  pkt   = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (pkt) {
        auto& p   = static_cast<PlaySoundPacket&>(*pkt);
        p.mName   = std::string("chime.amethyst_block");
        p.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4}; // 1/8 block units
        p.mVolume = 1.0f;
        p.mPitch  = pitch;
        pkt->sendTo(pos, region.getDimensionId());
    }
    auto type = static_cast<SharedTypes::Legacy::LevelEvent>(
        static_cast<int>(SharedTypes::Legacy::LevelEvent::ParticleLegacyEvent) | static_cast<int>(ParticleType::Note)
    );
    region.getLevel().broadcastLevelEvent(type, Vec3{pos.x + 0.5f, pos.y + 1.2f, pos.z + 0.5f}, note, nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    NoteTriggerHook,
    ll::memory::HookPriority::Normal,
    NoteBlock,
    &NoteBlock::$triggerEvent,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    int             instrument,
    int             note
) {
    auto const& c = cfg();
    if (!c.enableMoreNoteBlockSounds || !c.enableAmethystSound || instrument != kHarp
        || !isAmethyst(region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z})))
        return origin(region, pos, instrument, note);
    if (region.getLevel().isClientSide()) return; // the server sends the chime
    playChime(region, pos, note);
    ++gAmethystNotes;
}

} // namespace

void installNoteBlocks() { NoteTriggerHook::hook(); }
void uninstallNoteBlocks() { NoteTriggerHook::unhook(); }

} // namespace lp::feature::misctweaks
