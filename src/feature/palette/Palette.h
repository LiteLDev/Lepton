#pragma once

// U5 palette: Shingles, FramedGlass, GoldBars, IndustrialPalette, JapanesePalette,
// NetherBrickFenceGate, Thatch, Grate (docs/spec/palette.md). The blocks come from
// tools/pack/palette.ps1; this side adds what JSON cannot express: disabled-module blocking, fortress
// gold bars, thatch fall cushioning and composting, grate physics, ladder support and climbing,
// bamboo mat placement.

#include <optional>
#include <string>
#include <string_view>

class Actor;
class AABB;
class Block;
class BlockPos;
class BlockSource;

namespace lp::feature::palette {

struct PaletteConfig {
    int    version                    = 1;
    bool   enableShingles             = true;
    bool   enableFramedGlass          = true;
    bool   enableGoldBars             = true;
    bool   generateInNetherFortress   = true;
    bool   enableIndustrialPalette    = true;
    bool   enableIronPlates           = true;
    bool   enableIronLadder           = true;
    bool   enableJapanesePalette      = true;
    bool   enablePaperBlocks          = true;
    bool   enableBambooMats           = true;
    bool   enableNetherBrickFenceGate = true;
    bool   enableThatch               = true;
    double fallDamageMultiplier       = 0.5;
    bool   enableGrate                = true;
};

/// The config switch that enables a block / recipe of this unit.
enum class Part {
    Shingles,
    FramedGlass,
    GoldBars,
    IronPlates,
    IronLadder,
    PaperBlocks,
    BambooMats,
    NetherBrickFenceGate,
    Thatch,
    Grate,
};

/// The part of a block or recipe id of this unit (nullopt for other ids).
[[nodiscard]] std::optional<Part> partOf(std::string_view id);
/// Whether the content of `part` is enabled by the current config (module + option).
[[nodiscard]] bool enabled(Part part);
/// The effective thatch fall damage multiplier (config clamped to 0..1).
[[nodiscard]] double thatchMultiplier();

/// The grate's collision box at `pos` for `actor` (nullptr = no entity): empty for items and
/// experience orbs, tall for unleashed animals not standing on a grate, else the top plate.
[[nodiscard]] AABB grateCollision(BlockSource const& region, BlockPos const& pos, Actor const* actor);

/// Java BambooMatBlock#getStateForPlacement: the facing for a mat placed at `pos` by a player
/// looking along `yaw`/`pitch` ("north".."east", "down").
[[nodiscard]] std::string bambooMatFacing(BlockSource& region, BlockPos const& pos, std::string_view id, float yaw, float pitch);

/// Whether the ladder at `pos` still hangs on a solid block.
[[nodiscard]] bool ladderSupported(BlockSource& region, BlockPos const& pos);

/// Applies the Climbable block property to every `lepton:climbable` block of the unit (idempotent).
void applyClimbable();

namespace testing {
/// Runs the fortress hook's replacement logic for a placement by a structure piece of `pieceType`.
[[nodiscard]] Block const* fortressReplacement(int pieceType, Block const& block);
/// Number of queued gold bars positions waiting for their connections.
[[nodiscard]] size_t pendingBars();
} // namespace testing

} // namespace lp::feature::palette
