#pragma once

// The blocks the tree generators place, resolved once per world on the server thread and shared
// read-only with the world generation threads.

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

class Block;
class BlockType;

namespace lp::feature::newwoods {

struct Palette {
    Block const*                                                 air  = nullptr;
    Block const*                                                 dirt = nullptr;
    std::map<std::string, std::array<Block const*, 3>, std::less<>> logs;   ///< id -> axis x, y, z
    std::map<std::string, Block const*, std::less<>>              leaves; ///< id -> non-persistent state
    std::array<Block const*, 3>                                  azaleaLog{}; ///< lepton:azalea_log per axis
    std::map<BlockType const*, std::string>                       blossomLeafTypes; ///< trumpet leaves -> particle

    [[nodiscard]] Block const* log(std::string_view id, int axis) const {
        auto it = logs.find(id);
        return it == logs.end() || axis < 0 || axis > 2 ? nullptr : it->second[axis];
    }
    [[nodiscard]] Block const* generatedLeaves(std::string_view id) const {
        auto it = leaves.find(id);
        return it == leaves.end() ? nullptr : it->second;
    }
};

/// The current palette (nullptr before the first world is ready / without the behaviour pack).
[[nodiscard]] std::shared_ptr<Palette const> palette();
/// Resolves the palette again (server thread; world ready).
void rebuildPalette();
void clearPalette();

} // namespace lp::feature::newwoods
