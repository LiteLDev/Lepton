// Client part of U6 woodvariants (client build): the changeNames renames (Bookshelf -> Oak Bookshelf,
// Ladder -> Oak Ladder) and the block-type properties (climbable ladders / hollow logs) of the
// client's own block registry, which is rebuilt from the server's definitions on every join.

#ifdef LP_CLIENT

#include <string>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/locale/Localization.h"
#include "mc/locale/OptionalString.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/woodvariants/WoodVariants.h"
#include "game/BlockStates.h"
#include "mod/Module.h"

namespace lp::client::woodvariants {

namespace wv = lp::feature::woodvariants;

namespace {

/// The renamed text for a vanilla name key, or nullopt.
std::optional<std::string> renamed(std::string const& id) {
    auto const& c = wv::config();
    if (id == "tile.bookshelf.name" && c.enableVariantBookshelves && c.variantBookshelvesChangeNames) {
        return client::ctr("block.quark.oak_bookshelf", "Oak Bookshelf");
    }
    if (id == "tile.ladder.name" && c.enableVariantLadders && c.variantLaddersChangeNames) {
        return client::ctr("block.quark.oak_ladder", "Oak Ladder");
    }
    return std::nullopt;
}

LL_TYPE_INSTANCE_HOOK(
    LocalizationGetHook,
    ll::memory::HookPriority::Normal,
    Localization,
    &Localization::_get,
    OptionalString,
    std::string const&              id,
    std::vector<std::string> const& params
) {
    if (auto r = renamed(id)) {
        OptionalString s;
        s.valid  = true;
        s.string = *r;
        return s;
    }
    return origin(id, params);
}

LL_TYPE_INSTANCE_HOOK(
    LocalizationGetSimpleHook,
    ll::memory::HookPriority::Normal,
    Localization,
    &Localization::_getSimple,
    OptionalString,
    std::string const& id
) {
    if (auto r = renamed(id)) {
        OptionalString s;
        s.valid  = true;
        s.string = *r;
        return s;
    }
    return origin(id);
}

int gFrames = 0;

} // namespace

// Re-applies the climbable / hanging-support properties about once a second while in a world (cheap
// and idempotent; the client's block types are replaced on every join).
LP_REGISTER_HUD([](ui::DrawContext&) {
    if (++gFrames % 60 != 0 || !client::inLevel()) return;
    wv::applyKnownBlockProperties();
});

// Quark's JEI hints of the posts and hollow logs, shown in the item tooltip (docs §6).
LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    if (item.isNull()) return;
    auto const& name = item.getTypeName();
    if (!name.starts_with("lepton:")) return;
    auto const* block = game::blockstates::of(name);
    if (!block || !wv::moduleEnabled(*block)) return;
    if (wv::hasTag(*block, "lepton:post")) {
        lines.push_back("§7" + client::ctr("quark.jei.hint.posts", "Can be placed in any axis. Chains and Lanterns can connect to this block."));
    } else if (wv::hasTag(*block, "lepton:hollow_log")) {
        lines.push_back("§7" + client::ctr("quark.jei.hint.hollow_logs", "Can be crawled into by sneaking while walking towards the hollow side, can also be climbed when you're inside."));
    }
});

class WoodVariantsClientModule {
public:
    void install() {
        LocalizationGetHook::hook();
        LocalizationGetSimpleHook::hook();
    }
    void uninstall() {
        LocalizationGetHook::unhook();
        LocalizationGetSimpleHook::unhook();
    }
};

} // namespace lp::client::woodvariants

LP_REGISTER_MODULE(lp::client::woodvariants::WoodVariantsClientModule);

#endif
