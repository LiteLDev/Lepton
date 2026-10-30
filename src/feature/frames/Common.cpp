// U24 frames: config registration, the unit's module, and small helpers shared by the glass and
// dyed frames (sounds, drops, entity properties, recipes, cauldrons, dye colours).

#include "feature/frames/Frames.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <random>
#include <variant>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/ActorAttribute.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("frames", lp::feature::frames::FramesConfig);

namespace lp::feature::frames {

namespace bs = game::blockstates;

FramesConfig const& cfg() { return config::get<FramesConfig>(); }

namespace {
std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float rand01() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
} // namespace

// ---- sounds, drops, inventory ---------------------------------------------------------------------

void playSound(BlockSource& region, Vec3 const& pos, std::string const& sound, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p   = static_cast<PlaySoundPacket&>(*pkt);
    p.mName   = sound;
    p.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    p.mVolume = volume;
    p.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(game::blockPosOf(pos), p, nullptr);
}

void dropItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos) {
    auto level = ll::service::getLevel();
    if (!level || stack.isNull() || stack.mCount == 0) return;
    ItemActor* actor = level->getSpawner().spawnItem(region, stack, nullptr, pos, 0);
    if (!actor) return;
    actor->mBuiltInComponents->mStateVectorComponent->mPosDelta =
        Vec3{(rand01() - 0.5f) * 0.1f, 0.2f, (rand01() - 0.5f) * 0.1f};
    actor->mPickupDelay = 10;
}

void giveOrDrop(Player& player, ItemStack const& stack) {
    if (stack.isNull() || stack.mCount == 0) return;
    ItemStack copy = stack;
    auto&     inv  = game::inv::playerInventory(player);
    game::inv::insert(inv, copy, game::inv::kPlayerAll);
    if (copy.mCount > 0) player.drop(copy, false);
    game::inv::sync(player);
}

void exchangeOne(Player& player, ItemStack const& result) {
    auto&     inv  = game::inv::playerInventory(player);
    int       slot = player.getSelectedItemSlot();
    ItemStack held = inv.getItem(slot);
    if (player.isCreative()) {
        giveOrDrop(player, result);
        return;
    }
    if (held.mCount <= 1) {
        inv.setItem(slot, result);
        game::inv::sync(player);
        return;
    }
    held.remove(1);
    inv.setItem(slot, held);
    giveOrDrop(player, result);
}

// ---- directions ---------------------------------------------------------------------------------------

int dirFromName(std::string_view name) {
    static constexpr std::string_view names[6]{"down", "up", "north", "south", "west", "east"};
    for (int i = 0; i < 6; ++i)
        if (names[i] == name) return i;
    return -1;
}

BlockPos offset(BlockPos const& pos, int dir) {
    switch (dir) {
    case 0: return {pos.x, pos.y - 1, pos.z};
    case 1: return {pos.x, pos.y + 1, pos.z};
    case 2: return {pos.x, pos.y, pos.z - 1};
    case 3: return {pos.x, pos.y, pos.z + 1};
    case 4: return {pos.x - 1, pos.y, pos.z};
    case 5: return {pos.x + 1, pos.y, pos.z};
    default: return pos;
    }
}

float yawFor(int dir) {
    switch (dir) {
    case 2: return 180.f; // north
    case 4: return 90.f;  // west
    case 5: return -90.f; // east
    default: return 0.f;  // south, up, down
    }
}

// ---- entity properties ------------------------------------------------------------------------------

namespace {
optional_ref<PropertyComponent> props(Actor& actor) { return actor.getEntityContext().tryGetComponent<PropertyComponent>(); }
} // namespace

bool setIntProperty(Actor& actor, std::string_view name, int value) {
    auto p = props(actor);
    return p && ActorPropertyUtils::tryQueueIntValue(*p, actor, HashedString(name).getHash(), value);
}
bool setBoolProperty(Actor& actor, std::string_view name, bool value) {
    auto p = props(actor);
    return p && ActorPropertyUtils::tryQueueBoolValue(*p, actor, HashedString(name).getHash(), value);
}
bool setFloatProperty(Actor& actor, std::string_view name, float value) {
    auto p = props(actor);
    return p && ActorPropertyUtils::tryQueueFloatValue(*p, actor, HashedString(name).getHash(), value);
}

namespace {
template <class T>
std::optional<T> readProperty(Actor& actor, std::string_view name) {
    auto p = props(actor);
    if (!p) return std::nullopt;
    auto v = p->mPropertyContainer->getValueVariant(HashedString(name).getHash());
    if (!v) return std::nullopt;
    if (auto const* i = std::get_if<int>(&*v)) return static_cast<T>(*i);
    if (auto const* b = std::get_if<bool>(&*v)) return static_cast<T>(*b ? 1 : 0);
    if (auto const* f = std::get_if<float>(&*v)) return static_cast<T>(*f);
    return std::nullopt;
}
} // namespace

std::optional<int>   intProperty(Actor& actor, std::string_view name) { return readProperty<int>(actor, name); }
std::optional<float> floatProperty(Actor& actor, std::string_view name) { return readProperty<float>(actor, name); }

void orient(Actor& actor, float yaw) {
    actor.mBuiltInComponents->mActorRotationComponent->mRot     = Vec2{0.f, yaw};
    actor.mBuiltInComponents->mActorRotationComponent->mRotPrev = Vec2{0.f, yaw};
    if (actor.hasCategory(ActorCategory::Mob)) static_cast<Mob&>(actor).setYBodyRotations(yaw, yaw);
}

bool frameMayPlace(BlockSource& region, BlockPos const& target, int dir) {
    auto const* frame = bs::of("minecraft:frame");
    if (!frame || dir < 0 || dir > 5) return false;
    if (!game::replaceable(region, target)) return false;
    return frame->getBlockType().mayPlace(region, target, static_cast<uchar>(dir));
}

std::optional<core::GlobalPos> parseKey(std::string_view key) {
    int  v[4]{};
    int  n = 0;
    auto rest = key;
    while (n < 4) {
        auto cut  = rest.find(':');
        auto part = rest.substr(0, cut);
        auto r    = std::from_chars(part.data(), part.data() + part.size(), v[n]);
        if (r.ec != std::errc{} || r.ptr != part.data() + part.size()) return std::nullopt;
        ++n;
        if (cut == std::string_view::npos) break;
        rest = rest.substr(cut + 1);
    }
    if (n != 4) return std::nullopt;
    return core::GlobalPos{v[0], v[1], v[2], v[3]};
}

// ---- recipes ------------------------------------------------------------------------------------------

namespace {
struct Removed {
    HashedString              tag;
    std::string               id;
    std::shared_ptr<Recipe>   recipe;
    std::vector<ItemInstance> outputKeys;
    bool                      unlockable = false;
};
struct RecipeStash {
    Recipes const*       owner = nullptr;
    std::vector<Removed> removed;
};
RecipeStash& stash() {
    static RecipeStash s;
    return s;
}

void notify(Recipes& recipes) {
    for (auto const& [life, fn] : recipes.mListeners.get())
        if (!life.expired() && fn) fn();
}
} // namespace

void setRecipesEnabled(std::vector<std::string> const& ids, bool enabled) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& recipes = level->getRecipes();
    auto& s       = stash();
    if (s.owner != &recipes) {
        s.owner = &recipes;
        s.removed.clear(); // a new world: its recipes are complete
    }
    bool changed = false;
    if (!enabled) {
        for (auto const& id : ids) {
            for (auto& [tag, byId] : recipes.mRecipes.get()) {
                auto it = byId.find(id);
                if (it == byId.end()) continue;
                Removed r{tag, id, it->second, {}, false};
                byId.erase(it);
                Recipe* ptr = r.recipe.get();
                for (auto& [key, m] : recipes.mRecipesByOutput.get()) {
                    auto f = m.find(id);
                    if (f != m.end() && f->second == ptr) {
                        m.erase(f);
                        r.outputKeys.push_back(key);
                    }
                }
                auto& unl = recipes.mUnlockableRecipes.get();
                auto  sz  = unl.size();
                std::erase_if(unl, [&](auto const& p) { return p.get() == ptr; });
                r.unlockable = unl.size() != sz;
                s.removed.push_back(std::move(r));
                changed = true;
                break;
            }
        }
    } else {
        for (auto it = s.removed.begin(); it != s.removed.end();) {
            if (std::find(ids.begin(), ids.end(), it->id) == ids.end()) {
                ++it;
                continue;
            }
            recipes.mRecipes.get()[it->tag][it->id] = it->recipe;
            Recipe* ptr                             = it->recipe.get();
            for (auto const& key : it->outputKeys)
                for (auto& [k, m] : recipes.mRecipesByOutput.get())
                    if (k.getTypeName() == key.getTypeName() && k.getAuxValue() == key.getAuxValue()) {
                        m[it->id] = ptr;
                        break;
                    }
            if (it->unlockable) recipes.mUnlockableRecipes.get().emplace_back(ptr);
            it      = s.removed.erase(it);
            changed = true;
        }
    }
    if (changed) notify(recipes);
}

void forgetRemovedRecipes() {
    stash().owner = nullptr;
    stash().removed.clear();
}

bool hasRecipe(std::string const& id) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    for (auto const& [tag, byId] : level->getRecipes().mRecipes.get())
        if (byId.contains(id)) return true;
    return false;
}

// ---- cauldrons ------------------------------------------------------------------------------------------

std::optional<CauldronInfo> cauldronAt(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (block.getTypeName() != "minecraft:cauldron") return std::nullopt;
    CauldronInfo info;
    info.level  = bs::getInt(block, "fill_level").value_or(0);
    auto liquid = bs::getString(block, "cauldron_liquid").value_or("water");
    info.water  = liquid == "water" && info.level > 0;
    if (auto* be = region.getBlockEntity(pos)) {
        auto& c     = static_cast<CauldronBlockActor&>(*be);
        info.potion = c.mPotionId != -1;
        info.dyed   = c.mHasCustomColor && !info.potion;
        info.rgb    = c.mCustomColor & 0xFFFFFF;
    }
    if (info.potion) info.water = false;
    return info;
}

void lowerCauldron(BlockSource& region, BlockPos const& pos, int levels) {
    auto const& block = region.getBlock(pos);
    int         level = bs::getInt(block, "fill_level").value_or(0);
    int         next  = std::max(0, level - levels);
    Block const* nb   = bs::with(block, {{"fill_level", next}});
    if (!nb) return;
    if (next == 0) {
        if (auto const* empty = bs::with(*nb, {{"cauldron_liquid", std::string("water")}})) nb = empty;
        if (auto* be = region.getBlockEntity(pos)) {
            auto& c = static_cast<CauldronBlockActor&>(*be);
            c.mHasCustomColor = false;
            c.mPotionId       = -1;
        }
    }
    game::setBlock(region, pos, *nb);
}

// ---- dye colours ---------------------------------------------------------------------------------------

std::optional<int> itemColor(ItemStackBase const& stack) {
    auto const* item = stack.mItem.get();
    if (!item || !item->hasCustomColor(stack)) return std::nullopt;
    auto c = stack.getColor();
    return static_cast<int>(c.toARGB() & 0xFFFFFF);
}

void setItemColor(ItemStack& stack, int rgb) {
    auto const* item = stack.mItem.get();
    if (!item) return;
    item->setColor(stack, mce::Color(static_cast<uint>(rgb & 0xFFFFFF)));
}

// ---- module ------------------------------------------------------------------------------------------------

namespace {
struct FramesModule {
    void install() {
        glass::install();
        dyed::install();
        banners::install();
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] { forgetRemovedRecipes(); }); // they belong to the closed world
            config::onReload([] {
                glass::applyConfig();
                dyed::applyConfig();
            });
        }
        log().info("frames: installed (glass {}, dyeable {}, banner layers {} -> {}, map washing {})",
                   cfg().enableGlassItemFrame, cfg().enableDyeableItemFrames, cfg().enableMoreBannerLayers,
                   banners::effectiveLimit(), cfg().enableMapWashing);
    }
    void uninstall() {
        banners::uninstall();
        dyed::uninstall();
        glass::uninstall();
    }
};
} // namespace

} // namespace lp::feature::frames

LP_REGISTER_MODULE(lp::feature::frames::FramesModule);
