// U2 stones, client side: the celebratory lamps' tooltip line (Quark shows it in advanced tooltips;
// Bedrock has no advanced-tooltip mode, so the client mod always shows it).
#ifdef LP_CLIENT

#include <string>
#include <vector>

#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/stones/Stones.h"
#include "game/Items.h"

LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    if (item.isNull() || !lp::feature::stones::isLamp(lp::game::itemId(item))) return;
    lines.push_back("§7" + lp::client::ctr("quark.misc.celebration", "Celebrating 10 Years of Vazkii's Mods (2021)"));
});

#endif
