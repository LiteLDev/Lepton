// Pickarang / Flamerang: a thrown pick that mines, hits, and comes back (Quark PickarangModule).

#include "feature/throwables/Throwables.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantResult.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/level/Spawner.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::throwables::pickarang {

namespace ent = game::entities;

namespace {

constexpr char const* kCooldown = "lepton_pickarang";

struct Rang {
    int64_t                     id = 0;
    bool                        flame = false;
    ItemStack                   stack;
    int64_t                     owner = 0;
    int                         slot  = 0;
    int                         dim   = 0;
    int                         liveTime = 0;
    bool                        returning = false;
    int                         blockHits = 0;
    std::set<int64_t>           entitiesHit;
    Vec3                        motion{0, 0, 0};
    std::vector<int64_t>        carried;
    bool                        alive = true;
};

std::vector<Rang>& rangs() {
    static std::vector<Rang> r;
    return r;
}
std::unordered_set<int64_t>& fireproof() {
    static std::unordered_set<int64_t> s;
    return s;
}

PickarangType const& typeOf(bool flame) { return flame ? cfg().flamerangType : cfg().pickarangType; }

int enchant(ItemStackBase const& stack, Enchant::Type t) { return EnchantUtils::getEnchantLevel(t, stack); }

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, double s) { return {static_cast<float>(a.x * s), static_cast<float>(a.y * s), static_cast<float>(a.z * s)}; }
double lenSqr(Vec3 a) { return double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z; }

/// Damages a pickarang stack by `amount` (Unbreaking rolls per point). Returns false when it broke.
bool damageRang(ItemStack& stack, int amount, PickarangType const& type) {
    if (type.durability < 0 || amount <= 0) return true;
    int unbreaking = enchant(stack, Enchant::Type::Unbreaking);
    int taken      = 0;
    for (int i = 0; i < amount; ++i) {
        if (unbreaking > 0 && randomInt(unbreaking + 1) != 0) continue;
        ++taken;
    }
    int damage = stack.getDamageValue() + taken;
    if (damage >= type.durability) {
        stack = ItemStack{};
        return false;
    }
    stack.setDamageValue(static_cast<short>(damage));
    return true;
}

/// Ray / box slab test: the fraction of `from -> to` where the segment enters `box`, or < 0.
double segmentHitsBox(Vec3 from, Vec3 to, AABB const& box) {
    double t0 = 0.0, t1 = 1.0;
    double f[3]  = {from.x, from.y, from.z};
    double d[3]  = {double(to.x) - from.x, double(to.y) - from.y, double(to.z) - from.z};
    double lo[3] = {box.min.x, box.min.y, box.min.z};
    double hi[3] = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9) {
            if (f[i] < lo[i] || f[i] > hi[i]) return -1;
            continue;
        }
        double a = (lo[i] - f[i]) / d[i], b = (hi[i] - f[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return -1;
    }
    return t0;
}

bool isPickable(Actor& a) {
    auto type = ent::typeOf(a);
    if (type == "lepton:pickarang" || type == "lepton:flamerang") return true;
    if (a.hasCategory(ActorCategory::Mob)) return true;
    return type == "minecraft:armor_stand" || type == "minecraft:boat" || type == "minecraft:chest_boat"
        || type.find("minecart") != std::string::npos || type == "minecraft:end_crystal";
}

Rang* rangOf(int64_t id) {
    for (auto& r : rangs())
        if (r.id == id && r.alive) return &r;
    return nullptr;
}

void clank(Rang& r, Actor& self, BlockSource* region, BlockPos const* pos) {
    if (region && pos) {
        // a couple of block crack particles where it hit
        particle("minecraft:breaking_item_terrain", self.getPosition(), r.dim);
    }
    playSound("lepton.pickarang.clank", self.getPosition(), 1.0f, 1.0f);
    r.returning = true;
}

void postHit(Rang& r) {
    int pierce = enchant(r.stack, Enchant::Type::Piercing);
    if (static_cast<int>(r.entitiesHit.size()) + r.blockHits > pierce) r.returning = true;
    else if (pierce > 0) r.motion = mul(r.motion, 0.8);
}

/// The vanilla tool of `kind` ("pickaxe", "axe", "shovel", "hoe") of the pickarang's tier.
std::string toolId(int tier, char const* kind) {
    static char const* tiers[] = {"wooden", "stone", "iron", "diamond", "netherite"};
    return std::string("minecraft:") + tiers[std::clamp(tier, 0, 4)] + "_" + kind;
}

/// The tool that "is" the pickarang for `block`: the first equivalent tool that can harvest it,
/// or nullopt when none can.
std::optional<ItemStack> harvestTool(Block const& block, PickarangType const& type) {
    std::vector<char const*> kinds{"pickaxe"};
    if (type.canActAsAxe) kinds.push_back("axe");
    if (type.canActAsShovel) kinds.push_back("shovel");
    if (type.canActAsHoe) kinds.push_back("hoe");
    for (auto* k : kinds) {
        auto tool = game::makeItem(toolId(type.harvestLevel, k));
        if (game::isEmpty(tool)) continue;
        auto* item = tool.mItem.get();
        if (item && item->canDestroySpecial(block)) return tool;
    }
    return std::nullopt;
}

void copyEnchants(ItemStack const& from, ItemStack& to) {
    for (auto t : {Enchant::Type::Fortune, Enchant::Type::SilkTouch, Enchant::Type::Efficiency}) {
        int lvl = enchant(from, t);
        if (lvl <= 0) continue;
        EnchantmentInstance inst;
        inst.mEnchantType = t;
        inst.mLevel       = lvl;
        EnchantUtils::applyEnchant(to, inst, true);
    }
}

float digSpeedFactor(Player& player, bool inWater) {
    float f = 1.0f;
    if (auto* haste = player.getEffect(3u)) f *= 1.0f + (haste->mAmplifier + 1) * 0.2f;
    if (auto* fatigue = player.getEffect(4u)) {
        switch (fatigue->mAmplifier) {
        case 0: f *= 0.3f; break;
        case 1: f *= 0.09f; break;
        case 2: f *= 0.0027f; break;
        default: f *= 8.1e-4f; break;
        }
    }
    if (inWater) f /= 5.0f;
    return f;
}

bool isImmune(Block const& block) {
    static std::set<std::string> const ids{
        "minecraft:sculk_sensor", "minecraft:calibrated_sculk_sensor", "minecraft:sculk", "minecraft:sculk_vein",
        "minecraft:sculk_catalyst", "minecraft:sculk_shrieker", "minecraft:reinforced_deepslate",
    };
    return ids.contains(block.getTypeName()) || block.hasTag(HashedString("lepton:pickarang_immune"));
}

void hitBlock(Rang& r, Actor& self, BlockSource& region, BlockPos const& pos, int face) {
    auto const& block = region.getBlock(pos);
    int         pierce = enchant(r.stack, Enchant::Type::Piercing);
    if (pierce == 0 || block.getBlockType().mSolid) {
        r.blockHits++;
        postHit(r);
    }
    auto* owner = playerById(r.owner);
    if (!owner || owner->isClientSide()) return;
    auto const& type     = typeOf(r.flame);
    float       hardness = block.getBlockType().getDestroySpeed();
    if (hardness < 0) return; // unbreakable: Quark's progress is 0 and it just returns
    auto  tool    = harvestTool(block, type);
    bool  harvest = tool.has_value() || !block.getBlockType().mRequiresCorrectToolForDrops;
    float f       = digSpeedFactor(*owner, self.isInWater());
    double equivalent = hardness == 0 ? 0.0 : (hardness * (harvest ? 30.0 : 100.0)) / (f * 30.0);
    if (!((equivalent <= type.maxHardness && !isImmune(block)) || isCreative(*owner))) {
        clank(r, self, &region, &pos);
        return;
    }
    ItemStack use = tool ? *tool : game::makeItem(toolId(type.harvestLevel, "pickaxe"));
    copyEnchants(r.stack, use);
    ItemStack prev = owner->getSelectedItem();
    owner->setSelectedItem(use);
    bool broken = false;
    if (owner->mGameMode.get()) broken = owner->mGameMode->destroyBlock(pos, static_cast<uchar>(face));
    owner->setSelectedItem(prev);
    if (!broken) {
        clank(r, self, &region, &pos);
        return;
    }
    if (hardness != 0 && !isCreative(*owner)) damageRang(r.stack, 1, type);
    if (game::isEmpty(r.stack)) {
        playSound("random.break", self.getPosition(), 1.0f, 1.0f);
        r.alive = false;
    }
}

void hitEntity(Rang& r, Actor& self, Actor& target) {
    auto tid = ent::idOf(target);
    r.entitiesHit.insert(tid);
    postHit(r);
    if (auto* other = rangOf(tid)) {
        other->returning = true;
        clank(r, self, nullptr, nullptr);
        return;
    }
    auto* owner = playerById(r.owner);
    if (!owner) return;
    float     before = static_cast<float>(target.getHealth());
    ItemStack prev   = owner->getSelectedItem();
    short     dmg0   = r.stack.getDamageValue();
    owner->setSelectedItem(r.stack);
    owner->attack(target, SharedTypes::Legacy::ActorDamageCause::EntityAttack);
    owner->setSelectedItem(prev);
    r.stack.setDamageValue(dmg0); // Quark's hurtEnemy costs exactly 2
    if (!isCreative(*owner)) damageRang(r.stack, 2, typeOf(r.flame));
    if (game::isEmpty(r.stack)) {
        playSound("random.break", self.getPosition(), 1.0f, 1.0f);
        r.alive = false;
        return;
    }
    if (target.isAlive() && static_cast<float>(target.getHealth()) == before) clank(r, self, nullptr, nullptr);
}

void checkImpact(Rang& r, Actor& self) {
    auto& region = self.getDimensionBlockSource();
    int   tries  = 100;
    bool  doEntities = true;
    while (r.alive && !r.returning && tries-- > 0) {
        Vec3 from = self.getPosition();
        from.y += 0.2f;
        Vec3 to = add(from, r.motion);
        if (doEntities) {
            AABB box{
                Vec3{std::min(from.x, to.x) - 1.2f, std::min(from.y, to.y) - 1.2f, std::min(from.z, to.z) - 1.2f},
                Vec3{std::max(from.x, to.x) + 1.2f, std::max(from.y, to.y) + 1.2f, std::max(from.z, to.z) + 1.2f}
            };
            Actor* best  = nullptr;
            double bestT = 2.0;
            for (auto* a : ent::inBox(region, box)) {
                if (!a || a == &self || !a->isAlive() || a->isSpectator()) continue;
                auto aid = ent::idOf(*a);
                if (aid == r.owner || r.entitiesHit.contains(aid) || !isPickable(*a)) continue;
                AABB bb = a->getAABB();
                bb.min.x -= 0.3f; bb.min.y -= 0.3f; bb.min.z -= 0.3f;
                bb.max.x += 0.3f; bb.max.y += 0.3f; bb.max.z += 0.3f;
                double t = segmentHitsBox(from, to, bb);
                if (t >= 0 && t < bestT) {
                    bestT = t;
                    best  = a;
                }
            }
            if (best) {
                hitEntity(r, self, *best);
                continue;
            }
            doEntities = false;
        }
        auto hit = region.clip(
            from, to, false, ShapeType::Collision, 200, false, false, &self,
            [](BlockSource const&, Block const&, bool) { return true; }, false
        );
        if (hit.mType != HitResultType::Tile) return;
        hitBlock(r, self, region, hit.mBlock, hit.mFacing);
    }
}

void dropAt(Rang& r, Actor& self) {
    if (!game::isEmpty(r.stack)) {
        auto& region = self.getDimensionBlockSource();
        Vec3  p      = self.getPosition();
        while (p.y < 320 && !region.getBlock(game::blockPosOf(p)).isAir()) p.y += 1;
        auto* item = region.getLevel().getSpawner().spawnItem(region, r.stack, nullptr, p, 0);
        if (item && r.flame) fireproof().insert(ent::idOf(*item));
    }
    r.alive = false;
}

void giveCarried(Rang& r, Player& player) {
    for (auto id : r.carried) {
        auto* a = ent::find(id);
        if (!a || !a->isAlive()) continue;
        auto type = ent::typeOf(*a);
        if (type == "minecraft:item") {
            auto& itemActor = *static_cast<ItemActor*>(a);
            ItemStack stack = itemActor.mItem;
            ent::despawn(*a);
            giveOrDrop(player, stack);
        } else if (type == "minecraft:xp_orb") {
            a->teleport(player.getPosition(), player.getDimensionId());
            static_cast<ExperienceOrb*>(a)->playerTouch(player);
        }
        fireproof().erase(id);
    }
    r.carried.clear();
}

void tickRang(Rang& r) {
    auto* self = ent::find(r.id);
    if (!self) return; // chunk unloaded: wait
    auto* owner = playerById(r.owner);
    if (!owner || !owner->isAlive() || static_cast<int>(owner->getDimensionId()) != static_cast<int>(self->getDimensionId())) {
        dropAt(r, *self);
        for (auto id : r.carried) fireproof().erase(id);
        ent::despawn(*self);
        return;
    }
    if (!r.returning) checkImpact(r, *self);
    if (!r.alive) {
        giveCarried(r, *owner);
        ent::despawn(*self);
        return;
    }
    Vec3 pos  = self->getPosition();
    Vec3 next = add(pos, r.motion);
    self->teleport(next, self->getDimensionId());
    if (r.flame && randomDouble() < 0.4) {
        Vec3 p{
            static_cast<float>(next.x - r.motion.x * 0.25 + (randomDouble() - 0.5) * 0.4),
            static_cast<float>(next.y + 0.2 - r.motion.y * 0.25 + (randomDouble() - 0.5) * 0.4),
            static_cast<float>(next.z - r.motion.z * 0.25 + (randomDouble() - 0.5) * 0.4)
        };
        particle("minecraft:basic_flame_particle", p, r.dim);
    }
    r.motion = mul(r.motion, self->isInWater() ? 0.8 : 0.99);
    r.liveTime++;

    int eff = enchant(r.stack, Enchant::Type::Efficiency);
    if (!r.returning) {
        if (r.liveTime > typeOf(r.flame).timeout) r.returning = true;
    } else {
        auto& region = self->getDimensionBlockSource();
        AABB  box{Vec3{next.x - 2.2f, next.y - 2.2f, next.z - 2.2f}, Vec3{next.x + 2.2f, next.y + 2.6f, next.z + 2.2f}};
        for (auto* a : ent::inBox(region, box)) {
            if (!a || !a->isAlive()) continue;
            auto type = ent::typeOf(*a);
            if (type != "minecraft:item" && type != "minecraft:xp_orb") continue;
            auto aid = ent::idOf(*a);
            bool taken = false;
            for (auto const& o : rangs())
                if (std::find(o.carried.begin(), o.carried.end(), aid) != o.carried.end()) taken = true;
            if (taken) continue;
            r.carried.push_back(aid);
            if (r.flame) fireproof().insert(aid);
            if (type == "minecraft:item") static_cast<ItemActor*>(a)->mPickupDelay = 5;
        }
        Vec3 ownerPos = owner->getFeetPos();
        ownerPos.y += 1.0f;
        Vec3 delta = sub(ownerPos, next);
        if (lenSqr(delta) < 3.25 + eff * 0.25) {
            playSound("lepton.pickarang.pickup", next, 1.0f, 1.0f);
            auto& pinv = game::inv::playerInventory(*owner);
            if (r.slot >= 0 && r.slot < pinv.getContainerSize() && game::isEmpty(pinv.getItem(r.slot))) {
                pinv.setItem(r.slot, r.stack);
                owner->refreshInventory();
            } else {
                giveOrDrop(*owner, r.stack);
            }
            giveCarried(r, *owner);
            r.alive = false;
            ent::despawn(*self);
            log().info("throwables: pickarang caught by {}", owner->getRealName());
            return;
        }
        double len = std::sqrt(lenSqr(delta));
        r.motion   = mul(delta, (0.7 + eff * 0.325) / std::max(len, 1e-6));
    }
    // carried items ride along
    for (auto id : r.carried) {
        if (auto* a = ent::find(id)) {
            a->teleport(Vec3{next.x, next.y - 0.2f, next.z}, self->getDimensionId());
            if (ent::typeOf(*a) == "minecraft:item") static_cast<ItemActor*>(a)->mPickupDelay = 5;
        }
    }
}

// ---- persistence ------------------------------------------------------------------------------
constexpr char const* kDbKey = "lp:throwables:rangs";

void save() {
    if (!game::db::available()) return;
    auto arr = nlohmann::json::array();
    for (auto const& r : rangs()) {
        if (!r.alive) continue;
        arr.push_back({
            {"id", r.id}, {"flame", r.flame}, {"stack", game::itemToSnbt(r.stack)}, {"owner", r.owner},
            {"slot", r.slot}, {"dim", r.dim}, {"liveTime", r.liveTime}, {"returning", r.returning},
            {"blockHits", r.blockHits}, {"motion", {r.motion.x, r.motion.y, r.motion.z}}, {"carried", r.carried},
        });
    }
    game::db::save(kDbKey, arr);
}

void load() {
    rangs().clear();
    auto v = game::db::load(kDbKey);
    if (!v || !v->is_array()) return;
    for (auto const& j : *v) {
        Rang r;
        r.id        = j.value("id", int64_t{0});
        r.flame     = j.value("flame", false);
        r.stack     = game::itemFromSnbt(j.value("stack", std::string("{}")));
        r.owner     = j.value("owner", int64_t{0});
        r.slot      = j.value("slot", 0);
        r.dim       = j.value("dim", 0);
        r.liveTime  = j.value("liveTime", 0);
        r.returning = j.value("returning", true);
        r.blockHits = j.value("blockHits", 0);
        if (auto m = j.find("motion"); m != j.end() && m->is_array() && m->size() == 3)
            r.motion = Vec3{(*m)[0].get<float>(), (*m)[1].get<float>(), (*m)[2].get<float>()};
        if (auto c = j.find("carried"); c != j.end() && c->is_array()) r.carried = c->get<std::vector<int64_t>>();
        if (r.flame)
            for (auto id : r.carried) fireproof().insert(id);
        rangs().push_back(std::move(r));
    }
}

bool isStray(Actor& a) { return !rangOf(ent::idOf(a)); }

} // namespace

bool isRang(std::string_view id) { return id == kPickarang || id == kFlamerang; }
int  inFlight() {
    return static_cast<int>(std::count_if(rangs().begin(), rangs().end(), [](Rang const& r) { return r.alive; }));
}

bool onUse(Player& player, ItemStack& item) {
    auto id = game::itemId(item);
    if (!isRang(id)) return false;
    bool flame = id == kFlamerang;
    if (!cfg().enablePickarang || (flame && !cfg().enableFlamerang)) return true; // disabled: nothing happens
    if (player.getItemCooldownLeft(HashedString(kCooldown)) > 0) return true;

    int   eff    = enchant(item, Enchant::Type::Efficiency);
    auto& region = player.getDimensionBlockSource();
    Vec3  eye    = player.getEyePos();
    Vec3  spawnAt{eye.x, eye.y - 0.2f, eye.z};
    auto* self = ent::spawn(region, flame ? "lepton:flamerang" : "lepton:pickarang", spawnAt);
    if (!self) return true;

    Rang r;
    r.id    = ent::idOf(*self);
    r.flame = flame;
    r.stack = item;
    r.owner = ent::idOf(player);
    r.slot  = player.getSelectedItemSlot();
    r.dim   = static_cast<int>(player.getDimensionId());
    Vec3 dir   = player.getViewVector(1.0f);
    r.motion   = mul(dir, 1.5 + eff * 0.325);
    Vec3 pv    = player.getPosDelta();
    r.motion.x += pv.x;
    r.motion.z += pv.z;
    if (!player.isOnGround()) r.motion.y += pv.y;
    rangs().push_back(std::move(r));

    setSelected(player, ItemStack{});
    float pitch = 0.4f / (static_cast<float>(randomDouble()) * 0.4f + 0.8f);
    playSound("lepton.pickarang.throw", player.getPosition(), 0.5f + eff * 0.14f, pitch);
    auto const& type = typeOf(flame);
    if (!isCreative(player) && type.cooldown > 0) {
        int cd = type.cooldown - eff;
        if (cd > 0) player.startItemCooldown(HashedString(kCooldown), cd, true);
    }
    log().info("throwables: {} thrown by {}", flame ? "flamerang" : "pickarang", player.getRealName());
    return true;
}

void tick() {
    auto& list = rangs();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].alive) tickRang(list[i]);
    }
    std::erase_if(list, [](Rang const& r) { return !r.alive; });
    // pickarang entities nobody owns (e.g. state lost) fall back to items
    static int sweep = 0;
    if (++sweep % 100 == 0) {
        for (char const* type : {"lepton:pickarang", "lepton:flamerang"})
            for (auto* a : ent::ofType(type))
                if (a && isStray(*a)) ent::despawn(*a);
    }
}

// ---- hooks ------------------------------------------------------------------------------------
namespace {

// Items carried by a flamerang (and dropped flamerangs) don't burn.
LL_TYPE_INSTANCE_HOOK(RangFireproofHook, ll::memory::HookPriority::Normal, ItemActor, &ItemActor::$isFireImmune, bool) {
    if (fireproof().contains(this->getOrCreateUniqueID().rawID)) return true;
    if (game::itemId(this->mItem) == kFlamerang) return true;
    return origin();
}

// Pickarangs accept Piercing (Quark: supportsEnchantment); checked as if they were a crossbow.
LL_STATIC_HOOK(
    RangPiercingHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::canEnchant,
    EnchantResult,
    ItemStackBase const& item,
    EnchantmentInstance const& enchantInst,
    bool allowNonVanilla
) {
    if (enchantInst.mEnchantType == Enchant::Type::Piercing && isRang(game::itemId(item))) {
        auto crossbow = game::makeItem("minecraft:crossbow");
        if (item.mUserData) crossbow.setUserData(item.mUserData->clone());
        return origin(crossbow, enchantInst, allowNonVanilla);
    }
    return origin(item, enchantInst, allowNonVanilla);
}

} // namespace

void install() {
    RangFireproofHook::hook();
    RangPiercingHook::hook();
    static bool registered = false;
    if (!registered) {
        registered = true;
        game::db::onWorldReady([] { load(); });
        game::db::onWorldSave([] { save(); });
    }
    if (game::db::available()) load();
}

void uninstall() {
    save();
    RangFireproofHook::unhook();
    RangPiercingHook::unhook();
    rangs().clear();
    fireproof().clear();
}

void onWorldClosed() {
    rangs().clear();
    fireproof().clear();
}

} // namespace lp::feature::throwables::pickarang
