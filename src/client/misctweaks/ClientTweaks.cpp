#ifdef LP_CLIENT

// Client parts of U27 misctweaks: the "(Was Repaired)" tooltip (GameNerfs), AdjustableChat,
// NarratorReadout and AutomaticRecipeUnlock's toast filter. Options come from the client's own
// config/misctweaks.json (as Quark reads its client config).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/models/ClientInstanceModel.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/packet/UnlockedRecipesPacket.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/phys/HitResult.h"

#include "client/Client.h"
#include "client/misctweaks/Speech.h"
#include "client/ui/Ui.h"
#include "feature/misctweaks/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::misctweaks {

using feature::misctweaks::cfg;

namespace {

// ---- GameNerfs: "(Was Repaired)" ----------------------------------------------------------------

LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    auto const& c = cfg();
    if (!c.enableGameNerfs || !c.nerfMending || item.isNull()) return;
    if (item.getBaseRepairCost() > 0) lines.push_back("§e" + ctr("quark.misc.repaired", "(Was Repaired)"));
});

// ---- AdjustableChat --------------------------------------------------------------------------------

struct ChatShift {
    std::weak_ptr<UIControl> control;
    float                    baseX = 0, baseY = 0;       ///< position the game laid out
    float                    appliedX = 0, appliedY = 0; ///< position we set (base + shift)
    bool                     valid = false;
};
ChatShift gChat;
UIControl const* gChatMissRoot = nullptr; ///< HUD root searched without finding the chat panel

std::shared_ptr<UIControl> findControl(std::shared_ptr<UIControl> const& c, std::string_view name, int depth = 0) {
    if (!c || depth > 40) return nullptr;
    if (c->mName.get() == name) return c;
    for (auto const& child : c->mChildren.get())
        if (auto found = findControl(child, name, depth + 1)) return found;
    return nullptr;
}

/// Moves the HUD's chat panel by (horizontalShift, verticalShift) UI units, like Quark's pose
/// translation around the chat layer. The game lays the panel out again when the chat grows:
/// a position we did not set is the new base.
void shiftChat(ScreenView& view) {
    auto const& tree = view.mVisualTree.get();
    if (!tree || tree->mRootControlName.get().find("hud_screen") == std::string::npos) return;
    auto const& c  = cfg();
    float       dx = c.enableAdjustableChat ? static_cast<float>(c.horizontalShift) : 0.f;
    float       dy = c.enableAdjustableChat ? static_cast<float>(c.verticalShift) : 0.f;
    auto        panel = gChat.control.lock();
    if (!panel) {
        if (dx == 0 && dy == 0) return;
        auto const& root = tree->mRootControl;
        if (root.get() == gChatMissRoot) return; // searched this tree already
        panel = findControl(root, "chat_panel");
        if (!panel) {
            gChatMissRoot = root.get();
            return;
        }
        gChat = ChatShift{panel};
    }
    auto& rel = panel->mParentRelativePosition.get();
    if (!gChat.valid || rel.x != gChat.appliedX || rel.y != gChat.appliedY) {
        gChat.baseX = rel.x;
        gChat.baseY = rel.y;
        gChat.valid = true;
    }
    float x = gChat.baseX + dx, y = gChat.baseY + dy;
    if (rel.x == x && rel.y == y) {
        gChat.appliedX = x;
        gChat.appliedY = y;
        return;
    }
    rel.x          = x;
    rel.y          = y;
    gChat.appliedX = x;
    gChat.appliedY = y;
    panel->_setPositionDirty();
    static bool logged = false;
    if (!logged && (dx != 0 || dy != 0)) {
        logged = true;
        log().info("misctweaks: chat panel shifted by {},{}", dx, dy);
    }
}

// ---- NarratorReadout --------------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

struct Hover {
    std::string       text; ///< the game's hover text of the last hovered item
    Clock::time_point when;
};
Hover             gHover;
std::set<std::string> gSlotScreens; ///< screens seen with item slots (container screens)
/// The game builds an item's hover text once when the mouse reaches its slot: the text is current
/// until the mouse moves again.
std::atomic<Clock::rep> gLastMouseMove{0}; ///< written on the window thread
Clock::time_point gLastReadout{};

std::string stripFormatting(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        // "§" is two bytes in UTF-8 (C2 A7), followed by the format code.
        if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out += s[i];
    }
    return out;
}

std::vector<std::string> splitLines(std::string const& s) {
    std::vector<std::string> out;
    size_t                   start = 0;
    while (start <= s.size()) {
        auto end  = s.find('\n', start);
        auto line = stripFormatting(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!line.empty()) out.push_back(line);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

LL_TYPE_INSTANCE_HOOK(
    HoverTextHook,
    ll::memory::HookPriority::Low,
    ClientInstanceModel,
    &ClientInstanceModel::$getFormattedHoverText,
    std::string,
    ItemStackBase const& item,
    bool const           showCategory
) {
    auto text = origin(item, showCategory);
    if (!item.isNull()) gHover = Hover{text, Clock::now()};
    return text;
}

LP_REGISTER_SCREEN_OVERLAY("*", [](ui::Overlay&, ui::ScreenInfo const& s) {
    if (!s.slots.empty()) gSlotScreens.insert(s.name);
});

std::string blockName(Block const& block) {
    auto name = stripFormatting(block.buildDescriptionName());
    return name.empty() ? block.getTypeName() : name;
}

std::string signText(BlockSource& region, BlockPos const& pos) {
    auto* be = region.getBlockEntity(pos);
    if (!be) return {};
    auto*       sign = static_cast<SignBlockActor*>(be);
    std::string out;
    for (auto side : {SignTextSide::Front, SignTextSide::Back}) {
        out += ctr("quark.readout.sign_says", ". Sign Says ");
        for (auto const& line : splitLines(sign->getMessage(side))) {
            auto trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(' '));
            if (trimmed.empty()) continue;
            out += line + " ";
        }
        out += ". ";
    }
    return out;
}

/// Quark's NarratorReadoutModule.getReadout.
std::string readout(bool full) {
    LocalPlayer* player = currentPlayer();
    if (!player || !inLevel()) return ctr("quark.readout.not_ingame", "Not ingame");
    std::string sb;
    std::string screen = ui::topScreen();
    if (screen == "hud_screen") {
        HitResult const& hit = player->getLevel().getHitResult();
        if (hit.mType == HitResultType::Tile) {
            auto&        region = player->getDimensionBlockSource();
            Block const& block  = region.getBlock(hit.mBlock);
            sb += ctr("quark.readout.looking", "Looking at {}", blockName(block));
            if (full) sb += ", ";
            if (block.getTypeName().find("sign") != std::string::npos) sb += signText(region, hit.mBlock);
        }
        if (full) {
            ItemStack const* main = &player->getCarriedItem();
            ItemStack const* off  = &player->getOffhandSlot();
            if (main->isNull()) std::swap(main, off), off = &ItemStack::EMPTY_ITEM();
            if (!main->isNull()) {
                if (!off->isNull())
                    sb += ctr("quark.readout.holding_with_off", "Holding {} {} and {} {}", static_cast<int>(main->mCount),
                              stripFormatting(main->getHoverName()), static_cast<int>(off->mCount), stripFormatting(off->getHoverName()));
                else sb += ctr("quark.readout.holding", "Holding {}x {}", static_cast<int>(main->mCount), stripFormatting(main->getHoverName()));
                sb += ", ";
            }
            sb += ctr("quark.readout.health", "Health {}", player->getHealth());
            sb += ", ";
            auto food = player->getAttribute(Player::HUNGER());
            sb += ctr("quark.readout.food", "Food {}", food.mPtr ? static_cast<int>(food.mPtr->mCurrentValue) : 0);
        }
    } else if (bool hovering = !gHover.text.empty() && gHover.when.time_since_epoch().count() >= gLastMouseMove.load();
               hovering || gSlotScreens.contains(screen)) {
        if (!hovering) sb += ctr("quark.readout.no_item", "No item");
        else {
            for (auto const& line : splitLines(gHover.text)) {
                sb += line;
                if (!full) break;
                sb += ", ";
            }
        }
    } else {
        // Bedrock screens have no narration message: read the screen's name.
        std::string name = screen;
        if (name.ends_with("_screen")) name.resize(name.size() - 7);
        std::replace(name.begin(), name.end(), '_', ' ');
        sb += name;
    }
    return sb;
}

void acceptInput(bool full) {
    if (!cfg().enableNarratorReadout) return;
    auto now = Clock::now();
    if (now - gLastReadout <= std::chrono::milliseconds(500)) return; // 10 ticks
    auto text = readout(full);
    if (text.empty()) return;
    gLastReadout = now;
    bool spoken  = say(text);
    log().info("misctweaks: narrator{} '{}'{}", full ? " (full)" : "", text, spoken ? "" : " (no speech API)");
}

// Quark calls acceptInput(down || full, down): its "narrator_readout" key gives the full readout and
// "narrator_full_readout" the short one. Kept as Quark behaves.
LP_REGISTER_KEYBIND("narrator_readout", ui::Key::Unbound, [] { acceptInput(true); }, {}, true);
LP_REGISTER_KEYBIND("narrator_full_readout", ui::Key::Unbound, [] { acceptInput(false); }, {}, true);

// ---- AutomaticRecipeUnlock: no toast for the mass unlock --------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    UnlockedRecipesHook,
    ll::memory::HookPriority::Normal,
    ClientNetworkHandler,
    &ClientNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     source,
    UnlockedRecipesPacket const& packet
) {
    using Type = UnlockedRecipesPacketPayload::PacketType;
    auto& p    = const_cast<UnlockedRecipesPacket&>(packet);
    if (cfg().enableAutomaticRecipeUnlock && p.mPacketType == Type::NewlyUnlockedRecipes && p.mUnlockedRecipes->size() > 100) {
        p.mPacketType = Type::InitiallyUnlockedRecipes; // Quark drops the recipe toast past 100 unlocks
        log().info("misctweaks: {} recipes unlocked without a toast", p.mUnlockedRecipes->size());
    }
    origin(source, packet);
}

// ---- e2e aid: anvil slot positions --------------------------------------------------------------------

LP_REGISTER_SCREEN_OVERLAY("anvil_screen", [](ui::Overlay&, ui::ScreenInfo const& s) {
    static bool logged = false;
    if (s.frames <= 1) logged = false;
    if (!s.settled || logged) return;
    logged = true;
    for (auto const& slot : s.slots) {
        if (slot.collection != "hotbar_items") continue;
        log().info("misctweaks: anvil slot {}:{} at {:.4f},{:.4f}", slot.collection, slot.index,
                   (slot.rect.x + slot.rect.w / 2) / s.size.x, (slot.rect.y + slot.rect.h / 2) / s.size.y);
    }
});

/// e2e aid: the anvil's own slots are plain controls (no item collection), found by name.
void logAnvilSlots(ScreenView& view) {
    static int                                   frames = 0;
    static std::chrono::steady_clock::time_point last;
    auto const&                                  tree = view.mVisualTree.get();
    if (!tree || tree->mRootControlName.get().find("anvil_screen") == std::string::npos) return;
    auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::milliseconds(500)) frames = 0; // a new opening (other screens render too)
    last = now;
    if (++frames != 90) return; // once per opening, after the screen slid in
    auto const& size = view.mSize.get();
    for (auto name : {"input_item_slot", "material_item_slot", "result_item_slot"}) {
        auto slot = findControl(tree->mRootControl, name);
        if (!slot) continue;
        auto const& p = slot->mCachedPosition.get();
        auto const& s = slot->mSize.get();
        log().info("misctweaks: anvil slot {}:0 at {:.4f},{:.4f}", name, (p.x + s.x / 2) / size.x, (p.y + s.y / 2) / size.y);
    }
}
class ClientTweaksModule {
public:
    void install() {
        mRender = ll::event::EventBus::getInstance().emplaceListener<ll::event::BeforeUIRenderEvent>(
            [](ll::event::BeforeUIRenderEvent& ev) {
                shiftChat(ev.screenView());
                logAnvilSlots(ev.screenView());
            }
        );
        mMouse = ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
            if (ev.actionButtonId() == 0) gLastMouseMove = Clock::now().time_since_epoch().count(); // 0 = movement
        });
        HoverTextHook::hook();
        UnlockedRecipesHook::hook();
    }
    void uninstall() {
        if (mRender) ll::event::EventBus::getInstance().removeListener(mRender);
        mRender = nullptr;
        if (mMouse) ll::event::EventBus::getInstance().removeListener(mMouse);
        mMouse = nullptr;
        UnlockedRecipesHook::unhook();
        HoverTextHook::unhook();
        stopSpeech();
    }

private:
    ll::event::ListenerPtr mRender;
    ll::event::ListenerPtr mMouse;
};

} // namespace

} // namespace lp::client::misctweaks

LP_REGISTER_MODULE(lp::client::misctweaks::ClientTweaksModule);

#endif
