// BucketsShowInhabitants (client): bucket icons show the creature inside (docs/spec/cosmetics.md §3).
// The GUI item renderer draws hidden icon items (lepton:bucket_icon_*) instead of the vanilla icon.
#ifdef LP_CLIENT

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/gui/controls/renderers/BaseActorInventoryRenderContext.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/UIItemRenderInfo.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/deps/core/utility/PropertyBag.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/Int64Tag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "feature/cosmetics/Cosmetics.h"
#include "game/Items.h"
#include "game/Inventory.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

namespace cos = lp::feature::cosmetics;

namespace {

std::optional<int64_t> intOf(CompoundTag const& tag, std::string_view name) {
    Tag const* t = tag.get(name);
    if (!t) return std::nullopt;
    switch (t->getId()) {
    case Tag::Type::Byte:
        return static_cast<int64_t>(static_cast<unsigned char>(static_cast<ByteTag const*>(t)->data));
    case Tag::Type::Short:
        return static_cast<ShortTag const*>(t)->data;
    case Tag::Type::Int:
        return static_cast<IntTag const*>(t)->data;
    case Tag::Type::Int64:
        return static_cast<Int64Tag const*>(t)->data;
    default:
        return std::nullopt;
    }
}

/// Java DyeColor texture diffuse colours, by dye index (white, orange, ..., black).
constexpr std::array<uint32_t, 16> kDye{
    0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
    0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21,
};

/// Render-thread cache of the icon stacks.
ItemStack const& icon(std::string const& key, std::optional<uint32_t> dye = std::nullopt) {
    // Item stacks belong to the item registry of the world they were made in: the cache is
    // dropped when the client joins another world. Missing items are not cached.
    static std::map<std::pair<std::string, uint32_t>, ItemStack> cache;
    static void const*                                          cacheLevel = nullptr;
    auto*                                                       c          = client::currentClient();
    void const*                                                 level      = c ? static_cast<void const*>(c->getLevel()) : nullptr;
    if (level != cacheLevel) {
        cache.clear();
        cacheLevel = level;
    }
    auto k  = std::make_pair(key, dye.value_or(0xFFFFFFFFu));
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    ItemStack stack = game::makeItem("lepton:bucket_icon_" + key);
    if (stack.isNull()) {
        static ItemStack const empty;
        return empty;
    }
    if (!stack.isNull() && dye) {
        auto tag = std::make_unique<CompoundTag>();
        tag->putInt("customColor", static_cast<int>(0xFF000000u | *dye));
        stack.setUserData(std::move(tag));
    }
    return cache.emplace(k, std::move(stack)).first->second;
}

void logOnce(ItemStack const& item) {
    static std::set<std::string> seen;
    auto                         id = item.getTypeName();
    if (!seen.insert(id).second) return;
    log().debug("cosmetics: bucket {} data {}", id, item.mUserData ? item.mUserData->toSnbt(SnbtFormat::Minimize, 0) : "{}");
}

} // namespace

/// The icon layers to draw for `item` instead of its own icon (empty = draw it normally).
std::vector<ItemStack const*> bucketLayers(ItemStack const& item) {
    std::vector<ItemStack const*> out;
    auto const&                   c = cos::config();
    if (!c.enableBucketsShowInhabitants || item.isNull()) return out;
    auto const& id = item.getTypeName();
    if (id != "minecraft:axolotl_bucket" && id != "minecraft:tropical_fish_bucket" && id != "lepton:crab_bucket"
        && id != "lepton:slime_in_a_bucket" && id != "lepton:slime_in_a_bucket_excited")
        return out;
    if (!item.mUserData) return out;
    logOnce(item);
    CompoundTag const& data = *item.mUserData;
    auto push = [&](ItemStack const& s) {
        if (!s.isNull()) out.push_back(&s);
    };
    if (id == "minecraft:axolotl_bucket") {
        if (!c.showAxolotls) return out;
        // Bedrock axolotl variants: 0 lucy, 1 cyan, 2 gold, 3 wild, 4 blue.
        static constexpr char const* kNames[]{"lucy", "cyan", "gold", "wild", "blue"};
        auto v = intOf(data, "Variant");
        if (v && *v >= 0 && *v < 5) push(icon(std::string("axolotl_") + kNames[*v]));
    } else if (id == "minecraft:tropical_fish_bucket") {
        if (!c.showTropicalFish) return out;
        auto shape = intOf(data, "Variant"), pattern = intOf(data, "MarkVariant");
        auto base = intOf(data, "Color"), patternColor = intOf(data, "Color2");
        if (!shape || !pattern || !base || !patternColor) return out;
        std::string s = *shape == 1 ? "b" : "a";
        push(icon("tropical_bucket"));
        push(icon("tropical_" + s, kDye[static_cast<size_t>(*base) & 15]));
        push(icon("tropical_" + s + "_" + std::to_string(std::clamp<int64_t>(*pattern, 0, 5) + 1), kDye[static_cast<size_t>(*patternColor) & 15]));
    } else if (id == "lepton:crab_bucket") {
        if (!c.showCrabs) return out;
        Tag const* t = data.get("lepton_crab");
        if (!t || t->getId() != Tag::Type::String) return out;
        auto j = nlohmann::json::parse(static_cast<std::string const&>(*static_cast<StringTag const*>(t)), nullptr, false);
        if (!j.is_object()) return out;
        static constexpr char const* kNames[]{"red", "blue", "green"};
        int v = j.value("variant", -1);
        if (v >= 0 && v < 3) push(icon(std::string("crab_") + kNames[v]));
    } else {
        if (!c.showShinySlime || !c.enableVariantAnimalTextures || !c.enableShinySlime) return out;
        Tag const* t = data.get("slime_nbt");
        if (!t || t->getId() != Tag::Type::Compound) return out;
        auto uid = intOf(*static_cast<CompoundTag const*>(t), "UniqueID");
        if (uid && cos::isShiny(cos::mostBits(*uid), c.shinyAnimalChance)) push(icon("slime_shiny"));
    }
    if (out.size() != 1 && out.size() != 3) out.clear();
    static std::set<std::string> reported;
    if (reported.insert(id + ":" + std::to_string(out.size())).second)
        log().info("cosmetics: bucket {} drawn with {} icon layer(s)", id, out.size());
    return out;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    GuiItemHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemNew,
    void,
    ::BaseActorRenderContext& ctx,
    ::ItemStack const&        item,
    int                       frame,
    float                     x,
    float                     y,
    bool                      foil,
    float                     transparency,
    float                     light,
    float                     scale,
    int                       zOrder
) {
    std::vector<ItemStack const*> layers;
    try {
        layers = bucketLayers(item);
    } catch (...) {}
    if (layers.empty()) return origin(ctx, item, frame, x, y, foil, transparency, light, scale, zOrder);
    for (auto const* layer : layers) origin(ctx, *layer, frame, x, y, foil, transparency, light, scale, zOrder);
}

LL_TYPE_INSTANCE_HOOK(
    GuiItemChunkHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemInChunk,
    void,
    ::BaseActorRenderContext&                        ctx,
    ::ItemRenderChunkType                            chunk,
    ::ItemStack const&                               item,
    float                                            x,
    float                                            y,
    float                                            light,
    float                                            alpha,
    float                                            scale,
    int                                              frame,
    bool                                             animate,
    int                                              zOrder,
    ::std::optional<::TextureUVCoordinateSet> const& uv
) {
    std::vector<ItemStack const*> layers;
    try {
        layers = bucketLayers(item);
    } catch (...) {}
    if (layers.empty()) return origin(ctx, chunk, item, x, y, light, alpha, scale, frame, animate, zOrder, uv);
    std::optional<TextureUVCoordinateSet> none;
    for (auto const* layer : layers) origin(ctx, chunk, *layer, x, y, light, alpha, scale, frame, animate, zOrder, none);
}

LL_TYPE_INSTANCE_HOOK(
    InventoryItemChunkHook,
    ll::memory::HookPriority::Normal,
    BaseActorInventoryRenderContext,
    &BaseActorInventoryRenderContext::$renderGuiItemInChunk,
    void,
    ::ItemRenderChunkType                            chunk,
    ::ItemStack const&                               item,
    float                                            x,
    float                                            y,
    float                                            light,
    float                                            alpha,
    float                                            scale,
    int                                              frame,
    bool                                             animate,
    int                                              zOrder,
    ::std::optional<::TextureUVCoordinateSet> const& uv
) {
    std::vector<ItemStack const*> layers;
    try {
        layers = bucketLayers(item);
    } catch (...) {}
    if (layers.empty()) return origin(chunk, item, x, y, light, alpha, scale, frame, animate, zOrder, uv);
    std::optional<TextureUVCoordinateSet> none;
    for (auto const* layer : layers) origin(chunk, *layer, x, y, light, alpha, scale, frame, animate, zOrder, none);
}

bool interesting(std::string const& id) {
    return id == "minecraft:axolotl_bucket" || id == "minecraft:tropical_fish_bucket" || id == "lepton:crab_bucket"
        || id == "lepton:slime_in_a_bucket" || id == "lepton:slime_in_a_bucket_excited";
}

/// The real stack behind a UI slot: the renderer only gets id/aux/colour from the slot's property
/// bag ("#collection_name", "#collection_index"), so the bucket's NBT is looked up here.
ItemStack const* realStack(std::string const& collection, int index, std::string const& id) {
    auto* player = client::currentPlayer();
    if (!player || index < 0) return nullptr;
    auto matches = [&](ItemStack const& s) { return !s.isNull() && s.getTypeName() == id && s.mUserData; };
    if (collection == "hotbar_items" || collection == "inventory_items" || collection == "combined_hotbar_and_inventory_items") {
        auto& inv = game::inv::playerInventory(*player);
        for (int slot : {index, index + 9}) {
            if (slot >= inv.getContainerSize()) continue;
            if (collection == "hotbar_items" && slot != index) continue;
            auto const& s = inv.getItem(slot);
            if (matches(s)) return &s;
        }
        return nullptr;
    }
    if (auto const& mgr = player->mContainerManager.get()) {
        auto const& s = mgr->getSlot(index);
        if (matches(s)) return &s;
    }
    return nullptr;
}

/// Gives the renderer's stripped copy of a bucket the NBT of the real stack, so bucketLayers can
/// read the creature inside.
LL_TYPE_INSTANCE_HOOK(SlotUpdateHook, ll::memory::HookPriority::Normal, InventoryItemRenderer, &InventoryItemRenderer::_update, bool,
                      InventoryItemRenderer::UpdateData const& data) {
    bool r = origin(data);
    try {
        ItemStack& it = mItemInstance;
        if (it.isNull() || !cos::config().enableBucketsShowInhabitants) return r;
        auto id = it.getTypeName();
        if (!interesting(id)) return r;
        PropertyBag const* bag = data.mOwnerPropertyBag;
        if (!bag) return r;
        Json::Value const& json = bag->mJsonValue;
        auto collection = json.get("#collection_name", Json::Value()).asString("");
        int  index      = json.get("#collection_index", Json::Value()).asInt(-1);
        if (auto const* real = realStack(collection, index, id)) it.setUserData(real->mUserData->clone());
    } catch (...) {}
    return r;
}

class Buckets {
public:
    void install() {
        GuiItemHook::hook();
        GuiItemChunkHook::hook();
        InventoryItemChunkHook::hook();
        SlotUpdateHook::hook();
    }
    void uninstall() {
        SlotUpdateHook::unhook();
        InventoryItemChunkHook::unhook();
        GuiItemChunkHook::unhook();
        GuiItemHook::unhook();
    }
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::Buckets);

#endif // LP_CLIENT
