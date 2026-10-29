// Glimmering Weald runtime: bone meal on glow shrooms / lichen growths, huge-shroom faces, pots,
// composting, silk touch, the biome fog and the biome's stoneling spawns
// (docs/spec/worldsurface.md §3 "Glimmering Weald").
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/worldsurface/Climate.h"
#include "feature/worldsurface/Common.h"
#include "feature/worldsurface/Gen.h"
#include "feature/worldsurface/Runtime.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::worldsurface::glimmer {

namespace bs = game::blockstates;

namespace {

constexpr char const* kShroom      = "lepton:glow_shroom";
constexpr char const* kGrowth      = "lepton:glow_lichen_growth";
constexpr char const* kShroomBlock = "lepton:glow_shroom_block";
constexpr char const* kShroomStem  = "lepton:glow_shroom_stem";
constexpr char const* kFogId       = "lepton:glimmering_weald";
constexpr char const* kFogUserId   = "lepton_glimmering_weald";

std::vector<ll::event::ListenerPtr>          gListeners;
std::map<std::pair<int64_t, core::GlobalPos>, uint64_t> gDebounce;
std::unordered_set<int64_t>                  gInFog;
std::unordered_set<int64_t>                  gKnown; ///< players seen by updateFog since they joined
bool                                         gStonelingMissing  = false;
int                                          gStonelingFailures = 0;

Rng& rng() {
    static Rng r(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x6c6c);
    return r;
}

bool debounced(int64_t player, core::GlobalPos const& at) {
    auto key = std::make_pair(player, at);
    auto it  = gDebounce.find(key);
    if (it != gDebounce.end() && tickCount() - it->second < 5) return true;
    gDebounce[key] = tickCount();
    if (gDebounce.size() > 256) gDebounce.clear();
    return false;
}

bool isHugeShroom(std::string const& n) { return n == kShroomBlock || n == kShroomStem; }

Vec3 centerOf(BlockPos const& p) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
}

/// Removes one of the held item (not in creative).
void consumeHeld(Player& p, std::string const& itemId) {
    if (p.isCreative()) return;
    ItemStack item = p.getSelectedItem();
    if (item.isNull() || item.getTypeName() != itemId) return;
    item.remove(1);
    p.setSelectedItem(item);
    p.sendInventory(false);
}

void giveOrDrop(Player& p, BlockSource& region, BlockPos const& pos, ItemStack stack) {
    ItemStack held = p.getSelectedItem();
    if (held.isNull()) {
        p.setSelectedItem(stack);
        p.sendInventory(false);
        return;
    }
    if (!p.add(stack)) region.getLevel().getSpawner().spawnItem(region, stack, nullptr, centerOf(pos), 0);
    p.sendInventory(false);
}

/// GlowLichenGrowthBlock.canSpread: air with a sturdy top below.
bool canSpread(BlockSource& r, BlockPos const& p) {
    if (p.y - 1 < r.getMinHeight() || p.y >= r.getMaxHeight()) return false;
    return r.getBlock(p).isAir() && sturdy(r.getBlock(BlockPos{p.x, p.y - 1, p.z}), 1);
}

FlowerPotBlockActor* emptyPot(BlockSource& r, BlockPos const& pos) {
    if (nameOf(r.getBlock(pos)) != "minecraft:flower_pot") return nullptr;
    auto* be = r.getBlockEntity(pos);
    if (!be) return nullptr;
    auto* pot = static_cast<FlowerPotBlockActor*>(be);
    return pot->mPlant == nullptr ? pot : nullptr;
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !cfg().enableGlimmeringWeald) return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.blockPos();
    auto const& name   = nameOf(region.getBlock(pos));
    auto const& item   = ev.item();
    std::string itemId = item.isNull() ? std::string() : item.getTypeName();
    int64_t     id     = player.getOrCreateUniqueID().rawID;
    int         dim    = static_cast<int>(region.getDimensionId());
    auto        gp     = game::globalPos(dim, pos);

    // bone meal
    if (itemId == "minecraft:bone_meal" && (name == kShroom || name == kGrowth)) {
        ev.cancel();
        if (debounced(id, gp)) return;
        later(1, [dim, pos, id, name] {
            auto* r = game::regionOf(dim);
            auto* p = playerById(id);
            if (!r || !p || nameOf(r->getBlock(pos)) != name) return;
            if (name == kGrowth) {
                bool valid = false;
                for (int f = 0; f < 4; ++f) {
                    auto [dx, dz] = facingStep(f);
                    valid |= canSpread(*r, BlockPos{pos.x + dx, pos.y, pos.z + dz});
                }
                if (!valid) return;
            }
            consumeHeld(*p, "minecraft:bone_meal");
            if (auto d = r->getLevel().getDimension(dim).lock())
                r->getLevel().spawnParticleEffect("minecraft:crop_growth_emitter", centerOf(pos), d.get());
            boneMeal(*r, pos, rng());
        });
        return;
    }
    // potting a glow shroom / lichen growth
    if (auto potted = pottedOf(itemId); !potted.empty() && emptyPot(region, pos)) {
        ev.cancel();
        if (debounced(id, gp)) return;
        later(1, [dim, pos, id, itemId, potted] {
            auto* r = game::regionOf(dim);
            auto* p = playerById(id);
            if (!r || !p || !emptyPot(*r, pos)) return;
            auto const* b = bs::of(potted);
            if (!b || !liveSet(*r, pos, *b)) return;
            consumeHeld(*p, itemId);
            log().info("worldsurface: potted {} at {} {} {}", itemId, pos.x, pos.y, pos.z);
        });
        return;
    }
    // taking the plant out of our pot
    if (auto plant = plantOf(name); !plant.empty()) {
        if (player.isSneaking() && !item.isNull()) return;
        ev.cancel();
        if (debounced(id, gp)) return;
        later(1, [dim, pos, id, name, plant] {
            auto* r = game::regionOf(dim);
            auto* p = playerById(id);
            if (!r || !p || nameOf(r->getBlock(pos)) != name) return;
            auto const* pot = bs::of("minecraft:flower_pot");
            if (!pot || !liveSet(*r, pos, *pot)) return;
            giveOrDrop(*p, *r, pos, game::makeItem(plant, 1));
        });
        return;
    }
    // composting
    if (name == "minecraft:composter") {
        double chance = compostChance(itemId);
        if (chance <= 0) return;
        ev.cancel();
        if (debounced(id, gp)) return;
        later(1, [dim, pos, id, itemId, chance] {
            auto* r = game::regionOf(dim);
            auto* p = playerById(id);
            if (!r || !p || nameOf(r->getBlock(pos)) != "minecraft:composter") return;
            int level = bs::getInt(r->getBlock(pos), "composter_fill_level").value_or(8);
            if (level >= 7) return;
            consumeHeld(*p, itemId);
            bool rose = compost(*r, pos, chance, rng().nextDouble());
            runCommand(
                std::format("playsound {} @a {} {} {}", rose ? "block.composter.fill_success" : "block.composter.fill", pos.x, pos.y, pos.z),
                dim
            );
        });
    }
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& name = nameOf(ev.placedBlock());
    if (!isHugeShroom(name) && name != kGrowth) return;
    auto    pos = ev.pos();
    int     dim = static_cast<int>(player.getDimensionId());
    int64_t id  = player.getOrCreateUniqueID().rawID;
    later(1, [dim, pos, id, name] {
        auto* r = game::regionOf(dim);
        if (!r || nameOf(r->getBlock(pos)) != name) return;
        if (isHugeShroom(name)) {
            updateFaces(*r, pos);
            return;
        }
        // GlowLichenGrowthBlock.mayPlaceOn: a sturdy top face
        if (!sturdy(r->getBlock(BlockPos{pos.x, pos.y - 1, pos.z}), 1)) {
            game::clearBlock(*r, pos);
            if (auto* p = playerById(id); p && !p->isCreative()) giveOrDrop(*p, *r, pos, game::makeItem(kGrowth, 1));
        }
    });
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || player.isCreative() || !cfg().enableGlimmeringWeald) return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.pos();
    auto const& name   = nameOf(region.getBlock(pos));
    if (!isHugeShroom(name)) return;
    if (!EnchantUtils::hasEnchant(Enchant::Type::SilkTouch, player.getSelectedItem())) return;
    // silk touch: the block itself instead of the loot table's drops
    ev.cancel();
    int     dim = static_cast<int>(region.getDimensionId());
    int64_t id  = player.getOrCreateUniqueID().rawID;
    later(1, [dim, pos, id, name] {
        auto* r = game::regionOf(dim);
        if (!r || nameOf(r->getBlock(pos)) != name) return;
        auto& level = r->getLevel();
        level.broadcastLevelEvent(
            SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
            centerOf(pos),
            static_cast<int>(r->getBlock(pos).mNetworkId),
            nullptr
        );
        game::clearBlock(*r, pos);
        level.getSpawner().spawnItem(*r, game::makeItem(name, 1), nullptr, centerOf(pos), 10);
        if (auto* p = playerById(id)) {
            ItemStack tool = p->getSelectedItem();
            if (!tool.isNull()) {
                tool.hurtAndBreak(1, p);
                p->setSelectedItem(tool);
                p->sendInventory(false);
            }
        }
    });
}

void updateFog() {
    auto level = ll::service::getLevel();
    if (!level) return;
    bool enabled = cfg().enableGlimmeringWeald;
    std::unordered_set<int64_t> seen;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulated()) return true;
        int64_t id = p.getOrCreateUniqueID().rawID;
        seen.insert(id);
        bool in  = enabled && playerInGlimmering(p);
        bool had = gInFog.contains(id);
        std::string name = p.getRealName();
        // the fog stack is saved with the player: a player who left (or whose world closed) in
        // the Glimmering Weald may come back with our fog still pushed, so clear it on first sight
        if (gKnown.insert(id).second && !in) runCommand(std::format("fog \"{}\" remove {}", name, kFogUserId));
        if (in == had) return true;
        if (in) {
            runCommand(std::format("fog \"{}\" push {} {}", name, kFogId, kFogUserId));
            gInFog.insert(id);
            log().info("worldsurface: {} entered the Glimmering Weald", name);
        } else {
            runCommand(std::format("fog \"{}\" remove {}", name, kFogUserId));
            gInFog.erase(id);
        }
        return true;
    });
    std::erase_if(gInFog, [&](int64_t id) { return !seen.contains(id); });
    std::erase_if(gKnown, [&](int64_t id) { return !seen.contains(id); });
}

/// The biome's creature spawn entry: stonelings (weight 200, groups 1-4), when U21's mob exists.
void spawnStonelings() {
    if (gStonelingMissing || !cfg().enableGlimmeringWeald) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    GameRuleId rule;
    rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoMobSpawning);
    if (!level->getGameRules().getBool(rule, true)) return;
    auto const* gen = climate::generator();
    if (!gen) return;
    std::vector<std::pair<int64_t, Vec3>> players;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == 0 && !p.isSpectator()) players.emplace_back(p.getOrCreateUniqueID().rawID, p.getPosition());
        return true;
    });
    auto* region = game::regionOf(0);
    if (!region) return;
    for (auto const& [id, pos] : players) {
        double ang  = rng().nextDouble() * 6.283185307179586;
        double dist = 24 + rng().nextDouble() * 24;
        int    x    = static_cast<int>(std::floor(pos.x + std::cos(ang) * dist));
        int    z    = static_cast<int>(std::floor(pos.z + std::sin(ang) * dist));
        int    y0   = static_cast<int>(std::floor(pos.y)) + rng().between(-16, 16);
        for (int dy = 0; dy < 16; ++dy) {
            BlockPos p{x, y0 - dy, z};
            BlockPos below{x, p.y - 1, z};
            if (below.y < region->getMinHeight() || p.y + 1 >= region->getMaxHeight()) break;
            if (!region->hasChunksAt(p, 1, false)) break;
            if (!region->getBlock(p).isAir() || !region->getBlock(BlockPos{x, p.y + 1, z}).isAir()) continue;
            if (!sturdy(region->getBlock(below), 1)) continue;
            if (region->canSeeSky(p)) break;
            if (!climate::isGlimmeringWeald(*gen, region, p)) break;
            AABB box{Vec3{pos.x - 64.f, pos.y - 64.f, pos.z - 64.f}, Vec3{pos.x + 64.f, pos.y + 64.f, pos.z + 64.f}};
            if (game::entities::inBox(*region, box, "lepton:stoneling").size() >= 8) break;
            int group = rng().between(1, 4);
            for (int i = 0; i < group; ++i) {
                Vec3   at{static_cast<float>(x) + 0.5f, static_cast<float>(p.y), static_cast<float>(z) + 0.5f};
                Actor* a = game::entities::spawn(*region, "lepton:stoneling", at);
                if (!a) {
                    // U21 not present (or a spot the game refused): after a few failures in a row
                    // the biome has no stonelings to spawn
                    if (++gStonelingFailures >= 5) gStonelingMissing = true;
                    return;
                }
                gStonelingFailures = 0;
            }
            log().info("worldsurface: {} stonelings spawned in the Glimmering Weald", group);
            break;
        }
    }
}

void sendParticle(Player& p, std::string const& effect, Vec3 const& at) {
    SpawnParticleEffectPacket pkt(
        SpawnParticleEffectPacketPayload(at, effect, static_cast<uchar>(static_cast<int>(p.getDimensionId())), std::nullopt)
    );
    pkt.sendTo(p);
}

/// Java ClientLevel.animateTick: 667 random blocks around the player within 16 blocks and 667
/// within 32 blocks get their animateTick each client tick. The server does it per real player
/// and sends the particles to that player only, so every client (vanilla too) sees them.
void ambientParticles() {
    if (!cfg().enableGlimmeringWeald) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto st = genState();
    if (!st->glowShroom || !st->lichenGrowth || !st->shroomCap) return;
    AmbientTypes types{&st->glowShroom->getBlockType(), &st->lichenGrowth->getBlockType(), &st->shroomCap->getBlockType()};
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulated()) return true;
        auto&    region = p.getDimensionBlockSource();
        BlockPos base   = game::blockPosOf(p.getPosition());
        auto&    r      = rng();
        for (int radius : {16, 32}) {
            for (int i = 0; i < 667; ++i) {
                BlockPos q{
                    base.x + r.nextInt(radius) - r.nextInt(radius),
                    base.y + r.nextInt(radius) - r.nextInt(radius),
                    base.z + r.nextInt(radius) - r.nextInt(radius)
                };
                if (q.y < region.getMinHeight() || q.y >= region.getMaxHeight()) continue;
                animateTick(p, region, q, types, r);
            }
        }
        return true;
    });
}

} // namespace

int animateTick(Player& viewer, BlockSource& region, BlockPos const& pos, AmbientTypes const& types, Rng& r) {
    auto const& type = region.getBlock(pos).getBlockType();
    auto        air  = [&](BlockPos const& q) {
        return q.y >= region.getMinHeight() && q.y < region.getMaxHeight() && region.getBlock(q).isAir();
    };
    if (&type == types.shroom) {
        // GlowShroomBlock: an end rod spark rising from the cap (1 in 12) when the block above is air
        if (r.nextInt(12) != 0 || !air(BlockPos{pos.x, pos.y + 1, pos.z})) return 0;
        sendParticle(
            viewer,
            "lepton:glow_shroom_spore",
            Vec3{
                static_cast<float>(pos.x + 0.4 + r.nextDouble() * 0.2),
                static_cast<float>(pos.y + 0.5 + r.nextDouble() * 0.1),
                static_cast<float>(pos.z + 0.4 + r.nextDouble() * 0.2)
            }
        );
        return 1;
    }
    if (&type == types.growth) {
        // GlowLichenGrowthBlock: 10 mycelium specks in a 5x8x5 box and one at the plant
        sendParticle(viewer, "lepton:glow_lichen_spores", centerOf(pos));
        sendParticle(
            viewer,
            "minecraft:mycelium_dust_particle",
            Vec3{
                static_cast<float>(pos.x + (r.nextDouble() - 0.5) * 0.4 + 0.5),
                static_cast<float>(pos.y + (r.nextDouble() - 0.5) * 0.3 + 0.3),
                static_cast<float>(pos.z + (r.nextDouble() - 0.5) * 0.4 + 0.5)
            }
        );
        return 2;
    }
    if (&type == types.shroomBlock) {
        // HugeGlowShroomBlock (glowing): a spark dripping down (1 in 10) above air or a ring
        if (r.nextInt(10) != 0) return 0;
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (below.y < region.getMinHeight()) return 0;
        auto const& b = region.getBlock(below);
        if (!b.isAir() && nameOf(b) != "lepton:glow_shroom_ring") return 0;
        sendParticle(
            viewer,
            "lepton:glow_shroom_drip",
            Vec3{
                static_cast<float>(pos.x + r.nextDouble()),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z + r.nextDouble())
            }
        );
        return 1;
    }
    return 0;
}

bool boneMeal(BlockSource& region, BlockPos const& pos, Rng& r) {
    auto const& name = nameOf(region.getBlock(pos));
    auto        st   = genState();
    if (name == kShroom) {
        // GlowShroomBlock: always a valid target, 40 % success: the glow_shrooms feature here
        if (r.nextFloat() < 0.4f) gen::placeHugeGlowShroom(region, *st, r, pos);
        return true;
    }
    if (name == kGrowth) {
        int order[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; --i) std::swap(order[i], order[r.nextInt(i + 1)]);
        for (int f : order) {
            auto [dx, dz] = facingStep(f);
            BlockPos n{pos.x + dx, pos.y, pos.z + dz};
            if (canSpread(region, n)) {
                liveSet(region, n, region.getBlock(pos));
                return true;
            }
        }
        return false;
    }
    return false;
}

void updateFaces(BlockSource& region, BlockPos const& pos) {
    static constexpr struct {
        int         dx, dy, dz;
        char const* state;
        char const* opposite;
    } faces[6] = {
        {0,  -1, 0,  "lepton:down",  "lepton:up"   },
        {0,  1,  0,  "lepton:up",    "lepton:down" },
        {0,  0,  -1, "lepton:north", "lepton:south"},
        {0,  0,  1,  "lepton:south", "lepton:north"},
        {-1, 0,  0,  "lepton:west",  "lepton:east" },
        {1,  0,  0,  "lepton:east",  "lepton:west" },
    };
    Block const* self = &region.getBlock(pos);
    auto const&  name = nameOf(*self);
    if (!isHugeShroom(name)) return;
    for (auto const& f : faces) {
        BlockPos n{pos.x + f.dx, pos.y + f.dy, pos.z + f.dz};
        if (n.y < region.getMinHeight() || n.y >= region.getMaxHeight()) continue;
        auto const& nb = region.getBlock(n);
        if (nameOf(nb) != name) continue;
        if (auto const* s = bs::with(*self, {{f.state, false}})) self = s;
        if (auto const* o = bs::with(nb, {{f.opposite, false}}); o && o != &nb) liveSet(region, n, *o);
    }
    if (self != &region.getBlock(pos)) liveSet(region, pos, *self);
}

double compostChance(std::string const& itemId) {
    if (!cfg().enableGlimmeringWeald) return 0;
    if (itemId == kShroom || itemId == kShroomBlock || itemId == kShroomStem || itemId == "lepton:glow_shroom_ring") return 0.65;
    if (itemId == kGrowth) return 0.5;
    return 0;
}

bool compost(BlockSource& region, BlockPos const& pos, double chance, double roll) {
    auto const& b     = region.getBlock(pos);
    int         level = bs::getInt(b, "composter_fill_level").value_or(8);
    if (level >= 7) return false;
    // ComposterBlock.addItem: an empty composter always takes the first layer
    if (!((level == 0 && chance > 0) || roll < chance)) return false;
    auto const* next = bs::with(b, {{"composter_fill_level", level + 1}});
    if (!next || !liveSet(region, pos, *next)) return false;
    if (level + 1 == 7) {
        // ComposterBlock: a full composter becomes ready (level 8) 20 ticks later
        int dim = static_cast<int>(region.getDimensionId());
        later(20, [dim, pos] {
            auto* r = game::regionOf(dim);
            if (!r || !r->hasChunksAt(pos, 0, false)) return;
            auto const& cur = r->getBlock(pos);
            if (nameOf(cur) != "minecraft:composter" || bs::getInt(cur, "composter_fill_level") != 7) return;
            if (auto const* ready = bs::with(cur, {{"composter_fill_level", 8}}); ready && liveSet(*r, pos, *ready))
                runCommand(std::format("playsound block.composter.ready @a {} {} {}", pos.x, pos.y, pos.z), dim);
        });
    }
    return true;
}

std::string pottedOf(std::string const& plant) {
    if (plant == kShroom) return "lepton:potted_glow_shroom";
    if (plant == kGrowth) return "lepton:potted_glow_lichen_growth";
    return {};
}

std::string plantOf(std::string const& potted) {
    if (potted == "lepton:potted_glow_shroom") return kShroom;
    if (potted == "lepton:potted_glow_lichen_growth") return kGrowth;
    return {};
}

bool playerInGlimmering(Player const& p) {
    if (static_cast<int>(p.getDimensionId()) != 0) return false;
    auto const* gen = climate::generator();
    if (!gen) return false;
    auto* region = game::regionOf(0);
    BlockPos at  = game::blockPosOf(p.getPosition());
    if (!region || !region->hasChunksAt(at, 0, false)) return false;
    return climate::isGlimmeringWeald(*gen, region, at);
}

void tick() {
    uint64_t t = tickCount();
    ambientParticles();
    if (t % 20 == 0) updateFog();
    if (t % 400 == 200) spawnStonelings();
}

void onWorldClosed() {
    gInFog.clear();
    gKnown.clear();
    gDebounce.clear();
    gStonelingMissing  = false;
    gStonelingFailures = 0;
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    // take our fog off the players that still have it
    if (auto level = ll::service::getLevel()) {
        level->forEachPlayer([&](Player& p) {
            if (gInFog.contains(p.getOrCreateUniqueID().rawID))
                runCommand(std::format("fog \"{}\" remove {}", p.getRealName(), kFogUserId));
            return true;
        });
    }
    gInFog.clear();
    gKnown.clear();
}

} // namespace lp::feature::worldsurface::glimmer
