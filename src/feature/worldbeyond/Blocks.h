#pragma once

// Block handles used by this unit, resolved once per world (thread-safe; world generation threads
// read them too). Ids of other units (lepton:myalite of U3, lepton:blaze_lantern of U8) are looked
// up by id and may be missing.

#include <array>

class Block;
class BlockType;
class BlockPos;
class BlockSource;

namespace lp::feature::worldbeyond {

inline constexpr char const* kChorusWeeds   = "lepton:chorus_weeds";
inline constexpr char const* kChorusTwist   = "lepton:chorus_twist";
inline constexpr char const* kDuskyMyalite  = "lepton:dusky_myalite";
inline constexpr char const* kMyaliteCrystal = "lepton:myalite_crystal";
inline constexpr char const* kMyalite       = "lepton:myalite";      ///< U3 (NewStoneTypes)
inline constexpr char const* kBlazeLantern  = "lepton:blaze_lantern"; ///< U8 (CompressedBlocks)
inline constexpr char const* kHueState      = "lepton:hue";
inline constexpr int         kHueSteps      = 16;

struct BlockSet {
    Block const* air            = nullptr;
    Block const* obsidian       = nullptr;
    Block const* cryingObsidian = nullptr;
    Block const* glowstone      = nullptr;
    Block const* spawner        = nullptr;
    Block const* chest          = nullptr;
    Block const* endStone       = nullptr;
    Block const* weeds          = nullptr;
    Block const* twist          = nullptr;
    Block const* dusky          = nullptr;
    Block const* myalite        = nullptr; ///< U3; nullptr when not registered
    Block const* blazeLantern   = nullptr; ///< U8; nullptr when not registered
    std::array<Block const*, kHueSteps> crystal{};

    BlockType const* lavaType         = nullptr;
    BlockType const* flowingLavaType  = nullptr;
    BlockType const* waterType        = nullptr;
    BlockType const* flowingWaterType = nullptr;
    BlockType const* endStoneType     = nullptr;
    BlockType const* obsidianType     = nullptr;
    BlockType const* cryingObsType    = nullptr;
    BlockType const* weedsType        = nullptr;
    BlockType const* twistType        = nullptr;
    BlockType const* crystalType      = nullptr;

    /// Our own blocks are registered (behaviour pack present).
    [[nodiscard]] bool ready() const { return air && weeds && twist && dusky && crystal[0] && endStone; }

    [[nodiscard]] bool isLava(Block const& b) const;
    [[nodiscard]] bool isWater(Block const& b) const;
    [[nodiscard]] bool isEndStone(Block const& b) const;
    [[nodiscard]] bool isChorus(Block const& b) const;
    [[nodiscard]] bool isCrystal(Block const& b) const;
};

/// The handles of the current world (resolved on first use; never null itself).
[[nodiscard]] BlockSet const& blocks();
/// Lock-free check for hot paths (every block tick): the type is chorus weeds or twist. False
/// until `blocks()` resolved the handles of the current world.
[[nodiscard]] bool isChorusType(BlockType const* type);
/// Drops the cached handles (world closed; the client reloads the registry per world).
void resetBlocks();

/// The crystal state for a position (colour of Quark's MyaliteColorLogic, quantised).
[[nodiscard]] Block const* crystalFor(BlockPos const& pos);

/// Water in the block or its extra (waterlogging) layer.
[[nodiscard]] bool hasWater(BlockSource& region, BlockPos const& pos);

} // namespace lp::feature::worldbeyond
