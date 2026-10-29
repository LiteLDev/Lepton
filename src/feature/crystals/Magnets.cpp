// U12 crystals / Magnets (Quark oddities): powered magnets push (front) and pull (back) magnetic
// blocks and entities. Port of MagnetBlockEntity / MagnetSystem / MagnetTracker /
// MagnetizedBlockBlockEntity / DefaultMoveActions; see docs/spec/crystals.md section 3.

#include "feature/crystals/Magnets.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DataLoadHelper.h"
#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/level/block/components/BlockMovableComponent.h"
#include "mc/world/phys/AABB.h"

#include "feature/crystals/Common.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::crystals {

namespace bs = game::blockstates;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;

namespace {

bool gInstalled = false;
bool magnetsOn() { return gInstalled && cfg().enableMagnets; }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float randF() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

// ---- magnetism (ZRecipeCrawl digest of Quark's lists) -----------------------------------------------

struct Magnetism {
    std::set<std::string> derived; ///< crafted (recursively) from the derivation list
    bool                  built = false;
    std::unordered_map<BlockType const*, bool> blockCache;
};
Magnetism& mag() {
    static Magnetism m;
    return m;
}

std::string_view path(std::string_view id) {
    auto p = id.find(':');
    return p == std::string_view::npos ? id : id.substr(p + 1);
}
std::string normalize(std::string id) {
    if (id.starts_with("quark:")) id = "lepton:" + id.substr(6);
    if (!id.starts_with("#") && id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

/// A config entry matches an item / block id ("ns:id", or "#ns:tag" for the Java tags Quark lists).
bool entryMatches(std::string const& entryIn, std::string_view id) {
    auto entry = normalize(entryIn);
    if (!entry.starts_with("#")) return entry == id;
    auto tag = path(std::string_view(entry).substr(1));
    auto p   = path(id);
    if (tag == "cauldrons") return p.find("cauldron") != std::string_view::npos;
    if (tag == "hanging_signs" || tag == "all_hanging_signs" || tag == "ceiling_hanging_signs"
        || tag == "wall_hanging_signs")
        return p.ends_with("hanging_sign");
    if (tag == "anvil") return p.find("anvil") != std::string_view::npos;
    if (tag == "rails") return p == "rail" || p.ends_with("_rail");
    // generic Java tags: "<x>s" matches "*<x>" ("iron_bars" style names are listed explicitly)
    std::string_view one = tag.ends_with("s") ? tag.substr(0, tag.size() - 1) : tag;
    return p == one || p.ends_with("_" + std::string(one));
}
bool listMatches(std::vector<std::string> const& list, std::string_view id) {
    return std::any_of(list.begin(), list.end(), [&](std::string const& e) { return entryMatches(e, id); });
}

void buildMagnetism() {
    auto& m = mag();
    m.derived.clear();
    m.blockCache.clear();
    m.built     = true;
    auto level  = ll::service::getLevel();
    auto const& c = cfg();
    if (!level) return;
    // edges ingredient -> results of every crafting / stonecutting / smithing recipe
    std::map<std::string, std::set<std::string>> edges;
    for (auto const& [tag, byId] : level->getRecipes().mRecipes.get()) {
        for (auto const& [id, recipe] : byId) {
            if (!recipe) continue;
            std::vector<std::string> outs;
            for (auto const& r : recipe->getResultItems())
                if (!r.isNull()) outs.push_back(r.getTypeName());
            if (outs.empty()) continue;
            for (auto const& ing : recipe->mMyIngredients.get()) {
                ing.forEachItemUntil([&](Item const& item, short) {
                    auto const& name = item.mFullName->getString();
                    // Zeta skips ingredients that leave a remainder (buckets)
                    if (name.ends_with("_bucket") && name != "minecraft:bucket") return false;
                    for (auto const& o : outs) edges[name].insert(o);
                    return false;
                });
            }
        }
    }
    std::vector<std::string> queue;
    for (auto const& e : c.magneticDerivationList) {
        auto id = normalize(e);
        if (id.starts_with("#")) {
            for (auto const& [name, _] : edges)
                if (entryMatches(id, name)) queue.push_back(name);
        } else queue.push_back(id);
    }
    while (!queue.empty()) {
        auto id = std::move(queue.back());
        queue.pop_back();
        if (listMatches(c.magneticBlacklist, id) || !m.derived.insert(id).second) continue;
        if (auto it = edges.find(id); it != edges.end())
            for (auto const& o : it->second)
                if (!m.derived.contains(o)) queue.push_back(o);
    }
    log().info("crystals: {} magnetic items", m.derived.size());
}

/// The block id whose item a block drops / is picked as ("minecraft:iron_door", ...).
std::string blockItemId(Block const& b) {
    auto const& name = b.getTypeName();
    // a few Bedrock blocks whose item has another name
    static std::map<std::string, std::string, std::less<>> const kItem{
        {"minecraft:lit_blast_furnace", "minecraft:blast_furnace"},
        {"minecraft:powered_comparator", "minecraft:comparator"},
        {"minecraft:unpowered_comparator", "minecraft:comparator"},
        {"minecraft:powered_repeater", "minecraft:repeater"},
        {"minecraft:unpowered_repeater", "minecraft:repeater"},
        {"minecraft:lava_cauldron", "minecraft:cauldron"},
    };
    if (auto it = kItem.find(name); it != kItem.end()) return it->second;
    return name;
}

} // namespace

bool isItemMagnetic(std::string_view id) {
    if (id.empty() || id == "minecraft:air") return false;
    auto const& c = cfg();
    if (!mag().built) buildMagnetism();
    if (listMatches(c.magneticBlacklist, id)) return false;
    return mag().derived.contains(std::string(id)) || listMatches(c.magneticWhitelist, id);
}

bool isBlockMagnetic(Block const& b) {
    if (b.isAir() || isMagnet(b)) return false;
    auto& cache = mag().blockCache;
    if (!mag().built) buildMagnetism();
    auto const* type = &b.getBlockType();
    if (auto it = cache.find(type); it != cache.end()) return it->second;
    auto const& name = b.getTypeName();
    bool         v   = name == "minecraft:hopper" || name == "minecraft:stonecutter_block" || isItemMagnetic(blockItemId(b));
    cache.emplace(type, v);
    return v;
}

void rebuildMagnetism() { buildMagnetism(); }

namespace {

// ---- movability (PistonBaseBlock#isPushable with Bedrock piston rules) ----------------------------

MovementType movementOf(Block const& b) {
    if (auto const* m = b.tryGetComponent<BlockMovableComponent>()) return m->mMovementType;
    return MovementType::PushPull;
}

bool isPistonPushable(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (b.isAir()) return true;
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    auto mt = movementOf(b);
    // Java PUSH_ONLY blocks can't be moved by magnets (movement and piston facing never agree)
    return mt == MovementType::PushPull;
}
bool breaksOnPush(Block const& b) { return !b.isAir() && movementOf(b) == MovementType::Popped; }

bool collisionEmpty(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (b.isAir()) return true;
    auto box = b.getBlockType().getCollisionShape(b, region, pos, nullptr);
    return box.min == box.max || box.max.x <= box.min.x || box.max.y <= box.min.y || box.max.z <= box.min.z;
}

/// The direction (Java FACING, FacingID) the magnet pushes towards.
int magnetFacing(Block const& b) {
    auto f = faceFromName(bs::getString(b, "minecraft:facing_direction").value_or("down"));
    return f < 0 ? 0 : oppositeFace(f);
}

enum class Move { Skip, Move, Break, Prevent };

/// MagnetSystem#getPushAction.
Move pushAction(BlockSource& region, BlockPos const& pos, Block const& b, int moveDir) {
    if (isMagnetized(b)) return Move::Skip;
    // canBlockBeMagneticallyMoved
    if (cfg().usePistonLogic && !isPistonPushable(region, pos, b)) return Move::Skip;
    if (b.isAir() || isMagnet(b)) return Move::Skip;
    auto const& name = b.getTypeName();
    if ((name == "minecraft:piston" || name == "minecraft:sticky_piston")) {
        // an extended piston (its arm in front) can't be moved
        auto f = bs::getInt(b, "facing_direction");
        if (f) {
            auto const& arm = region.getBlock(offset(pos, *f)).getTypeName();
            if (arm == "minecraft:piston_arm_collision" || arm == "minecraft:sticky_piston_arm_collision") return Move::Skip;
        }
    }
    if (!isBlockMagnetic(b)) return Move::Skip;
    auto        front = offset(pos, moveDir);
    auto const& fb    = region.getBlock(front);
    // collateral movers (Zeta ICollateralMover, e.g. U10's iron rod)
    if (hasTag(b, "lepton:collateral_mover")) {
        if (cfg().usePistonLogic && !isPistonPushable(region, front, fb)) return Move::Prevent;
        if (fb.isAir()) return Move::Move;
        return breaksOnPush(fb) || hasTag(b, "lepton:collateral_breaker") ? Move::Break : Move::Skip;
    }
    if (fb.isAir()) return Move::Move;
    if (breaksOnPush(fb)) return Move::Break;
    return Move::Skip;
}

// ---- forces (MagnetTracker) -----------------------------------------------------------------------

struct Force {
    int      magnitude;
    bool     pushing;
    int      dir;
    int      distance;
    BlockPos origin;
};
std::map<core::GlobalPos, std::vector<Force>>& forces() {
    static std::map<core::GlobalPos, std::vector<Force>> f;
    return f;
}

// ---- moving blocks (MagnetizedBlockBlockEntity) ---------------------------------------------------

struct Moving {
    core::GlobalPos       pos;
    std::string           blockId;
    bs::Changes           states;
    int                   facing;
    int                   ticksLeft;
};
std::vector<Moving>& moving() {
    static std::vector<Moving> m;
    return m;
}
constexpr char const* kMovingKey = "lp:crystals:moving";

nlohmann::json statesToJson(bs::Changes const& states) {
    auto j = nlohmann::json::array();
    for (auto const& [k, v] : states) {
        nlohmann::json e;
        e["k"] = k;
        if (auto const* i = std::get_if<int>(&v)) e["i"] = *i;
        else if (auto const* b = std::get_if<bool>(&v)) e["b"] = *b;
        else e["s"] = std::get<std::string>(v);
        j.push_back(e);
    }
    return j;
}
bs::Changes statesFromJson(nlohmann::json const& j) {
    bs::Changes out;
    if (!j.is_array()) return out;
    for (auto const& e : j) {
        auto k = e.value("k", std::string{});
        if (e.contains("i")) out.emplace_back(k, e["i"].get<int>());
        else if (e.contains("b")) out.emplace_back(k, e["b"].get<bool>());
        else out.emplace_back(k, e.value("s", std::string{}));
    }
    return out;
}

void saveMoving() {
    if (!game::db::available()) return;
    auto arr = nlohmann::json::array();
    for (auto const& m : moving())
        arr.push_back({{"p", {m.pos.dim, m.pos.x, m.pos.y, m.pos.z}},
                       {"id", m.blockId},
                       {"st", statesToJson(m.states)},
                       {"f", m.facing},
                       {"t", m.ticksLeft}});
    if (arr.empty()) game::db::erase(kMovingKey);
    else game::db::save(kMovingKey, arr);
}
void loadMoving() {
    moving().clear();
    auto j = game::db::load(kMovingKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j) {
        if (!e.is_object() || !e.contains("p")) continue;
        auto const& p = e["p"];
        moving().push_back({{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()},
                            e.value("id", std::string{}),
                            statesFromJson(e.value("st", nlohmann::json::array())),
                            e.value("f", 0),
                            e.value("t", 1)});
    }
}

/// DefaultDataLoadHelper has no exported constructor; it has no members besides its vtable.
DataLoadHelper& loadHelper() {
    struct Raw {
        void** vt;
    };
    static Raw raw{DefaultDataLoadHelper::$vftable()};
    return *reinterpret_cast<DataLoadHelper*>(&raw);
}

/// A block actor's data while its block moves, plus the container items (dropped if the data
/// can't be put back, so nothing is ever lost).
struct SavedActor {
    std::unique_ptr<CompoundTag> tag;
    std::vector<ItemStack>       items;
};

SavedActor saveBlockActor(BlockSource& region, BlockPos const& pos) {
    SavedActor out;
    auto*      be = region.getBlockEntity(pos);
    if (!be) return out;
    out.tag  = std::make_unique<CompoundTag>();
    auto ctx = SaveContextFactory::createCloneSaveContext();
    if (!be->save(*out.tag, *ctx)) out.tag.reset();
    // empty the original container so replacing the block never drops its items
    if (auto* main = be->_getMainComponent())
        if (auto* container = main->getContainer())
            for (int i = 0; i < container->getContainerSize(); ++i) {
                out.items.push_back(container->getItem(i));
                container->setItem(i, ItemStack{});
            }
    return out;
}

BlockActor* restoreBlockActor(BlockSource& region, BlockPos const& pos, SavedActor const& saved) {
    auto* be = region.getBlockEntity(pos);
    bool  ok = saved.tag == nullptr && saved.items.empty();
    if (be && saved.tag) {
        auto tag = saved.tag->clone();
        tag->putInt("x", pos.x);
        tag->putInt("y", pos.y);
        tag->putInt("z", pos.z);
        be->load(region.getLevel(), *tag, loadHelper());
        be->refresh(region);
        ok = true;
    }
    if (!ok && !saved.items.empty()) {
        Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
        for (auto const& item : saved.items)
            if (!game::isEmpty(item)) region.getLevel().getSpawner().spawnItem(region, item, nullptr, at, 10);
        log().warn("crystals: a moved block lost its block data at {} {} {}; its items were dropped", pos.x, pos.y, pos.z);
    }
    return be;
}

// ---- move actions (DefaultMoveActions) ------------------------------------------------------------

std::optional<std::string> plantFor(ItemStack const& item) {
    static std::map<std::string, std::string, std::less<>> const kSeeds{
        {"minecraft:wheat_seeds", "minecraft:wheat"},          {"minecraft:beetroot_seeds", "minecraft:beetroot"},
        {"minecraft:pumpkin_seeds", "minecraft:pumpkin_stem"}, {"minecraft:melon_seeds", "minecraft:melon_stem"},
        {"minecraft:carrot", "minecraft:carrots"},             {"minecraft:potato", "minecraft:potatoes"},
        {"minecraft:torchflower_seeds", "minecraft:torchflower_crop"},
        {"minecraft:pitcher_pod", "minecraft:pitcher_crop"},   {"minecraft:nether_wart", "minecraft:nether_wart"},
        {"minecraft:sweet_berries", "minecraft:sweet_berry_bush"},
    };
    auto id = item.getTypeName();
    if (auto it = kSeeds.find(id); it != kSeeds.end()) return it->second;
    // block items of Java BushBlocks: saplings, flowers, grass, ferns, mushrooms, fungi, roots, bushes
    auto p = path(id);
    static constexpr std::string_view kBush[] = {
        "_sapling", "_mushroom", "_fungus", "_roots", "_tulip", "dandelion", "poppy", "blue_orchid", "allium",
        "azure_bluet", "oxeye_daisy", "cornflower", "lily_of_the_valley", "wither_rose", "torchflower", "sunflower",
        "lilac", "rose_bush", "peony", "short_grass", "tall_grass", "fern", "large_fern", "deadbush", "dead_bush",
        "pink_petals", "wildflowers", "bush", "firefly_bush", "short_dry_grass", "tall_dry_grass", "open_eyeblossom",
        "closed_eyeblossom", "cactus_flower", "lily_pad", "waterlily", "propagule", "leaf_litter"};
    for (auto s : kBush)
        if (p == s || (s.starts_with("_") && p.ends_with(s))) return std::string(id);
    return std::nullopt;
}

void hopperMoved(BlockSource& region, BlockPos const& pos, int moveDir, Block const& state, BlockActor* be) {
    if (!be) return;
    auto* main = be->_getMainComponent();
    auto* container = main ? main->getContainer() : nullptr;
    if (!container) return;
    int  dir    = bs::getInt(state, "facing_direction").value_or(0);
    auto offPos = offset(pos, dir);
    if (offPos == offset(pos, moveDir)) return;
    if (!region.getBlock(offPos).isAir()) return;
    for (int i = 0; i < container->getContainerSize(); ++i) {
        ItemStack stack = container->getItem(i);
        if (game::isEmpty(stack)) continue;
        ItemStack drop = stack;
        drop.set(1);
        stack.remove(1);
        container->setItem(i, stack);

        bool shouldDrop = true;
        if (auto plant = plantFor(drop)) {
            auto groundPos = offset(offPos, 0);
            if (region.getBlock(groundPos).isAir()) groundPos = offset(groundPos, 0);
            auto seedPos = offset(groundPos, 1);
            if (auto const* seed = bs::of(*plant); seed && region.getBlock(seedPos).isAir()
                                                     && seed->getBlockType().mayPlace(region, seedPos)) {
                bool canPlace = true;
                auto upper    = bs::with(*seed, {{"upper_block_bit", true}});
                if (upper) {
                    canPlace = false;
                    auto above = offset(seedPos, 1);
                    if (region.getBlock(above).isAir()) {
                        setBlock(region, above, *upper);
                        canPlace = true;
                    }
                }
                if (canPlace) {
                    setBlock(region, seedPos, *seed);
                    playSound(region, seedPos, static_cast<int>(SoundEvent::Place), *plant);
                    shouldDrop = false;
                }
            }
        }
        if (shouldDrop) {
            auto s = facingStep(dir);
            Vec3 at{pos.x + 0.5f + s[0] * 0.7f, pos.y + 0.15f + s[1] * 0.4f, pos.z + 0.5f + s[2] * 0.7f};
            if (auto* actor = region.getLevel().getSpawner().spawnItem(region, drop, nullptr, at, 0)) {
                const_cast<Vec3&>(actor->getPosDelta()) = Vec3{0, 0, 0};
            }
        }
        return;
    }
}

void stonecutterMoved(BlockSource& region, BlockPos const& pos) {
    auto        up    = offset(pos, 1);
    auto const& above = region.getBlock(up);
    if (above.isAir()) return;
    double hardness = above.mDirectData->mDestroySpeed;
    if (!(hardness > -1 && hardness < cfg().stoneCutterMaxHardness)) return;
    if (!cfg().stoneCutterSilkTouch) {
        destroyBlock(region, up, true);
        return;
    }
    // silk touch: the block itself (with its data), then remove it with the break effect
    ItemInstance self = above.asItemInstance(region, up, true);
    destroyBlock(region, up, false);
    if (self.isNull()) return;
    auto saveCtx = SaveContextFactory::createCloneSaveContext();
    auto selfTag = self.save(*saveCtx);
    if (!selfTag) return;
    ItemStack drop = ItemStack::fromTag(*selfTag);
    Vec3      at{up.x + 0.5f, up.y + 0.5f, up.z + 0.5f};
    region.getLevel().getSpawner().spawnItem(region, drop, nullptr, at, 10);
}

/// MagnetizedBlockBlockEntity finishing: the block leaves its old place and lands on the
/// placeholder (with its block actor data), then the move actions run.
void finalizeMove(BlockSource& region, Moving const& m) {
    BlockPos dest{m.pos.x, m.pos.y, m.pos.z};
    BlockPos src = offset(dest, oppositeFace(m.facing));
    if (!isMagnetized(region.getBlock(dest))) return;
    auto const& cur = region.getBlock(src);
    if (cur.getTypeName() != m.blockId) {
        game::clearBlock(region, dest); // the block was taken away meanwhile
        return;
    }
    auto saved = saveBlockActor(region, src);
    game::clearBlock(region, src);
    setBlock(region, dest, cur);
    playSound(region, dest, static_cast<int>(SoundEvent::Place), m.blockId);
    auto* be = restoreBlockActor(region, dest, saved);
    auto const& placed = region.getBlock(dest);
    if (m.blockId == "minecraft:hopper") hopperMoved(region, dest, m.facing, placed, be);
    else if (m.blockId == "minecraft:stonecutter_block") stonecutterMoved(region, dest);
}

/// MagnetBlock#triggerEvent: starts moving the block `distance` blocks from the magnet one step.
/// Bedrock can't draw Quark's sliding block, so the block stays visible where it is while the
/// (invisible) placeholder reserves its destination, and jumps there when the move completes.
bool moveBlock(BlockSource& region, BlockPos const& magnetPos, bool push, int distance) {
    auto const& magnet = region.getBlock(magnetPos);
    if (!isMagnet(magnet)) return false;
    int  moveDir = magnetFacing(magnet);
    int  dir     = push ? moveDir : oppositeFace(moveDir);
    auto target  = offset(magnetPos, dir, distance);
    auto const& targetState = region.getBlock(target);
    auto endPos  = offset(target, moveDir);
    auto action  = pushAction(region, target, targetState, moveDir);
    if (action != Move::Move && action != Move::Break) return false;
    auto const* placeholder = bs::of("lepton:magnetized_block");
    if (!placeholder) return false;
    if (action == Move::Break) destroyBlock(region, endPos, true);
    setBlock(region, endPos, *placeholder);
    // Java: placed at the block event, finished on the placeholder's third tick
    moving().push_back({game::globalPos(region, endPos), targetState.getTypeName(), bs::all(targetState), moveDir, 3});
    return true;
}
// ---- entities --------------------------------------------------------------------------------------

bool canPullEntity(Actor& e) {
    auto type = game::entities::typeOf(e);
    if (type == "lepton:gravisand") return true;
    if (type == "minecraft:item") return isItemMagnetic(static_cast<ItemActor&>(e).item().getTypeName());
    // Quark's #quark:affected_by_magnets
    static std::set<std::string, std::less<>> const kTypes{
        "minecraft:iron_golem",     "minecraft:minecart",      "minecraft:chest_minecart",
        "minecraft:hopper_minecart", "minecraft:tnt_minecart", "minecraft:command_block_minecart"};
    if (kTypes.contains(type)) return true;
    if (cfg().affectsArmor && (e.isPlayer() || e.hasCategory(ActorCategory::Mob))) {
        using SharedTypes::Legacy::ArmorSlot;
        for (auto slot : {ArmorSlot::Head, ArmorSlot::Torso, ArmorSlot::Legs, ArmorSlot::Feet, ArmorSlot::Body}) {
            auto const& armor = e.getArmor(slot);
            if (!game::isEmpty(armor) && isItemMagnetic(armor.getTypeName())) return true;
        }
    }
    return false;
}

void pushEntity(Actor& e, BlockPos const& magnetPos, int dir, double magnitude) {
    if (game::entities::typeOf(e) == "lepton:gravisand") magnitude = -magnitude;
    auto const& p  = e.getPosition();
    double      dx = p.x - (magnetPos.x + 0.5), dy = p.y - (magnetPos.y + 0.5), dz = p.z - (magnetPos.z + 0.5);
    if (e.isPlayer()) dy -= 1.62; // Java measures from the feet; Bedrock player positions are at the eyes
    double distSq = dx * dx + dy * dy + dz * dz;
    if (distSq < 1e-6) distSq = 1e-6;
    double k = magnitude * cfg().entitiesPullForce / distSq;
    auto   s = facingStep(dir);
    auto&  v = const_cast<Vec3&>(e.getPosDelta());
    v.x += static_cast<float>(s[0] * k);
    v.y += static_cast<float>(s[1] * k);
    v.z += static_cast<float>(s[2] * k);
    e.setFallDistance(0);
    if (e.isPlayer()) {
        auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
        auto& motion = static_cast<SetActorMotionPacket&>(*pkt);
        motion.mRuntimeId = e.getRuntimeID();
        motion.mMotion    = v;
        motion.sendTo(static_cast<Player&>(e));
    }
}

// ---- the magnet tick (MagnetBlockEntity#tick / #magnetize) -----------------------------------------

void magnetize(BlockSource& region, BlockPos const& pos, Block const& state, int dir, int moveDir, int power) {
    double magnitude = dir == moveDir ? 1 : -1;
    int    blockDist = 1;
    for (; blockDist <= power; ++blockDist) {
        auto        targetPos   = offset(pos, dir, blockDist);
        auto const* targetState = &region.getBlock(targetPos);
        auto        action      = pushAction(region, targetPos, *targetState, moveDir);
        if (action == Move::Move || action == Move::Break) {
            forces()[game::globalPos(region, targetPos)].push_back(
                {power - blockDist + 1, dir == moveDir, moveDir, blockDist, pos});
        } else if (action == Move::Prevent) break;
        if (isMagnetized(*targetState)) {
            // the moving block's own state decides whether the flux passes
            auto gp = game::globalPos(region, targetPos);
            for (auto const& m : moving())
                if (m.pos == gp)
                    if (auto const* b = bs::of(m.blockId, m.states); b || (b = bs::of(m.blockId))) targetState = b;
        }
        if (!collisionEmpty(region, targetPos, *targetState)) break;
    }

    if (cfg().affectEntities && blockDist > 1) {
        auto s = facingStep(dir);
        AABB box{Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
                 Vec3{static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1), static_cast<float>(pos.z + 1)}};
        (s[0] < 0 ? box.min.x : box.max.x) += static_cast<float>(s[0] * blockDist);
        (s[1] < 0 ? box.min.y : box.max.y) += static_cast<float>(s[1] * blockDist);
        (s[2] < 0 ? box.min.z : box.max.z) += static_cast<float>(s[2] * blockDist);
        for (auto* e : game::entities::inBox(region, box))
            if (e && canPullEntity(*e)) pushEntity(*e, pos, dir, magnitude);
    }

    // particles (client effect in Quark; sent from the server here)
    if (!bs::getBool(state, "lepton:waxed").value_or(false)) {
        double motion = 0.06 * magnitude;
        auto   s      = facingStep(dir);
        double off[3] = {s[0] * motion, s[1] * motion, s[2] * motion};
        auto   ms     = facingStep(moveDir);
        double sign   = (ms[0] + ms[1] + ms[2]) > 0 ? 1 : -1;
        auto   coord  = [&](double o) {
            return o == 0 ? 0.5 + (randF() + randF() - 1) / 2.0 : 0.5 + sign * (randF() - 1.25);
        };
        for (int j = 1; j < blockDist; ++j) {
            if (randF() > 0.2f) continue;
            auto t = offset(pos, dir, j);
            Vec3 at{static_cast<float>(t.x + coord(off[0])), static_cast<float>(t.y + coord(off[1])),
                    static_cast<float>(t.z + coord(off[2]))};
            spawnParticle(region, dir == moveDir ? "lepton:magnet_repulsor" : "lepton:magnet_attractor", at,
                          {{"dx", static_cast<float>(off[0])}, {"dy", static_cast<float>(off[1])}, {"dz", static_cast<float>(off[2])}});
        }
    }
}

/// Block events queued by MagnetTracker#actOnForces, run at the start of the next tick.
struct BlockEvent {
    int      dim;
    BlockPos origin;
    bool     pushing;
    int      distance;
};
std::vector<BlockEvent>& blockEvents() {
    static std::vector<BlockEvent> e;
    return e;
}

void magnetTick() {
    if (!magnetsOn()) {
        blockEvents().clear();
        return;
    }
    // block events of the last tick's forces
    if (!blockEvents().empty()) {
        auto events = std::move(blockEvents());
        blockEvents().clear();
        for (auto const& e : events)
            if (auto* region = game::regionOf(e.dim)) moveBlock(*region, e.origin, e.pushing, e.distance);
    }
    // finish the blocks that are moving
    if (!moving().empty()) {
        std::vector<Moving> done;
        for (auto it = moving().begin(); it != moving().end();) {
            if (--it->ticksLeft <= 0) {
                done.push_back(std::move(*it));
                it = moving().erase(it);
            } else ++it;
        }
        for (auto const& m : done)
            if (auto* region = game::regionOf(m.pos.dim)) finalizeMove(*region, m);
    }

    std::vector<core::GlobalPos> list(tracked(Kind::Magnet).begin(), tracked(Kind::Magnet).end());
    forces().clear();
    for (auto const& gp : list) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos bp{gp.x, gp.y, gp.z};
        if (!region->hasChunksAt(bp, 0, false)) continue;
        auto const* b = &region->getBlock(bp);
        if (!isMagnet(*b)) {
            untrack(Kind::Magnet, gp);
            continue;
        }
        int  power   = redstonePower(*region, bp);
        bool powered = power > 0;
        if (bs::getBool(*b, "lepton:powered").value_or(false) != powered) {
            if (auto const* n = bs::with(*b, {{"lepton:powered", powered}})) {
                setBlock(*region, bp, *n);
                b = &region->getBlock(bp);
            }
        }
        if (!powered) continue;
        int facing = magnetFacing(*b);
        magnetize(*region, bp, *b, facing, facing, power);
        magnetize(*region, bp, *b, oppositeFace(facing), facing, power);
    }

    // MagnetTracker#actOnForces
    auto all = std::move(forces());
    forces().clear();
    for (auto const& [gp, list2] : all) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        int net[3] = {0, 0, 0};
        for (auto const& f : list2) {
            auto s = facingStep(f.dir);
            for (int i = 0; i < 3; ++i) net[i] += s[i] * f.magnitude;
        }
        if (net[0] == 0 && net[1] == 0 && net[2] == 0) continue;
        // Direction.getNearest: the first direction (DOWN, UP, NORTH, SOUTH, WEST, EAST) with the
        // largest dot product
        int   target = 0;
        float best   = -1e30f;
        for (int d = 0; d < 6; ++d) {
            auto  s   = facingStep(d);
            float dot = static_cast<float>(s[0] * net[0] + s[1] * net[1] + s[2] * net[2]);
            if (dot > best) {
                best   = dot;
                target = d;
            }
        }
        for (auto const& f : list2)
            if (f.dir == target) blockEvents().push_back({gp.dim, f.origin, f.pushing, f.distance});
    }
}

class MagnetsModule {
public:
    void install() {
        gInstalled = true;
        static bool once = false;
        if (!once) {
            once = true;
            tickListeners().push_back([] { magnetTick(); });
            game::db::onWorldReady([] {
                mag() = Magnetism{};
                loadMoving();
            });
            game::db::onWorldSave([] { saveMoving(); });
            game::db::onWorldClosed([] {
                mag() = Magnetism{};
                moving().clear();
                forces().clear();
                blockEvents().clear();
            });
            config::onReload([] { mag() = Magnetism{}; });
        }
        if (game::db::available()) loadMoving();
    }
    void uninstall() {
        gInstalled = false;
        saveMoving();
        forces().clear();
    }
};

} // namespace

int movingBlockCount() { return static_cast<int>(moving().size()); }
int movementTypeOf(Block const& b) { return b.tryGetComponent<BlockMovableComponent>() ? static_cast<int>(movementOf(b)) : -1; }
int magnetPushAction(BlockSource& region, BlockPos const& pos, int moveDir) {
    return static_cast<int>(pushAction(region, pos, region.getBlock(pos), moveDir));
}
bool magnetMoveBlock(BlockSource& region, BlockPos const& magnetPos, bool push, int distance) {
    return moveBlock(region, magnetPos, push, distance);
}

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::MagnetsModule);
