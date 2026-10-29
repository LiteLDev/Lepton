// /quark gadgets ... (operator): stacks Bedrock can't list in the creative inventory.
//   /quark gadgets quill <biome>        an inscribed Pathfinder's Quill (Java or Bedrock biome id)
//   /quark gadgets potato angry         an Angry Potato (Java's is_angry component)
//   /quark gadgets potato name <name>   a named tiny potato (as an anvil would make it)

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/safety/RedactableString.h"

#include "feature/gadgets/Gadgets.h"
#include "feature/gadgets/Pathfinder.h"
#include "feature/gadgets/Potato.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace lp::feature::gadgets {

// Command parameter types need external linkage.
struct QuillParams {
    std::string biome;
};
struct PotatoNameParams {
    std::string name;
};

namespace {

void give(Player& p, ItemStack s) {
    ItemStack copy = s;
    if (!p.add(copy) || !game::isEmpty(copy)) p.drop(copy, false);
    game::inv::sync(p);
}

/// The trade colour of a known biome, white otherwise.
int colorOf(std::string const& biome) {
    for (auto const& t : pathfinder::tradeList())
        if (t.biome == biome || pathfinder::javaPath(t.biome) == biome) return t.color;
    return -1;
}

std::string opOnly(CommandOrigin const& origin) {
    auto* p = commands::playerOf(origin);
    return p ? trFor(*p, "lp.gadgets.cmd.op_only", "Players with operator permission only.")
             : tr("lp.gadgets.cmd.op_only", "Players with operator permission only.");
}

constexpr std::string_view g_quillId = kQuill;

} // namespace

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<QuillParams>().text("gadgets").text("quill").required("biome").execute(
        [](CommandOrigin const& origin, CommandOutput& output, QuillParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p || !commands::isAdmin(origin)) return output.error(opOnly(origin));
            auto biome = params.biome;
            if (biome.find(':') == std::string::npos) biome = "minecraft:" + biome;
            auto quill = pathfinder::makeQuill(biome, colorOf(biome), p);
            if (game::isEmpty(quill)) return output.error(trFor(*p, "lp.gadgets.cmd.missing", "{} is missing (behaviour pack?)", g_quillId));
            give(*p, quill);
            log().info("gadgets: gave a quill for {} to {}", biome, p->getRealName());
            output.success(trFor(*p, "lp.gadgets.cmd.quill", "Gave a Pathfinder's Quill for {}", pathfinder::biomeName(p, biome)));
        }
    );
    cmd.overload().text("gadgets").text("potato").text("angry").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = commands::playerOf(origin);
        if (!p || !commands::isAdmin(origin)) return output.error(opOnly(origin));
        give(*p, potato::makePotatoItem("", true, p));
        output.success(trFor(*p, "lp.gadgets.cmd.potato", "Gave an Angry Potato"));
    });
    cmd.overload<PotatoNameParams>().text("gadgets").text("potato").text("name").required("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, PotatoNameParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p || !commands::isAdmin(origin)) return output.error(opOnly(origin));
            give(*p, potato::makePotatoItem(params.name, false, p));
            output.success(trFor(*p, "lp.gadgets.cmd.potato_named", "Gave a Tiny Potato named {}", params.name));
        }
    );
});

} // namespace lp::feature::gadgets
