#include "feature/palette/Palette.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#ifdef LP_CLIENT
#include "ll/api/event/world/ClientLevelTickEvent.h"
#endif

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/vanilla_components/IConstBlockSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/GetCollisionShapeActorProxy.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/level/block/block_events/BlockEntityFallOnEvent.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructurePiece.h"
#include "mc/world/level/levelgen/structure/StructurePieceType.h"
#include "mc/world/phys/AABB.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("palette", lp::feature::palette::PaletteConfig);

namespace lp::feature::palette {

namespace bs     = game::blockstates;
namespace shapes = game::shapes;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;

namespace {

struct IdEntry {
    std::string_view id;
    Part             part;
};
constexpr IdEntry kIds[] = {
#include "feature/palette/PaletteIds.inc"
};

PaletteConfig const& cfg() { return config::get<PaletteConfig>(); }

std::unordered_map<std::string_view, Part> const& idTable() {
    static auto const table = [] {
        std::unordered_map<std::string_view, Part> t;
        for (auto const& e : kIds) t.emplace(e.id, e.part);
        return t;
    }();
    return table;
}

bool               hasTag(Block const& b, char const* tag) { return b.hasTag(HashedString(tag)); }
std::string const& nameOf(Block const& b) { return b.getTypeName(); }

// Block types the hooks compare against (set on the server/client thread, read by any thread).
std::atomic<BlockType const*> gGrateType{nullptr};
std::atomic<Block const*>     gGoldBars{nullptr};

// Block types that need a support check when a neighbour changes (ladders, the bamboo carpet).
std::vector<BlockType const*> gSupportTypes;

void cacheBlocks() {
    auto const* g = bs::of("lepton:grate");
    gGrateType    = g ? &g->getBlockType() : nullptr;
    gGoldBars     = bs::of("lepton:gold_bars");
    gSupportTypes.clear();
    for (auto const& e : kIds) {
        auto const* b = bs::of(e.id);
        if (b && (b->hasTag(HashedString("lepton:ladder")) || e.id == "lepton:bamboo_mat_carpet"))
            gSupportTypes.push_back(&b->getBlockType());
    }
}

void clearCachedBlocks() {
    gGrateType = nullptr;
    gGoldBars  = nullptr;
    gSupportTypes.clear();
}

bool needsSupport(Block const& b) {
    auto const* t = &b.getBlockType();
    return std::find(gSupportTypes.begin(), gSupportTypes.end(), t) != gSupportTypes.end();
}

// ---- small scheduler --------------------------------------------------------------------------

struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
struct State {
    uint64_t                              tick = 0;
    std::vector<Task>                     tasks;
    std::unordered_map<int64_t, uint64_t> lastCompost;
};
State& st() {
    static State s;
    return s;
}
// ladder / carpet support checks for the next tick (filled by any thread that changes blocks)
std::mutex                gChecksMutex;
std::set<core::GlobalPos> gChecks;

void later(int ticks, std::function<void()> fn) {
    st().tasks.push_back({st().tick + static_cast<uint64_t>(ticks), std::move(fn)});
}

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

// ---- fortress gold bars queue (filled by world generation threads) ----------------------------

std::mutex                   gBarsMutex;
std::vector<core::GlobalPos> gBars;
constexpr size_t             kMaxBars = 1 << 16;
constexpr char const*        kBarsKey = "lp:palette:bars";

void queueBars(core::GlobalPos const& p) {
    std::lock_guard lock(gBarsMutex);
    if (gBars.size() < kMaxBars) {
        gBars.push_back(p);
        return;
    }
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) log().warn("palette: too many fortress gold bars waiting for their chunks; dropping");
}

void processBars() {
    std::vector<core::GlobalPos> work;
    {
        std::lock_guard lock(gBarsMutex);
        if (gBars.empty()) return;
        work.swap(gBars);
    }
    std::vector<core::GlobalPos> keep;
    for (auto const& gp : work) {
        auto* region = game::regionOf(gp.dim);
        auto  pos    = game::blockPos(gp);
        if (!region || !region->hasChunksAt(pos, 1, false)) {
            keep.push_back(gp);
            continue;
        }
        if (nameOf(region->getBlock(pos)) == "lepton:gold_bars") shapes::queueUpdate(*region, pos); // + neighbours
    }
    if (!keep.empty()) {
        std::lock_guard lock(gBarsMutex);
        gBars.insert(gBars.end(), keep.begin(), keep.end());
    }
}

bool isFortressPiece(int type) {
    switch (static_cast<StructurePieceType>(type)) {
    case StructurePieceType::NetherFortressCrossing:
    case StructurePieceType::NetherFortressStartPiece:
    case StructurePieceType::NetherFortressStraight:
    case StructurePieceType::NetherFortressEndFiller:
    case StructurePieceType::NetherFortressRoomCrossing:
    case StructurePieceType::NetherFortressStairsRoom:
    case StructurePieceType::NetherFortressMonsterThrone:
    case StructurePieceType::NetherCastleEntrance:
    case StructurePieceType::NetherCastleStalkRoom:
    case StructurePieceType::NetherCastleSmallCorridor:
    case StructurePieceType::NetherCastleSmallCorridorCrossing:
    case StructurePieceType::NetherCastleSmallCorridorRightTurn:
    case StructurePieceType::NetherCastleSmallCorridorLeftTurn:
    case StructurePieceType::NetherCastleCorridorStairs:
    case StructurePieceType::NetherCastleCorridorTBalcony:
        return true;
    default:
        return false;
    }
}

// ---- grate collision --------------------------------------------------------------------------

constexpr uint32_t kItemEntity = 64;   // ActorType::ItemEntity
constexpr uint32_t kXpOrb      = 69;   // ActorType::Experience
constexpr uint32_t kAnimal     = 4864; // ActorType::Animal (family bits)
constexpr uint32_t kWaterAnim  = 8960; // ActorType::WaterAnimal (family bits)

struct ProxyLayout { // GetCollisionShapeActorProxy: vtable + Actor const&
    void**       vft;
    Actor const* actor;
};

Actor const* actorOf(GetCollisionShapeInterface const& e) {
    auto const& p = reinterpret_cast<ProxyLayout const&>(e);
    return p.vft == GetCollisionShapeActorProxy::$vftable() ? p.actor : nullptr;
}

AABB plateBox(BlockPos const& pos, double top = 1.0) {
    return AABB{
        Vec3{static_cast<float>(pos.x),       static_cast<float>(pos.y) + 15.f / 16.f, static_cast<float>(pos.z)      },
        Vec3{static_cast<float>(pos.x) + 1.f, static_cast<float>(pos.y + top),         static_cast<float>(pos.z) + 1.f}
    };
}

bool isEmptyBox(AABB const& b) { return b.max.x <= b.min.x || b.max.y <= b.min.y || b.max.z <= b.min.z; }

AABB grateBox(
    IConstBlockSource const& region,
    BlockPos const&          pos,
    uint32_t                 type,
    bool                     leashed,
    Vec3 const&              feet,
    float                    step
) {
    if (type == kItemEntity || type == kXpOrb) return AABB::BOX_AT_ORIGIN_WITH_NO_VOLUME();
    bool animal = (type & kAnimal) == kAnimal || (type & kWaterAnim) == kWaterAnim;
    if (animal && !leashed) {
        BlockPos below = game::blockPosOf(feet);
        --below.y;
        auto const* grate   = gGrateType.load();
        bool        onGrate = grate && &region.getBlock(below).getBlockType() == grate;
        if (!onGrate) return plateBox(pos, 17.0 / 16.0 + step);
    }
    return plateBox(pos);
}

/// The actor whose AABB component is `box` (the same memory), looked up in the dimension's main
/// region. Only on the thread that owns that region (the server thread); nullptr elsewhere.
Actor const* actorWithBox(IConstBlockSource const& query, AABB const& box) {
    auto* region = game::regionOf(static_cast<int>(query.getDimensionId()));
    if (!region || *region->mOwnerThreadID != std::this_thread::get_id()) return nullptr;
    AABB search{
        box.min - Vec3{0.25f, 0.25f, 0.25f},
        box.max + Vec3{0.25f, 0.25f, 0.25f}
    };
    for (Actor* a : region->fetchEntities(nullptr, search, false, false)) {
        if (&a->getAABB() == &box) return a;
    }
    return nullptr;
}

AABB grateShape(
    IConstBlockSource const&                       region,
    BlockPos const&                                pos,
    optional_ref<GetCollisionShapeInterface const> entity
) {
    if (!entity) return plateBox(pos);
    if (auto const* a = actorOf(*entity)) {
        return grateBox(
            region,
            pos,
            static_cast<uint32_t>(a->getEntityTypeId()),
            a->isLeashed(),
            a->getFeetPos(),
            a->getMaxAutoStep()
        );
    }
    // The movement systems query through an ECS proxy that has no Actor: find the actor whose
    // AABB component the proxy reads (only for animals, which need the leash state and step height).
    auto const& box  = entity->getAABB();
    auto        type = static_cast<uint32_t>(entity->getEntityTypeId());
    if ((type & kAnimal) == kAnimal || (type & kWaterAnim) == kWaterAnim) {
        if (auto const* a = actorWithBox(region, box)) {
            return grateBox(region, pos, type, a->isLeashed(), a->getFeetPos(), a->getMaxAutoStep());
        }
    }
    Vec3 feet{(box.min.x + box.max.x) / 2.f, box.min.y, (box.min.z + box.max.z) / 2.f};
    return grateBox(region, pos, type, false, feet, 0.5625f);
}

bool isActiveGrate(Block const& block) {
    auto const* g = gGrateType.load();
    return g && &block.getBlockType() == g && cfg().enableGrate;
}

} // namespace

// ---- hooks ------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    PaletteRecipeHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::getRecipeByNetId,
    Recipe const*,
    RecipeNetId const& netId
) {
    auto const* r = origin(netId);
    if (r) {
        if (auto p = partOf(*r->mRecipeId); p && !enabled(*p)) return nullptr;
    }
    return r;
}

LL_TYPE_INSTANCE_HOOK(
    PaletteGrateShapeHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$getCollisionShape,
    AABB,
    Block const&                                   block,
    IConstBlockSource const&                       region,
    BlockPos const&                                pos,
    optional_ref<GetCollisionShapeInterface const> entity
) {
    if (!isActiveGrate(block)) return origin(block, region, pos, entity);
    return grateShape(region, pos, entity);
}

LL_TYPE_INSTANCE_HOOK(
    PaletteGrateShapesHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$addCollisionShapes,
    bool,
    Block const&                                   block,
    IConstBlockSource const&                       region,
    BlockPos const&                                pos,
    AABB const*                                    intersectTestBox,
    std::vector<AABB>&                             inoutBoxes,
    optional_ref<GetCollisionShapeInterface const> entity
) {
    if (!isActiveGrate(block)) return origin(block, region, pos, intersectTestBox, inoutBoxes, entity);
    auto box = grateShape(region, pos, entity);
    if (isEmptyBox(box)) return false;
    if (intersectTestBox && !box.intersects(*intersectTestBox)) return false;
    inoutBoxes.push_back(box);
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    PaletteThatchFallHook,
    ll::memory::HookPriority::Normal,
    BlockEvents::BlockEntityFallOnEvent,
    &BlockEvents::BlockEntityFallOnEvent::handleActorFallDamage,
    void,
    float             distance,
    float             multiplier,
    ActorDamageSource source
) {
    if (cfg().enableThatch && hasTag(this->getBlock(), "lepton:thatch")) {
        multiplier *= static_cast<float>(thatchMultiplier());
    }
    origin(distance, multiplier, std::move(source));
}

LL_TYPE_INSTANCE_HOOK(
    PaletteFortressHook,
    ll::memory::HookPriority::Normal,
    StructurePiece,
    &StructurePiece::$placeBlock,
    void,
    BlockSource&       region,
    Block const&       block,
    int                x,
    int                y,
    int                z,
    BoundingBox const& chunkBB
) {
    if (auto const* rep = testing::fortressReplacement(static_cast<int>(this->getType()), block)) {
        origin(region, *rep, x, y, z, chunkBB);
        BlockPos world{this->getWorldX(x, z), this->mBoundingBox->min.y + y, this->getWorldZ(x, z)};
        if (chunkBB.contains(world)) queueBars(game::globalPos(region, world));
        return;
    }
    origin(region, block, x, y, z, chunkBB);
}

// ---- public helpers ---------------------------------------------------------------------------

std::optional<Part> partOf(std::string_view id) {
    auto const& t  = idTable();
    auto        it = t.find(id);
    if (it == t.end()) return std::nullopt;
    return it->second;
}

bool enabled(Part part) {
    auto const& c = cfg();
    switch (part) {
    case Part::Shingles:
        return c.enableShingles;
    case Part::FramedGlass:
        return c.enableFramedGlass;
    case Part::GoldBars:
        return c.enableGoldBars;
    case Part::IronPlates:
        return c.enableIndustrialPalette && c.enableIronPlates;
    case Part::IronLadder:
        return c.enableIndustrialPalette && c.enableIronLadder;
    case Part::PaperBlocks:
        return c.enableJapanesePalette && c.enablePaperBlocks;
    case Part::BambooMats:
        return c.enableJapanesePalette && c.enableBambooMats;
    case Part::NetherBrickFenceGate:
        return c.enableNetherBrickFenceGate;
    case Part::Thatch:
        return c.enableThatch;
    case Part::Grate:
        return c.enableGrate;
    }
    return false;
}

double thatchMultiplier() { return std::clamp(cfg().fallDamageMultiplier, 0.0, 1.0); }

AABB grateCollision(BlockSource const& region, BlockPos const& pos, Actor const* actor) {
    if (!actor) return plateBox(pos);
    return grateBox(
        region,
        pos,
        static_cast<uint32_t>(actor->getEntityTypeId()),
        actor->isLeashed(),
        actor->getFeetPos(),
        actor->getMaxAutoStep()
    );
}

std::string bambooMatFacing(BlockSource& region, BlockPos const& pos, std::string_view id, float yaw, float pitch) {
    std::string dir(shapes::lookDirection(yaw));
    if (pitch > 70.f) return "down";
    auto opposite = std::string(shapes::opposite(dir));
    auto facingAt = [&](BlockPos const& p) -> std::optional<std::string> {
        auto const& b = region.getBlock(p);
        if (nameOf(b) != id) return std::nullopt;
        return bs::getString(b, "lepton:facing");
    };
    if (facingAt(shapes::neighbor(pos, opposite)) != opposite) {
        if (facingAt(shapes::neighbor(pos, dir)) == dir) dir = opposite;
    }
    return dir;
}

bool ladderSupported(BlockSource& region, BlockPos const& pos) {
    auto const& b    = region.getBlock(pos);
    auto        face = bs::getString(b, "minecraft:block_face");
    if (!face) return true;
    auto const& support = region.getBlock(shapes::neighbor(pos, shapes::opposite(*face)));
    if (support.isAir()) return false;
    if (static_cast<bool>(support.mCachedComponentData->mIsSolid)) return true;
    static constexpr std::string_view kFaces[6] = {"down", "up", "north", "south", "west", "east"};
    uchar                             facing    = 2;
    for (uchar i = 0; i < 6; ++i)
        if (kFaces[i] == *face) facing = i;
    return support.canProvideSupport(facing, BlockSupportType::Edge);
}

void applyClimbable() {
    for (auto const& e : kIds) {
        auto const* b = bs::of(e.id);
        if (!b || !hasTag(*b, "lepton:climbable")) continue;
        BlockProperty& props = b->mBlockType->mProperties;
        props                = static_cast<BlockProperty>(
            static_cast<uint64_t>(props) | static_cast<uint64_t>(BlockProperty::Climbable)
            | static_cast<uint64_t>(BlockProperty::CanHaltWhenClimbing)
        );
    }
}

namespace testing {
Block const* fortressReplacement(int pieceType, Block const& block) {
    if (!isFortressPiece(pieceType)) return nullptr; // cheapest test first: runs for every structure block
    if (!enabled(Part::GoldBars) || !cfg().generateInNetherFortress) return nullptr;
    if (nameOf(block) != "minecraft:nether_brick_fence") return nullptr;
    return gGoldBars.load();
}
size_t pendingBars() {
    std::lock_guard lock(gBarsMutex);
    return gBars.size();
}
} // namespace testing

// ---- events -----------------------------------------------------------------------------------

namespace {

/// Breaks the block at `pos` (drops it as an item) if it is `id`.
void popBlock(BlockSource& region, BlockPos const& pos) {
    auto const& b  = region.getBlock(pos);
    std::string id = nameOf(b);
    if (!game::clearBlock(region, pos)) return;
    if (auto level = ll::service::getLevel()) {
        auto stack = game::makeItem(id, 1);
        if (!game::isEmpty(stack)) {
            Vec3 at{
                static_cast<float>(pos.x) + 0.5f,
                static_cast<float>(pos.y) + 0.5f,
                static_cast<float>(pos.z) + 0.5f
            };
            level->getSpawner().spawnItem(region, stack, nullptr, at, 10);
        }
    }
    log().info("palette: {} at {} {} {} lost its support", id, pos.x, pos.y, pos.z);
}

void checkSupport(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (hasTag(b, "lepton:ladder")) {
        if (!ladderSupported(region, pos)) popBlock(region, pos);
    } else if (nameOf(b) == "lepton:bamboo_mat_carpet") {
        if (region.getBlock(shapes::neighbor(pos, "down")).isAir()) popBlock(region, pos);
    }
}

void queueCheck(BlockSource& region, BlockPos const& pos) {
    std::lock_guard lock(gChecksMutex);
    gChecks.insert(game::globalPos(region, pos));
}

void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = player.getSelectedItem();
    if (item.isNull()) return;
    if (auto p = partOf(item.getTypeName()); p && !enabled(*p)) ev.cancel();
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& placed = ev.placedBlock();
    auto const& name   = nameOf(placed);
    if (!partOf(name)) return;
    auto pos = ev.pos();
    int  dim = static_cast<int>(player.getDimensionBlockSource().getDimensionId());
    if (name == "lepton:bamboo_mat" || name == "lepton:bamboo_mat_carpet") {
        auto rot = player.getRotation(); // x = pitch, y = yaw
        later(1, [dim, pos, name, yaw = rot.y, pitch = rot.x] {
            auto* r = game::regionOf(dim);
            if (!r || nameOf(r->getBlock(pos)) != name) return;
            auto facing = bambooMatFacing(*r, pos, name, yaw, pitch);
            if (auto const* b = bs::with(
                    r->getBlock(pos),
                    {
                        {"lepton:facing", facing}
            }
                ))
                game::setBlock(*r, pos, *b);
            checkSupport(*r, pos);
        });
    } else if (hasTag(placed, "lepton:ladder")) {
        later(1, [dim, pos] {
            if (auto* r = game::regionOf(dim)) checkSupport(*r, pos);
        });
    }
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto& region = ev.blockSource();
    if (gSupportTypes.empty() || region.getLevel().isClientSide()) return;
    auto const& pos = ev.pos();
    for (auto d : {"north", "south", "west", "east", "up"}) {
        auto n = shapes::neighbor(pos, d);
        if (needsSupport(region.getBlock(n))) queueCheck(region, n);
    }
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !enabled(Part::Thatch)) return;
    auto const& item = ev.item();
    if (item.isNull() || item.getTypeName() != "lepton:thatch") return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.blockPos();
    auto const& b      = region.getBlock(pos);
    if (nameOf(b) != "minecraft:composter") return;
    int level = bs::getInt(b, "composter_fill_level").value_or(8);
    if (level >= 7) return; // full / ready: vanilla behaviour
    ev.cancel();
    auto  id   = player.getOrCreateUniqueID().rawID;
    auto& last = st().lastCompost[id];
    if (st().tick - last < 4 && last != 0) return; // one use per click
    last    = st().tick;
    int dim = static_cast<int>(region.getDimensionId());
    later(1, [dim, pos, id] {
        auto* r = game::regionOf(dim);
        auto* p = playerById(id);
        if (!r || !p) return;
        auto const& c = r->getBlock(pos);
        if (nameOf(c) != "minecraft:composter") return;
        int lvl = bs::getInt(c, "composter_fill_level").value_or(8);
        if (lvl >= 7) return;
        auto held = p->getSelectedItem();
        if (held.isNull() || held.getTypeName() != "lepton:thatch") return;
        if (!p->isCreative()) {
            held.remove(1);
            p->setSelectedItem(held);
            game::inv::sync(*p);
        }
        static std::mt19937 rng{std::random_device{}()};
        bool                raise = lvl == 0 || std::uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.65;
        auto                level = ll::service::getLevel();
        Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
        if (raise) {
            if (auto const* n = bs::with(
                    c,
                    {
                        {"composter_fill_level", lvl + 1}
            }
                )) {
                game::setBlock(*r, pos, *n);
                // like vanilla: level 7 becomes ready (8) on the composter's queued tick 20 ticks
                // later (the tick is saved with the chunk)
                if (lvl + 1 == 7) r->addToTickingQueue(pos, *n, 20);
            }
        }
        if (level)
            level->broadcastSoundEvent(
                *r,
                raise ? SoundEvent::ComposterFillLayer : SoundEvent::ComposterFill,
                at,
                r->getBlock(pos),
                ActorSoundIdentifier(*p),
                false,
                std::nullopt
            );
        log().info("palette: composted thatch at {} {} {} (level {})", pos.x, pos.y, pos.z, raise ? lvl + 1 : lvl);
    });
}

void onSpawning(ll::event::SpawningMobEvent& ev) {
    if (!enabled(Part::Grate) || !ev.naturalSpawn()) return;
    auto const* g = gGrateType.load();
    if (!g) return;
    auto&    region = ev.blockSource();
    auto     at     = game::blockPosOf(ev.pos());
    BlockPos below{at.x, at.y - 1, at.z};
    if (&region.getBlock(at).getBlockType() == g || &region.getBlock(below).getBlockType() == g) ev.cancel();
}

void onTick(ll::event::ServerLevelTickEvent&) {
    auto& s = st();
    ++s.tick;
    if (!s.tasks.empty()) {
        std::vector<Task> due;
        for (auto it = s.tasks.begin(); it != s.tasks.end();) {
            if (it->due <= s.tick) {
                due.push_back(std::move(*it));
                it = s.tasks.erase(it);
            } else ++it;
        }
        for (auto& t : due) {
            try {
                t.fn();
            } catch (std::exception const& e) {
                log().error("palette: task failed: {}", e.what());
            }
        }
    }
    std::set<core::GlobalPos> checks;
    {
        std::lock_guard lock(gChecksMutex);
        checks.swap(gChecks);
    }
    if (!checks.empty()) {
        for (auto const& gp : checks)
            if (auto* r = game::regionOf(gp.dim)) checkSupport(*r, game::blockPos(gp));
    }
    if (s.tick % 20 == 0) processBars();
}

void loadBars() {
    auto j = game::db::load(kBarsKey);
    if (!j || !j->is_array()) return;
    std::lock_guard lock(gBarsMutex);
    for (auto const& e : *j) {
        if (gBars.size() >= kMaxBars) break;
        if (!e.is_array() || e.size() != 4) continue;
        if (!std::all_of(e.begin(), e.end(), [](auto const& v) { return v.is_number_integer(); })) continue;
        gBars.push_back(core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
}

void saveBars() {
    if (!game::db::available()) return;
    nlohmann::json arr = nlohmann::json::array();
    {
        std::lock_guard lock(gBarsMutex);
        for (auto const& p : gBars) arr.push_back({p.dim, p.x, p.y, p.z});
    }
    if (arr.empty()) game::db::erase(kBarsKey);
    else game::db::save(kBarsKey, arr);
}

bool gReady     = false;
bool gInstalled = false;

void onWorldReady() {
    if (!gInstalled || gReady) return; // registered after the world was ready (BDS) and called directly
    gReady = true;
    cacheBlocks();
    applyClimbable();
    loadBars();
    log().info("palette: ready ({} block/recipe ids)", std::size(kIds));
}

void resetState() {
    st() = State{};
    {
        std::lock_guard lock(gChecksMutex);
        gChecks.clear();
    }
    std::lock_guard lock(gBarsMutex);
    gBars.clear();
}

class PaletteModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        mListeners.push_back(bus.emplaceListener<ll::event::SpawningMobEvent>(onSpawning));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
#ifdef LP_CLIENT
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](ll::event::ClientLevelTickEvent&) {
            // remote worlds register the behaviour pack blocks on the client: apply the
            // climbable property (and cache the grate) whenever the registry changed
            // (checked once a second: a new registry may reuse the old addresses, so look at the
            // property itself)
            static uint64_t ticks = 0;
            if (++ticks % 20 != 0) return;
            auto const* l = bs::of("lepton:iron_ladder");
            if (!l) return;
            auto props = static_cast<uint64_t>(static_cast<BlockProperty>(l->mBlockType->mProperties));
            if (!(props & static_cast<uint64_t>(BlockProperty::Climbable))) applyClimbable();
            // only the atomic grate type: the rest of the cache belongs to the server thread
            if (auto const* g = bs::of("lepton:grate")) gGrateType = &g->getBlockType();
        }));
#endif
        PaletteRecipeHook::hook();
        PaletteGrateShapeHook::hook();
        PaletteGrateShapesHook::hook();
        PaletteThatchFallHook::hook();
        PaletteFortressHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady(onWorldReady);
            game::db::onWorldSave([] {
                if (gInstalled) saveBars();
            });
            game::db::onWorldClosed([] {
                gReady = false;
                clearCachedBlocks();
                resetState();
            });
        }
        gInstalled = true;
        if (game::db::available()) onWorldReady(); // BDS: the world was ready before this module
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        PaletteRecipeHook::unhook();
        PaletteGrateShapeHook::unhook();
        PaletteGrateShapesHook::unhook();
        PaletteThatchFallHook::unhook();
        PaletteFortressHook::unhook();
        if (gReady && game::db::available()) saveBars();
        gInstalled = false;
        gReady     = false;
        clearCachedBlocks();
        resetState();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace lp::feature::palette

LP_REGISTER_MODULE(lp::feature::palette::PaletteModule);
