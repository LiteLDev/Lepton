// U23 gadgets: config, protocol messages, shared helpers and the unit module that dispatches the
// game events to the module files. See docs/spec/gadgets.md.

#include "feature/gadgets/Gadgets.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/StopSoundPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

LP_REGISTER_CONFIG("gadgets", lp::feature::gadgets::GadgetsConfig);
LP_REGISTER_MESSAGE(lp::feature::gadgets::AbacusMessage);
LP_REGISTER_MESSAGE(lp::feature::gadgets::QuillMessage);
LP_REGISTER_MESSAGE(lp::feature::gadgets::PotatoNameMessage);

namespace lp::feature::gadgets {

std::vector<TradeEntry> defaultTrades() {
    auto t = [](char const* biome, int level, int lo, int hi, char const* color) {
        return TradeEntry{std::string("minecraft:") + biome, true, level, lo, hi, color};
    };
    return {
        t("snowy_plains", 4, 8, 14, "FF7FE4FF"),
        t("windswept_hills", 4, 8, 14, "FF8A8A8A"),
        t("dark_forest", 4, 8, 14, "FF00590A"),
        t("desert", 4, 8, 14, "FFCCB94E"),
        t("savanna", 4, 8, 14, "FF9BA562"),
        t("swamp", 4, 12, 18, "FF22370F"),
        t("mangrove_swamp", 4, 12, 18, "FF22370F"),
        t("old_growth_pine_taiga", 4, 12, 18, "FF5B421F"),
        t("flower_forest", 5, 16, 22, "FFCE46E2"),
        t("jungle", 5, 16, 22, "FF22B600"),
        t("bamboo_jungle", 5, 16, 22, "FF3DE217"),
        t("badlands", 5, 16, 22, "FFC67F22"),
        t("mushroom_fields", 5, 20, 26, "FF4D4273"),
        t("ice_spikes", 5, 20, 26, "FF1EC0C9"),
        t("cherry_grove", 5, 20, 26, "FFE9A9E8"),
    };
}

GadgetsConfig const& cfg() { return config::get<GadgetsConfig>(); }

std::string ambientNameOf(std::string_view itemId) {
    constexpr std::string_view prefix = "lepton:music_disc_";
    if (!itemId.starts_with(prefix)) return {};
    auto rest = itemId.substr(prefix.size());
    for (auto const* n : kAmbientNames)
        if (rest == n) return std::string(rest);
    return {};
}

// ---- scheduler ---------------------------------------------------------------------------------------

namespace {
struct Task {
    uint64_t              at;
    std::function<void()> fn;
};
uint64_t         gTick = 0;
std::deque<Task> gTasks;

void runTasks() {
    // tasks may schedule tasks: take the due ones first
    std::vector<Task> due;
    for (auto it = gTasks.begin(); it != gTasks.end();) {
        if (it->at <= gTick) {
            due.push_back(std::move(*it));
            it = gTasks.erase(it);
        } else ++it;
    }
    for (auto& t : due) t.fn();
}
} // namespace

uint64_t now() { return gTick; }
void     later(int ticks, std::function<void()> fn) { gTasks.push_back({gTick + std::max(ticks, 1), std::move(fn)}); }

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

// ---- packets -----------------------------------------------------------------------------------------

void actionBar(Player& player, std::string const& text) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!pkt) return;
    auto& title        = static_cast<SetTitlePacket&>(*pkt);
    title.mType        = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText   = text;
    title.mFadeInTime  = 0;
    title.mStayTime    = 40;
    title.mFadeOutTime = 10;
    title.sendTo(player);
}

void playSoundTo(Player& player, std::string const& name, Vec3 const& pos, float volume, float pitch) {
    PlaySoundPacket pkt;
    pkt.mName   = name;
    pkt.mPos    = BlockPos(static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8));
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.mLoopCount = 0;
    pkt.mBypassListenerRangeCheck = false;
    pkt.sendTo(player);
}

void playSoundNear(int dim, std::string const& name, Vec3 const& pos, float volume, float pitch, float range) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == dim && p.getPosition().distanceTo(pos) <= range)
            playSoundTo(p, name, pos, volume, pitch);
        return true;
    });
}

void stopSoundTo(Player& player, std::string const& name) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::StopSound);
    if (!pkt) return;
    auto& stop            = static_cast<StopSoundPacket&>(*pkt);
    stop.mName            = name;
    stop.mStopAll         = false;
    stop.mStopMusicLegacy = false;
    stop.sendTo(player);
}

void particle(int dim, std::string const& effect, Vec3 const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return;
    level->spawnParticleEffect(effect, pos, d.get());
}

// ---- item user data ----------------------------------------------------------------------------------

CompoundTag& userData(ItemStackBase& stack) {
    if (!stack.mUserData) stack.mUserData = std::make_unique<CompoundTag>();
    return *stack.mUserData;
}

std::optional<int> getInt(ItemStackBase const& stack, std::string_view key) {
    if (!stack.mUserData) return std::nullopt;
    auto const* t = stack.mUserData->get(key);
    if (!t) return std::nullopt;
    switch (t->getId()) {
    case Tag::Type::Int: return static_cast<IntTag const*>(t)->data;
    case Tag::Type::Byte: return static_cast<ByteTag const*>(t)->data;
    default: return std::nullopt;
    }
}

std::optional<std::string> getString(ItemStackBase const& stack, std::string_view key) {
    if (!stack.mUserData) return std::nullopt;
    auto const* t = stack.mUserData->get(key);
    if (!t || t->getId() != Tag::Type::String) return std::nullopt;
    return std::string(static_cast<StringTag const&>(*t));
}

bool getBool(ItemStackBase const& stack, std::string_view key) { return getInt(stack, key).value_or(0) != 0; }

void putInt(ItemStackBase& stack, std::string const& key, int value) { userData(stack).putInt(key, value); }
void putString(ItemStackBase& stack, std::string const& key, std::string const& value) {
    userData(stack).putString(key, value);
}
void putBool(ItemStackBase& stack, std::string const& key, bool value) {
    userData(stack).putByte(key, static_cast<uchar>(value ? 1 : 0));
}
void removeKey(ItemStackBase& stack, std::string_view key) {
    if (!stack.mUserData) return;
    stack.mUserData->remove(key);
    if (stack.mUserData->empty()) stack.mUserData.reset();
}

// ---- recipes -----------------------------------------------------------------------------------------

namespace {
/// Removed recipes stay alive here: other recipe indexes may still point at them until the level
/// is gone, so nothing dangles.
std::vector<std::shared_ptr<Recipe>> gRemovedRecipes;
} // namespace

int removeRecipes(std::vector<std::string> const& ids) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto& recipes = level->getRecipes();
    int   removed = 0;
    for (auto const& id : ids) {
        for (auto& [tag, byId] : *recipes.mRecipes) {
            auto it = byId.find(id);
            if (it == byId.end()) continue;
            Recipe* raw = it->second.get();
            gRemovedRecipes.push_back(it->second);
            byId.erase(it);
            for (auto& [out, m] : *recipes.mRecipesByOutput) m.erase(id);
            // mRecipesByNetId keeps its entry (RecipeNetId has no usable std::hash here): clients never
            // learn the id, since the crafting data sent to them no longer lists the recipe.
            auto& unl = *recipes.mUnlockableRecipes;
            unl.erase(std::remove_if(unl.begin(), unl.end(), [&](auto const& r) { return r.get() == raw; }), unl.end());
            ++removed;
        }
    }
    if (removed) { // Recipes::notifyRecipeListeners (client-only symbol): the crafting data is rebuilt
        for (auto const& [life, fn] : *recipes.mListeners)
            if (!life.expired() && fn) fn();
    }
    return removed;
}

bool hasRecipe(std::string const& id) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    for (auto const& [tag, byId] : *level->getRecipes().mRecipes)
        if (byId.contains(id)) return true;
    return false;
}

bool potatoUsesHeart() {
    auto const& c = cfg();
    if (!c.enableTinyPotato || c.neverUseHeartOfDiamond) return false;
    // the heart recipe needs Stonelings' Heart of Diamond (U21, config/mobsdeep.json)
    bool stonelings = true;
    try {
        auto path = config::configDir() / "mobsdeep.json";
        if (std::filesystem::exists(path)) {
            std::ifstream in(path);
            auto          j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object() && j.contains("enableStonelings") && j["enableStonelings"].is_boolean())
                stonelings = j["enableStonelings"].get<bool>();
        }
    } catch (...) {}
    return stonelings && !game::isEmpty(game::makeItem("lepton:diamond_heart"));
}

// ---- the unit module -------------------------------------------------------------------------------

namespace {

bool gInstalled = false;

void applyRecipeRules() {
    auto const&              c = cfg();
    std::vector<std::string> drop;
    if (!c.enableAbacus) drop.push_back("lepton:abacus");
    // TinyPotato: the heart recipe with Stonelings' Heart of Diamond, otherwise diamond / emerald.
    bool heart = potatoUsesHeart();
    if (!c.enableTinyPotato || !heart) drop.push_back("lepton:tiny_potato_heart");
    if (!c.enableTinyPotato || heart) {
        drop.push_back("lepton:tiny_potato_no_heart");
        drop.push_back("lepton:tiny_potato_no_heart_emerald");
    }
    int n = removeRecipes(drop);
    log().info("gadgets: {} recipe(s) disabled (tiny potato uses the {} recipe)", n, heart ? "heart" : "diamond/emerald");
}

struct GadgetsModule {
    ll::event::ListenerPtr mTick;

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            ++gTick;
            runTasks();
            abacus::tick();
            pathfinder::tick();
            discs::tick();
            compasses::tick();
            potato::tick();
        });
        abacus::install();
        pathfinder::install();
        discs::install();
        potato::install();
        static bool registered = false; // world lifecycle listeners can't be removed: add them once
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                if (!gInstalled) return;
                applyRecipeRules();
                potato::onWorldReady();
            });
            game::db::onWorldSave([] {
                if (gInstalled) potato::onWorldSave();
            });
            game::db::onWorldClosed([] {
                gTasks.clear();
                gRemovedRecipes.clear(); // the level that held them is closing
                pathfinder::onWorldClosed();
                discs::onWorldClosed();
                potato::onWorldClosed();
            });
        }
        gInstalled = true;
        if (game::db::available()) {
            applyRecipeRules();
            potato::onWorldReady();
        }
        log().info("gadgets: installed");
    }

    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        gInstalled = false;
        abacus::uninstall();
        pathfinder::uninstall();
        discs::uninstall();
        potato::uninstall();
        gTasks.clear();
    }
};

} // namespace
} // namespace lp::feature::gadgets

LP_REGISTER_MODULE(lp::feature::gadgets::GadgetsModule);
