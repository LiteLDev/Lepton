// U12 crystals / BeaconRedirection: Quark's beam walk (clusters bend the beam, corundum / lit lamps
// tint it, tinted glass dims it), beacon activity, and the beam drawn with lepton:beacon_beam entities.

#include "feature/crystals/Beacon.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/level/block/actor/BeaconBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"

#include "feature/crystals/Common.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "mod/Log.h"
#include "mod/Module.h"
#ifdef LP_CLIENT
#include "net/Net.h"
#endif

namespace lp::feature::crystals {

namespace bs = game::blockstates;

namespace {
bool gInstalled = false;
bool beaconOn() { return gInstalled && cfg().enableBeaconRedirection; }

constexpr int kNoAlpha = -256;

int topNonAir(BlockSource& region, int x, int z) {
    // Java Heightmap.Types.WORLD_SURFACE: one above the highest non-air block
    for (int y = region.getMaxHeight() - 1; y >= region.getMinHeight(); --y)
        if (!region.getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
    return region.getMinHeight();
}

bool isRedirecting(Block const& b) {
    if (cfg().enableCorundum) return isCluster(b);
    return b.getTypeName() == "minecraft:amethyst_cluster";
}
int redirectFacing(Block const& b) {
    if (isCluster(b)) return clusterFacing(b);
    if (auto f = bs::getString(b, "minecraft:block_face")) return faceFromName(*f);
    if (auto f = bs::getInt(b, "facing_direction")) return *f;
    return 1;
}
uint32_t redirectColor(Block const& b) {
    int c = colorOf(b);
    return isCluster(b) && c >= 0 ? kColors[c].beacon : 0x1000000u; // Quark: amethyst = 16777216
}
uint32_t mix(uint32_t cur, uint32_t t, int wCur, int wT) {
    auto ch = [&](int s) {
        return ((static_cast<int>((cur >> s) & 255) * wCur + static_cast<int>((t >> s) & 255) * wT) / (wCur + wT)) << s;
    };
    return static_cast<uint32_t>(ch(16) + ch(8) + ch(0));
}
int lightBlock(Block const& b) { return b.mDirectData->mLight->mValue; }
} // namespace

Beam computeBeam(BlockSource& region, BlockPos const& beacon) {
    auto const& c = cfg();
    Beam        beam;
    BlockPos    curr       = beacon;
    int         horizontal = c.horizontalMoveLimit;
    bool        broke      = false;
    uint32_t    color      = 0xffffff;
    int         alpha      = 255;
    int         lastDir    = -1;
    BeamSegment seg{1, {0, 0, 0}, 1, color, alpha};
    std::set<std::tuple<int, int, int>> seen;
    bool hardColorSet = false;
    int  minY = region.getMinHeight(), maxY = region.getMaxHeight();
    auto inWorld = [&](BlockPos const& p) { return p.y >= minY && p.y < maxY; };

    while (inWorld(curr) && horizontal > 0) {
        if (seg.dir == 1 && seg.dir != lastDir) {
            int h = topNonAir(region, curr.x, curr.z);
            if (h == curr.y + 1) {
                seg.height = h + 1000;
                seg.toSky  = true;
                break;
            }
            lastDir = seg.dir;
        }
        curr = offset(curr, seg.dir);
        if (seg.dir >= 2) --horizontal;
        else horizontal = c.horizontalMoveLimit;
        if (!inWorld(curr)) break;

        auto const& b           = region.getBlock(curr);
        auto        targetColor = beaconColorOf(b);
        int         targetAlpha = kNoAlpha;
        if (c.allowTintedGlassTransparency && b.getTypeName() == "minecraft:tinted_glass")
            targetAlpha = alpha < 77 ? 0 : 2 * (alpha / 3);
        BlockPos rel{curr.x - beacon.x, curr.y - beacon.y, curr.z - beacon.z};

        if (isRedirecting(b)) {
            int dir = redirectFacing(b);
            if (dir == seg.dir) {
                ++seg.height;
            } else {
                beam.segments.push_back(seg);
                uint32_t mixed = mix(color, redirectColor(b), 1, 3);
                color          = mixed;
                if (targetAlpha != kNoAlpha) alpha = targetAlpha;
                beam.redirected = true;
                lastDir         = seg.dir;
                seg             = BeamSegment{dir, rel, 1, mixed, alpha};
            }
        } else if (targetColor || targetAlpha != kNoAlpha) {
            if (targetColor && *targetColor == color && targetAlpha == alpha) {
                ++seg.height;
            } else {
                beam.segments.push_back(seg);
                uint32_t mixed = color;
                if (targetColor) {
                    mixed = mix(color, *targetColor, 1, 1);
                    if (!hardColorSet) {
                        mixed        = *targetColor;
                        hardColorSet = true;
                    }
                    color = mixed;
                    if (!b.getTypeName().starts_with("minecraft:")) beam.quarkTint = true;
                }
                if (targetAlpha != kNoAlpha) {
                    alpha          = targetAlpha;
                    beam.quarkTint = true;
                }
                lastDir = seg.dir;
                seg     = BeamSegment{seg.dir, rel, 1, mixed, alpha};
            }
        } else {
            bool bedrock = b.getTypeName() == "minecraft:bedrock"; // #quark:beacon_transparent
            if (!bedrock && lightBlock(b) >= 15) {
                broke = true;
                break;
            }
            ++seg.height;
            if (bedrock) continue;
        }
        if (!seen.insert({curr.x, curr.y, curr.z}).second) {
            broke = true;
            break;
        }
    }
    if (horizontal == 0 || curr.y <= minY) broke = true;
    if (!broke) {
        beam.segments.push_back(seg);
    } else {
        beam.segments.clear();
    }
    beam.broke = broke;
    return beam;
}

namespace {

// ---- beam entities ------------------------------------------------------------------------------------

struct Piece {
    BlockPos at;
    int      dir, len, glow;
    uint32_t color;
    friend bool operator==(Piece const&, Piece const&) = default;
};

int glowIndex(int alpha) {
    static constexpr int kSteps[5] = {255, 170, 112, 74, 0};
    int best = 0;
    for (int i = 0; i < 5; ++i)
        if (std::abs(kSteps[i] - alpha) < std::abs(kSteps[best] - alpha)) best = i;
    return best;
}

std::vector<Piece> piecesOf(Beam const& beam, BlockPos const& beacon, int maxY) {
    std::vector<Piece> out;
    for (auto const& s : beam.segments) {
        BlockPos start{beacon.x + s.offset.x, beacon.y + s.offset.y, beacon.z + s.offset.z};
        int      len = s.height;
        if (s.dir == 1) len = std::min(len, std::max(1, maxY - start.y)); // to the top of the world
        for (int done = 0; done < len; done += 64) {
            Piece p{offset(start, s.dir, done), s.dir, std::min(64, len - done), glowIndex(s.alpha), s.color};
            out.push_back(p);
        }
    }
    return out;
}

struct BeaconState {
    std::vector<Piece>   pieces;
    std::vector<int64_t> entities;
    uint64_t             lastSeen = 0;
    Beam                 beam;
    bool                 loggedRedirect = false;
};
std::map<core::GlobalPos, BeaconState>& beacons() {
    static std::map<core::GlobalPos, BeaconState> m;
    return m;
}

void setProps(Actor& a, Piece const& p) {
    auto props = a.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return;
    auto h = [](char const* n) { return HashedString::computeHash(n); };
    ActorPropertyUtils::tryQueueIntValue(*props, a, h("lepton:dir"), p.dir);
    ActorPropertyUtils::tryQueueIntValue(*props, a, h("lepton:len"), p.len);
    ActorPropertyUtils::tryQueueIntValue(*props, a, h("lepton:glow"), p.glow);
    ActorPropertyUtils::tryQueueFloatValue(*props, a, h("lepton:r"), ((p.color >> 16) & 255) / 255.f);
    ActorPropertyUtils::tryQueueFloatValue(*props, a, h("lepton:g"), ((p.color >> 8) & 255) / 255.f);
    ActorPropertyUtils::tryQueueFloatValue(*props, a, h("lepton:b"), (p.color & 255) / 255.f);
}

void clearEntities(BeaconState& st) {
    for (auto id : st.entities)
        if (auto* a = game::entities::find(id)) game::entities::despawn(*a);
    st.entities.clear();
    st.pieces.clear();
}

void updateEntities(BlockSource& region, core::GlobalPos const& key, BeaconState& st, std::vector<Piece> const& want) {
    bool alive = std::all_of(st.entities.begin(), st.entities.end(), [](int64_t id) { return game::entities::find(id) != nullptr; });
    if (want == st.pieces && alive) return;
    clearEntities(st);
    for (auto const& p : want) {
        Vec3 at{p.at.x + 0.5f, static_cast<float>(p.at.y), p.at.z + 0.5f};
        auto* a = game::entities::spawn(region, "lepton:beacon_beam", at);
        if (!a) continue;
        setProps(*a, p);
        st.entities.push_back(game::entities::idOf(*a));
    }
    st.pieces = want;
    (void)key;
}

void applyEffects(BeaconBlockActor& beacon, BlockSource& region) {
    int levels = beacon.mNumLevels;
    if (levels <= 0) return;
    int primary = beacon.mPrimaryEffectId, secondary = beacon.mSecondaryEffectId;
    if (primary <= 0) return;
    auto const& pos      = beacon.getPosition();
    double      range    = levels * 10 + 10;
    int         duration = (9 + levels * 2) * 20;
    int         amp      = (levels >= 4 && primary == secondary) ? 1 : 0;
    auto        level    = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != region.getDimensionId()) return true;
        auto const& v = p.getPosition();
        if (std::abs(v.x - (pos.x + 0.5)) > range + 0.5 || std::abs(v.z - (pos.z + 0.5)) > range + 0.5) return true;
        if (v.y < pos.y - range) return true; // Java: the box reaches up to the build height
        MobEffectInstance e(static_cast<uint>(primary), EffectDuration{duration});
        e.mAmplifier = amp;
        e.mAmbient   = true;
        p.addEffect(e);
        if (levels >= 4 && secondary > 0 && secondary != primary) {
            MobEffectInstance s(static_cast<uint>(secondary), EffectDuration{duration});
            s.mAmbient = true;
            p.addEffect(s);
        }
        return true;
    });
}

void serverTick(BeaconBlockActor& beacon, BlockSource& region) {
    auto const& pos = beacon.getPosition();
    auto        key = game::globalPos(region, pos);
    auto&       st  = beacons()[key];
    bool        fresh = st.lastSeen == 0;
    st.lastSeen       = currentTick();
    uint64_t phase    = static_cast<uint32_t>(javaHash(pos.x, pos.y, pos.z)) % 20u;
    if (fresh || (currentTick() + phase) % 20 == 0) {
        st.beam  = computeBeam(region, pos);
        bool own = !st.beam.broke && (st.beam.redirected || st.beam.quarkTint);
        updateEntities(region, key, st, own ? piecesOf(st.beam, pos, region.getMaxHeight()) : std::vector<Piece>{});
        if (st.beam.redirected && !st.beam.broke && st.pieces.size() && !st.loggedRedirect) {
            st.loggedRedirect = true;
            log().info("crystals: beacon at {} {} {} redirected ({} segments)", pos.x, pos.y, pos.z, st.beam.segments.size());
        }
        if (!st.beam.redirected) st.loggedRedirect = false;
    }
    if ((currentTick() + phase) % 80 == 0 && st.beam.redirected && !st.beam.broke) applyEffects(beacon, region);
}

void cleanup() {
    auto now = currentTick();
    for (auto it = beacons().begin(); it != beacons().end();) {
        if (now - it->second.lastSeen > 100) {
            clearEntities(it->second);
            it = beacons().erase(it);
        } else ++it;
    }
    if (now % 100 != 0) return;
    std::set<int64_t> known;
    for (auto const& [k, st] : beacons()) known.insert(st.entities.begin(), st.entities.end());
    for (auto* a : game::entities::ofType("lepton:beacon_beam"))
        if (a && !known.contains(game::entities::idOf(*a))) game::entities::despawn(*a);
}

#ifdef LP_CLIENT
// Client: the beam of a beacon whose Quark beam differs from vanilla's (redirected, tinted by
// Quark blocks, dimmed) or is broken is drawn by the server's lepton:beacon_beam entities, so the
// vanilla beam sections are dropped. Only when the server runs Lepton (local worlds included).
struct ClientBeacon {
    uint64_t nextCheck = 0;
    bool     hide      = false;
};
std::map<core::GlobalPos, ClientBeacon>& clientBeacons() {
    static std::map<core::GlobalPos, ClientBeacon> m;
    return m;
}
uint64_t& clientTicks() {
    static uint64_t t = 0;
    return t;
}

void clientTick(BeaconBlockActor& beacon, BlockSource& region) {
    if (!net::serverHasMod() && !ll::service::getLevel()) return;
    auto const& pos = beacon.getPosition();
    auto        key = game::globalPos(region, pos);
    auto&       st  = clientBeacons()[key];
    auto        now = ++clientTicks();
    if (now >= st.nextCheck) {
        st.nextCheck = now + 10;
        auto beam    = computeBeam(region, pos);
        st.hide      = beam.broke || beam.redirected || beam.quarkTint;
        if (clientBeacons().size() > 256) clientBeacons().clear();
    }
    if (st.hide) beacon.mBeamSections->clear();
}
#endif

// BeaconBlockActor::tick overrides IVanillaTickBlockActorComponent::tick (a secondary base), so
// MSVC calls it with `this` pointing at that subobject, not at the BeaconBlockActor: the hook's
// `this` is reinterpreted and the actor is fetched through getBlockActor().
LL_TYPE_INSTANCE_HOOK(
    BeaconTickHook,
    ll::memory::HookPriority::Normal,
    BeaconBlockActor,
    &BeaconBlockActor::$tick,
    void,
    BlockSource& region
) {
    if (!beaconOn()) {
        origin(region);
        return;
    }
    auto& self = static_cast<BeaconBlockActor&>(reinterpret_cast<IVanillaTickBlockActorComponent*>(this)->getBlockActor());
    if (region.getLevel().isClientSide()) {
        origin(region);
#ifdef LP_CLIENT
        clientTick(self, region);
#endif
        return;
    }
    // Java: a broken beam leaves the beacon without effects. Bedrock's tick re-checks the pyramid
    // and applies the effects in one go, so the chosen effects are hidden from the vanilla tick
    // while the beam is broken.
    auto key   = game::globalPos(region, self.getPosition());
    bool broke = false;
    if (auto it = beacons().find(key); it != beacons().end()) broke = it->second.beam.broke;
    if (broke) {
        int primary = self.mPrimaryEffectId, secondary = self.mSecondaryEffectId;
        self.mPrimaryEffectId   = 0;
        self.mSecondaryEffectId = 0;
        origin(region);
        self.mPrimaryEffectId   = primary;
        self.mSecondaryEffectId = secondary;
    } else {
        origin(region);
    }
    serverTick(self, region);
}

class BeaconModule {
public:
    void install() {
        gInstalled = true;
        static bool once = false;
        if (!once) {
            once = true;
            tickListeners().push_back([] {
                if (gInstalled) cleanup();
            });
        }
        BeaconTickHook::hook();
    }
    void uninstall() {
        gInstalled = false;
        BeaconTickHook::unhook();
        for (auto& [k, st] : beacons()) clearEntities(st);
        beacons().clear();
    }
};

} // namespace

bool beaconTicked(core::GlobalPos const& beacon) { return beacons().contains(beacon); }

std::vector<int64_t> beamEntitiesOf(core::GlobalPos const& beacon) {
    auto it = beacons().find(beacon);
    return it == beacons().end() ? std::vector<int64_t>{} : it->second.entities;
}

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::BeaconModule);
