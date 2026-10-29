// Client part of U7 newwoods (client build): Quark's JEI hints of the saplings and the Enchanted
// Fruit as tooltip lines.

#ifdef LP_CLIENT

#include <string>
#include <vector>

#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/newwoods/NewWoods.h"

namespace lp::client::newwoods {

namespace nw = lp::feature::newwoods;

namespace {

struct Hint {
    char const* item;
    char const* key;
    char const* english;
};

constexpr Hint kHints[] = {
    {"lepton:ancient_fruit", "quark.jei.hint.ancient_fruit",
     "Falls from Ashen Trees when they're chopped down. Gives you a small amount of XP when eaten."},
    {"lepton:ancient_sapling", "quark.jei.hint.ancient_sapling", "Can be sometimes sniffed by a Sniffer."},
    {"lepton:blue_blossom_sapling", "quark.jei.hint.blue_blossom_sapling", "Can be found in cold biomes."},
    {"lepton:lavender_blossom_sapling", "quark.jei.hint.lavender_blossom_sapling", "Can be found in swamps."},
    {"lepton:orange_blossom_sapling", "quark.jei.hint.orange_blossom_sapling", "Can be found in savannas."},
    {"lepton:red_blossom_sapling", "quark.jei.hint.red_blossom_sapling", "Can be found in badlands."},
    {"lepton:yellow_blossom_sapling", "quark.jei.hint.yellow_blossom_sapling", "Can be found in plains."},
};

} // namespace

LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    if (item.isNull()) return;
    auto const& id = item.getTypeName();
    if (!id.starts_with("lepton:")) return;
    for (auto const& h : kHints) {
        if (id != h.item) continue;
        if (!nw::moduleEnabled(nw::moduleOfId(id))) return;
        lines.push_back("§7" + client::ctr(h.key, h.english));
        return;
    }
});

} // namespace lp::client::newwoods

#endif
