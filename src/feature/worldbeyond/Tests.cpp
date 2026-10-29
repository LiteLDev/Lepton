// Selftests of U29 worldbeyond (dx 2900..2999). Structures are built high above the test strip
// (y + 120) so they cannot touch other units' tests; generation tests run in the real Nether / End.

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/Container.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/worldbeyond/Blocks.h"
#include "feature/worldbeyond/Chorus.h"
#include "feature/worldbeyond/Config.h"
#include "feature/worldbeyond/Generators.h"
#include "feature/worldbeyond/JavaMath.h"
#include "feature/worldbeyond/Myalite.h"
#include "feature/worldbeyond/MyaliteColor.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
using namespace lp::feature::worldbeyond;

constexpr int kSkyOffset  = 120; ///< structures are built this far above the test strip
constexpr int kItemHueStep = 6;  ///< tools/pack/worldbeyond.ps1 $itemHue (Quark: colour of 0,0,0)

/// Positions written by a test, cleared (air) when it ends.
struct Scratch {
    BlockSource&          region;
    std::vector<BlockPos> written;
    explicit Scratch(BlockSource& r) : region(r) {}
    ~Scratch() { clear(); }
    void set(BlockPos const& p, Block const& b) {
        written.push_back(p);
        lp::game::setBlock(region, p, b);
    }
    void set(BlockPos const& p, char const* id) {
        if (auto const* b = lp::game::blockWith(id)) set(p, *b);
    }
    void clear() {
        auto const* air = lp::game::blockWith("minecraft:air");
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            if (air && region.hasChunksAt(*it, 0, false)) lp::game::setBlock(region, *it, *air);
        }
        written.clear();
    }
    /// A square end stone platform of half-size `r` at `c` (the plants go on top: y + 1).
    void platform(BlockPos const& c, int r) {
        for (int x = -r; x <= r; ++x)
            for (int z = -r; z <= r; ++z) set(BlockPos{c.x + x, c.y, c.z + z}, "minecraft:end_stone");
    }
};

/// Restores the config after a test that changes it.
struct ConfigGuard {
    WorldBeyondConfig saved = lp::config::get<WorldBeyondConfig>();
    ~ConfigGuard() {
        lp::config::mut<WorldBeyondConfig>() = saved;
        publishConfig();
    }
    WorldBeyondConfig& edit() { return lp::config::mut<WorldBeyondConfig>(); }
    void               apply() { publishConfig(); }
};

std::string idAt(BlockSource& r, BlockPos const& p) { return lp::game::blockIdAt(r, p); }

/// The sky test position of `dx` and the overworld region (the player stands near it).
struct Sky {
    BlockSource* region = nullptr;
    BlockPos     pos;
};
ll::coro::CoroTask<Sky> sky(Context& ctx, int dx) {
    auto g = ctx.testPos(dx);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    Sky s;
    s.region = &ctx.player().getDimensionBlockSource();
    s.pos    = BlockPos{g.x, g.y + kSkyOffset, g.z};
    co_return s;
}

int countChorus(BlockSource& r, BlockPos const& c, int radius) {
    int n = 0;
    for (int x = -radius; x <= radius; ++x)
        for (int z = -radius; z <= radius; ++z)
            if (blocks().isChorus(r.getBlock(BlockPos{c.x + x, c.y, c.z + z}))) ++n;
    return n;
}

void clearChorus(BlockSource& r, BlockPos const& c, int radius) {
    for (int x = -radius; x <= radius; ++x)
        for (int z = -radius; z <= radius; ++z) {
            BlockPos p{c.x + x, c.y, c.z + z};
            if (blocks().isChorus(r.getBlock(p))) lp::game::clearBlock(r, p);
        }
}

} // namespace

LP_REGISTER_SELFTEST("worldbeyond.config", [](Context& ctx) {
    WorldBeyondConfig d;
    ctx.check(d.enableNetherObsidianSpikes && d.enableNoMoreLavaPockets && d.enableChorusVegetation && d.enableSpiralSpires, "modules on by default");
    ctx.check(d.chancePerChunk == 0.1 && d.bigSpikeChance == 0.03 && d.triesPerChunk == 4 && d.bigSpikeSpawners, "spike defaults");
    ctx.check(d.chorusVegetationRarity == 150 && d.chorusVegetationRadius == 7 && d.chunkAttempts == 120, "chorus generation defaults");
    ctx.check(d.highlandsChance == 1.0 && d.midlandsChance == 0.2 && d.otherEndBiomesChance == 0, "chorus biome chances");
    ctx.check(d.passiveTeleportChance == 0.2 && d.endermiteSpawnChance == 0.01 && d.teleportDuplicationChance == 0.01, "chorus teleport defaults");
    ctx.check(d.spiralSpiresRarity == 200 && d.spiralSpiresRadius == 15 && d.myaliteConduitDistance == 24, "spire defaults");
    ctx.check(d.enableMyaliteViaducts && d.renewableMyalite, "viaducts and renewable myalite on");
    ctx.check(d.netherObsidianSpikesDimensions.canSpawnHere(1) && !d.netherObsidianSpikesDimensions.canSpawnHere(0), "spikes: nether only");
    ctx.check(d.spiralSpiresDimensions.canSpawnHere(2) && !d.spiralSpiresDimensions.canSpawnHere(1), "spires: end only");
    ctx.check(d.biomes.canSpawn("minecraft:end_highlands", 2) && !d.biomes.canSpawn("minecraft:end_midlands", 2), "spires: highlands only");
    ctx.check(lp::config::get<WorldBeyondConfig>().version >= 1, "config section loaded");
});

LP_REGISTER_SELFTEST("worldbeyond.random", [](Context& ctx) {
    JavaRandom a(42);
    ctx.check(a.nextInt() == -1170105035, "Random(42).nextInt() as in Java");
    JavaRandom b(42);
    ctx.check(b.nextInt(10) == 0, "Random(42).nextInt(10) as in Java");
    JavaRandom c(42);
    ctx.check(std::abs(c.nextDouble() - 0.7275636800328681) < 1e-15, "Random(42).nextDouble() as in Java");
    JavaRandom d(42), e(42);
    ctx.check(d.nextLong() == e.nextLong(), "same seed, same numbers");
    ctx.check(endBiomeFromHeight(10, 10, 80.f) == "minecraft:the_end", "main island within 64 chunks");
    ctx.check(endBiomeFromHeight(100, 0, 60.f) == "minecraft:end_highlands", "high islands are highlands");
    ctx.check(endBiomeFromHeight(100, 0, 0.f) == "minecraft:end_midlands", "island edges are midlands");
    ctx.check(endBiomeFromHeight(100, 0, -10.f) == "minecraft:end_barrens", "barrens");
    ctx.check(endBiomeFromHeight(100, 0, -100.f) == "minecraft:small_end_islands", "void: small islands");
});

LP_REGISTER_SELFTEST("worldbeyond.myalite_color", [](Context& ctx) {
    std::set<int> seen;
    for (int x = 0; x < 200; x += 3) {
        int s = myaliteHueStep(x, 64, x / 2);
        ctx.check(s >= 0 && s < kHueSteps, "hue step in range");
        seen.insert(s);
    }
    ctx.check(seen.size() >= 4, "the colour changes over space (" + std::to_string(seen.size()) + " steps seen)");
    ctx.check(myaliteHueStep(5, 6, 7) == myaliteHueStep(5, 6, 7), "deterministic");
    int origin = myaliteHueStep(0, 0, 0);
    ctx.info("hue step at 0,0,0: " + std::to_string(origin) + " (hue " + std::to_string(myaliteHue(0, 0, 0)) + ")");
    ctx.check(origin == kItemHueStep, "the item's colour is the colour of 0,0,0");
    ctx.require(blocks().ready(), "worldbeyond blocks registered (behaviour pack)");
    for (int i = 0; i < kHueSteps; ++i) ctx.check(blocks().crystal[i] != nullptr, "crystal state " + std::to_string(i));
});

LP_REGISTER_SELFTEST("worldbeyond.spike", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2900);
    auto& r = *s.region;
    Scratch    sc(r);
    BlockPos   lava{s.pos.x, s.pos.y + 12, s.pos.z};
    sc.set(lava, "minecraft:lava");
    GenRegion g = GenRegion::unclipped(r);
    g.record(&sc.written);
    auto cfg            = config();
    cfg.bigSpikeChance  = 0;
    JavaRandom rand(2900);
    ctx.require(placeSpikeAt(g, lava, rand, cfg), "a spike is placed on open lava");
    ctx.check(idAt(r, lava) == "minecraft:obsidian", "the lava block became obsidian");
    ctx.check(idAt(r, BlockPos{lava.x + 1, lava.y - 10, lava.z + 1}) == "minecraft:obsidian", "the base reaches 10 blocks down");
    int top = lava.y;
    while (idAt(r, BlockPos{lava.x, top + 1, lava.z}) == "minecraft:obsidian") ++top;
    int h = top - lava.y + 1;
    ctx.check(h >= 7 && h <= 16, "spike height in Quark's range (" + std::to_string(h) + ")");
    ctx.check(idAt(r, BlockPos{lava.x, top, lava.z + 1}) == "minecraft:air", "the tip is one block wide");
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.spike_blocked", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2905);
    auto& r = *s.region;
    Scratch  sc(r);
    BlockPos lava{s.pos.x, s.pos.y + 12, s.pos.z};
    sc.set(lava, "minecraft:lava");
    sc.set(BlockPos{lava.x + 2, lava.y + 3, lava.z - 2}, "minecraft:netherrack");
    GenRegion g = GenRegion::unclipped(r);
    g.record(&sc.written);
    JavaRandom rand(2905);
    ctx.check(!placeSpikeAt(g, lava, rand, config()), "a block in the 5x5 column stops the spike");
    ctx.check(idAt(r, BlockPos{lava.x, lava.y + 1, lava.z}) == "minecraft:air", "nothing was placed");
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.spike_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2910);
    auto& r = *s.region;
    Scratch  sc(r);
    BlockPos lava{s.pos.x, s.pos.y + 5, s.pos.z};
    sc.set(lava, "minecraft:lava");
    GenRegion g = GenRegion::unclipped(r);
    g.record(&sc.written);
    auto cfg           = config();
    cfg.bigSpikeChance = 1;
    JavaRandom rand(2910);
    ctx.require(placeSpikeAt(g, lava, rand, cfg), "a big spike is placed");
    int spawner = -1, chest = -1, cap = -1;
    for (int y = lava.y; y < lava.y + 50; ++y) {
        auto id = idAt(r, BlockPos{lava.x, y, lava.z});
        if (id == "minecraft:mob_spawner") spawner = y;
        if (id == "minecraft:chest") chest = y;
        if (id == "minecraft:glowstone" || id == kBlazeLantern) cap = y;
    }
    ctx.check(spawner > 0 && chest == spawner - 1 && cap == spawner + 1, "glowstone cap, spawner below it, loot chest below that");
    ctx.check(spawner - lava.y >= 20, "big spikes are tall (" + std::to_string(spawner - lava.y) + ")");
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_teleport", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2920);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 4);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    ctx.require(blocks().ready(), "blocks registered");
    sc.set(at, *blocks().weeds);
    auto np = chorusTeleport(r, at, *blocks().weeds, 4, 1.0);
    ctx.require(np.has_value(), "the plant found a new spot");
    ctx.check(idAt(r, at) == "minecraft:air", "growth chance 1: the original is gone");
    ctx.check(idAt(r, *np) == kChorusWeeds, "the plant is at the new spot");
    ctx.check(std::abs(np->x - at.x) <= 2 && std::abs(np->z - at.z) <= 2 && np->y == at.y, "within range 4 (offsets -2..2)");
    sc.written.push_back(*np);
    auto dup = chorusTeleport(r, *np, *blocks().weeds, 4, 0.0);
    ctx.check(dup.has_value() && idAt(r, *np) == kChorusWeeds, "growth chance 0 copies the plant");
    if (dup) sc.written.push_back(*dup);
    clearChorus(r, BlockPos{s.pos.x, s.pos.y + 1, s.pos.z}, 4);
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_survive", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2925);
    auto& r = *s.region;
    Scratch sc(r);
    sc.set(s.pos, "minecraft:end_stone");
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    sc.set(at, kChorusTwist);
    co_await ctx.ticks(2);
    ctx.check(idAt(r, at) == kChorusTwist, "the plant stays on end stone");
    lp::game::setBlock(r, s.pos, *lp::game::blockWith("minecraft:stone"));
    co_await ctx.ticks(4);
    ctx.check(idAt(r, at) == "minecraft:air", "without end stone below it pops off");
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2930);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 4);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    sc.set(at, kChorusWeeds);
    co_await ctx.ticks(2);
    // a wall keeps the water from spreading over the platform
    sc.set(BlockPos{at.x + 1, at.y, at.z}, "minecraft:water");
    co_await ctx.ticks(3);
    ctx.check(idAt(r, at) != kChorusWeeds, "the plant ran away from the water");
    ctx.check(countChorus(r, at, 4) >= 1, "it moved somewhere on the platform");
    lp::game::clearBlock(r, BlockPos{at.x + 1, at.y, at.z});
    co_await ctx.ticks(1);
    clearChorus(r, at, 4);
    // the water may have spread: clear the layer above the platform
    for (int x = -5; x <= 5; ++x)
        for (int z = -5; z <= 5; ++z) {
            BlockPos p{at.x + x, at.y, at.z + z};
            if (!r.getBlock(p).isAir()) lp::game::clearBlock(r, p);
        }
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_trample", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2935);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 4);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    sc.set(at, kChorusWeeds);
    auto& p = ctx.player();
    p.teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, p.getDimensionId());
    for (int i = 0; i < 20 && idAt(r, at) == kChorusWeeds; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(r, at) != kChorusWeeds, "walking into chorus weeds teleports them");
    // chorus twist does not react
    p.teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y + 3), at.z + 0.5f}, p.getDimensionId());
    clearChorus(r, at, 4);
    co_await ctx.ticks(2);
    sc.set(at, kChorusTwist);
    p.teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + 0.5f}, p.getDimensionId());
    co_await ctx.ticks(10);
    ctx.check(idAt(r, at) == kChorusTwist, "chorus twist is not trampled away");
    clearChorus(r, at, 4);
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_bonemeal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2940);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 6);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    sc.set(at, kChorusWeeds);
    auto& p = static_cast<SimulatedPlayer&>(ctx.player());
    p.teleport(Vec3{at.x + 2.5f, static_cast<float>(at.y), at.z + 0.5f}, p.getDimensionId());
    co_await ctx.ticks(10);
    auto meal = lp::game::makeItem("minecraft:bone_meal", 4);
    p.simulateUseItemOnBlock(meal, at, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 0.5f, 0.5f});
    for (int i = 0; i < 10 && countChorus(r, at, 6) < 2; ++i) co_await ctx.ticks(1);
    int n = countChorus(r, at, 6);
    ctx.check(n >= 2, "bone meal multiplies the plant (" + std::to_string(n) + " plants)");
    ctx.check(idAt(r, at) != kChorusWeeds || n >= 3, "the plant itself moved too");
    clearChorus(r, at, 6);
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2945);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 4);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    ConfigGuard guard;
    guard.edit().passiveTeleportChance     = 1.0;
    guard.edit().teleportDuplicationChance = 0.0;
    guard.apply();
    sc.set(at, kChorusWeeds);
    co_await ctx.ticks(1);
    // the placement scheduled a tick within 1..2730 ticks; force one now
    r.addToTickingQueue(at, *blocks().weeds, 1);
    for (int i = 0; i < 10 && idAt(r, at) == kChorusWeeds; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(r, at) != kChorusWeeds, "a scheduled tick teleports the plant (passiveTeleportChance 1)");
    ctx.check(countChorus(r, at, 4) == 1, "it moved, it did not multiply");
    clearChorus(r, at, 4);
});

LP_REGISTER_SELFTEST("worldbeyond.crystal_hue", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2950);
    auto& r = *s.region;
    Scratch sc(r);
    std::set<int> hues;
    for (int i = 0; i < 6; ++i) {
        BlockPos p{s.pos.x, s.pos.y + i * 7, s.pos.z};
        sc.set(p, kMyaliteCrystal); // default state: the item colour
    }
    co_await ctx.ticks(3);
    for (int i = 0; i < 6; ++i) {
        BlockPos p{s.pos.x, s.pos.y + i * 7, s.pos.z};
        auto     h = lp::game::blockstates::getInt(r.getBlock(p), kHueState);
        ctx.check(h && *h == myaliteHueStep(p.x, p.y, p.z), "a placed crystal takes the colour of its position");
        if (h) hues.insert(*h);
    }
    ctx.info("crystal colours seen: " + std::to_string(hues.size()));
});

LP_REGISTER_SELFTEST("worldbeyond.viaduct", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2955);
    auto& r = *s.region;
    Scratch sc(r);
    // a floor to stand on at both ends, a line of 6 crystals at head height
    BlockPos base{s.pos.x - 8, s.pos.y, s.pos.z};
    for (int x = -2; x <= 10; ++x) sc.set(BlockPos{base.x + x, base.y, base.z}, "minecraft:end_stone");
    for (int x = 0; x < 6; ++x) sc.set(BlockPos{base.x + x, base.y + 1, base.z}, kMyaliteCrystal);
    auto& p = ctx.player();
    // teleport (feet position) next to the west end of the line: Quark sends us past the east end
    p.teleportTo(Vec3{base.x - 0.5f, base.y + 1.0f, base.z + 0.5f}, true, 3, 1, false); // cause 3: command
    co_await ctx.ticks(3);
    auto     feet = p.getFeetPos();
    BlockPos fb   = lp::game::blockPosOf(feet);
    ctx.check(fb.x == base.x + 6 && fb.z == base.z, "the viaduct moved the player to the far end (x " + std::to_string(fb.x - base.x) + ")");
    ctx.check(fb.y == base.y + 1, "at the height of the line (y " + std::to_string(fb.y - base.y) + ")");
    p.teleport(Vec3{base.x - 1.5f, base.y + 1.0f, base.z + 0.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
});

namespace {

void runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("lepton-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

/// A floor and a line of 6 crystals at feet height going east from `base` (x .. x+5).
void viaductLine(Scratch& sc, BlockPos const& base) {
    for (int x = -3; x <= 10; ++x)
        for (int z = -1; z <= 1; ++z) sc.set(BlockPos{base.x + x, base.y, base.z + z}, "minecraft:end_stone");
    for (int x = 0; x < 6; ++x) sc.set(BlockPos{base.x + x, base.y + 1, base.z}, kMyaliteCrystal);
}

} // namespace

LP_REGISTER_SELFTEST("worldbeyond.viaduct_command", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2980);
    Scratch sc(*s.region);
    BlockPos base{s.pos.x - 8, s.pos.y, s.pos.z};
    viaductLine(sc, base);
    auto& p = ctx.player();
    // a real /tp command next to the west end of the line
    runCommand(
        "tp @a[name=\"" + p.getRealName() + "\"] " + std::to_string(base.x - 1) + " " + std::to_string(base.y + 1) + " "
        + std::to_string(base.z)
    );
    co_await ctx.ticks(3);
    BlockPos fb = lp::game::blockPosOf(p.getFeetPos());
    ctx.check(fb.x == base.x + 6 && fb.z == base.z, "/tp next to a viaduct ends at its far end (x " + std::to_string(fb.x - base.x) + ")");
});

LP_REGISTER_SELFTEST("worldbeyond.viaduct_pearl", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2985);
    Scratch sc(*s.region);
    BlockPos base{s.pos.x - 8, s.pos.y, s.pos.z};
    auto& p = ctx.spawnPlayer();
    p.setPlayerGameType(GameType::Survival);
    // stand there first (a teleport next to the line would take the viaduct), then build the line
    p.teleport(Vec3{base.x - 0.5f, base.y + 1.0f, base.z + 0.5f}, p.getDimensionId());
    viaductLine(sc, base);
    co_await ctx.ticks(10);
    // throw an ender pearl straight down: it lands next to the west end of the line
    p.simulateLookAt(Vec3{base.x - 0.5f, static_cast<float>(base.y), base.z + 0.5f}, sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    auto pearl = lp::game::makeItem("minecraft:ender_pearl", 1);
    ctx.require(p.simulateUseItem(pearl), "the pearl was thrown");
    BlockPos fb;
    for (int i = 0; i < 40; ++i) {
        co_await ctx.ticks(1);
        fb = lp::game::blockPosOf(p.getFeetPos());
        if (fb.x == base.x + 6) break;
    }
    ctx.check(fb.x == base.x + 6 && fb.z == base.z, "an ender pearl next to a viaduct ends at its far end (x " + std::to_string(fb.x - base.x) + ")");
    p.setPlayerGameType(GameType::Creative);
});

LP_REGISTER_SELFTEST("worldbeyond.chorus_dispenser", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2990);
    auto& r = *s.region;
    Scratch sc(r);
    sc.platform(s.pos, 6);
    BlockPos at{s.pos.x, s.pos.y + 1, s.pos.z};
    BlockPos disp{at.x - 1, at.y, at.z};
    sc.set(at, kChorusWeeds);
    auto const* dispenser = lp::game::blockstates::of("minecraft:dispenser", {{"facing_direction", 5}}); // east, at the plant
    ctx.require(dispenser != nullptr, "dispenser state");
    sc.set(disp, *dispenser);
    co_await ctx.ticks(1);
    auto container = [&]() -> Container* {
        auto* be = r.getBlockEntity(disp);
        if (!be || be->getType() != BlockActorType::Dispenser) return nullptr;
        return &static_cast<Container&>(static_cast<DispenserBlockActor&>(*be));
    };
    ctx.require(container() != nullptr, "the dispenser has its container");
    container()->setItem(0, lp::game::makeItem("minecraft:bone_meal", 3));
    co_await ctx.ticks(1);
    BlockPos power{disp.x - 1, disp.y, disp.z};
    sc.set(power, "minecraft:redstone_block");
    int n = 0;
    for (int i = 0; i < 20 && (n = countChorus(r, at, 6)) < 2; ++i) co_await ctx.ticks(1);
    ctx.check(n >= 2, "a dispenser with bone meal multiplies the plant (" + std::to_string(n) + " plants)");
    int left = container() ? container()->getItem(0).mCount : -1;
    ctx.check(left == 2, "the dispenser used one bone meal (" + std::to_string(left) + " left)");
    if (auto* c = container()) c->removeItem(0, c->getItem(0).mCount);
    clearChorus(r, at, 6);
});

LP_REGISTER_SELFTEST("worldbeyond.viaduct_loop",[](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2960);
    auto& r = *s.region;
    Scratch sc(r);
    BlockPos c{s.pos.x, s.pos.y, s.pos.z};
    // a 3x3 ring of crystals (a loop): no redirect
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            if (x != 0 || z != 0) sc.set(BlockPos{c.x + x, c.y, c.z + z}, kMyaliteCrystal);
    BlockPos start{c.x - 2, c.y, c.z - 1};
    int      redirected = 0;
    for (int i = 0; i < 8; ++i) {
        if (viaductTarget(r, start, ctx.player())) ++redirected;
    }
    ctx.check(redirected == 0, "loops do not work (" + std::to_string(redirected) + " of 8 tries redirected)");
    // a fork works: a T of crystals always leads to one of the ends
    sc.clear();
    BlockPos f{c.x, c.y, c.z};
    for (int x = 0; x < 5; ++x) sc.set(BlockPos{f.x + x, f.y, f.z}, kMyaliteCrystal);
    for (int z = 1; z < 4; ++z) sc.set(BlockPos{f.x + 2, f.y, f.z + z}, kMyaliteCrystal);
    auto t = viaductTarget(r, BlockPos{f.x - 1, f.y, f.z}, ctx.player());
    ctx.check(t.has_value(), "forks work");
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.spire", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto s = co_await sky(ctx, 2970);
    auto& r = *s.region;
    Scratch  sc(r);
    // the trunk (up to 69 blocks) and the climbing spiral (up to 22 more) stay below the build limit
    BlockPos pad{s.pos.x, std::min(s.pos.y + 40, static_cast<int>(r.getMaxHeight()) - 95), s.pos.z};
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) sc.set(BlockPos{pad.x + x, pad.y, pad.z + z}, "minecraft:end_stone");
    GenRegion g = GenRegion::unclipped(r);
    g.record(&sc.written);
    JavaRandom rand(2970);
    ctx.require(makeSpire(g, rand, pad), "a spire is made on end stone");
    ctx.check(idAt(r, BlockPos{pad.x, pad.y + 10, pad.z}) == kDuskyMyalite, "the trunk is dusky myalite");
    ctx.check(idAt(r, BlockPos{pad.x + 4, pad.y + 1, pad.z}) == kDuskyMyalite || idAt(r, BlockPos{pad.x + 4, pad.y + 1, pad.z}) == kMyalite, "the base is wide");
    int crystals = 0;
    for (auto const& p : sc.written)
        if (blocks().isCrystal(r.getBlock(p))) ++crystals;
    ctx.check(crystals >= 100, "a spiral of myalite crystals on top (" + std::to_string(crystals) + ")");
    JavaRandom rand2(2970);
    ctx.check(!makeSpire(g, rand2, BlockPos{pad.x + 1, pad.y, pad.z}), "no spire where a spire stands (blocked)");
    co_return;
});

LP_REGISTER_SELFTEST("worldbeyond.renewable", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.check(isGrowingCorundum("lepton:violet_corundum") && isGrowingCorundum("lepton:black_corundum"), "corundum colours");
    ctx.check(!isGrowingCorundum("lepton:waxed_violet_corundum") && !isGrowingCorundum("lepton:violet_corundum_cluster"), "waxed corundum and clusters do not grow");
    auto const* corundum = lp::game::blockWith("lepton:violet_corundum");
    if (!corundum) {
        ctx.info("lepton:violet_corundum (U12) is not registered: live growth check skipped");
        co_return;
    }
    auto s = co_await sky(ctx, 2975);
    auto& r = *s.region;
    Scratch sc(r);
    BlockPos c{s.pos.x, s.pos.y, s.pos.z};
    sc.set(c, kMyaliteCrystal);
    sc.set(BlockPos{c.x, c.y + 1, c.z}, *corundum);
    co_await ctx.ticks(2);
    sc.set(BlockPos{c.x, c.y + 2, c.z}, *corundum); // what a growing corundum does
    co_await ctx.ticks(3);
    ctx.check(blocks().isCrystal(r.getBlock(BlockPos{c.x, c.y + 1, c.z})), "corundum growing on myalite crystal turns into crystal");
});

// ---- generation in the real dimensions ----------------------------------------------------------

namespace {

ll::coro::CoroTask<bool> moveTo(Context& ctx, int dim, Vec3 pos) {
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Creative); // no fall / void / suffocation deaths
    p.teleport(pos, DimensionType(dim));
    for (int i = 0; i < 400; ++i) {
        co_await ctx.ticks(1);
        if (static_cast<int>(p.getDimensionId()) != dim) continue;
        auto& r = p.getDimensionBlockSource();
        if (r.hasChunksAt(lp::game::blockPosOf(pos), 3, false)) co_return true;
    }
    co_return false;
}

struct Found {
    int count = 0;
    BlockPos at;
};

template <class Pred>
Found scan(BlockSource& r, BlockPos const& c, int radius, int y0, int y1, Pred pred) {
    Found f;
    for (int x = -radius; x <= radius; x += 1)
        for (int z = -radius; z <= radius; z += 1) {
            BlockPos col{c.x + x, y0, c.z + z};
            if (!r.hasChunksAt(col, 0, false)) continue;
            for (int y = y0; y <= y1; ++y) {
                BlockPos p{col.x, y, col.z};
                if (pred(r.getBlock(p))) {
                    if (!f.count) f.at = p;
                    ++f.count;
                }
            }
        }
    return f;
}

} // namespace

LP_REGISTER_SELFTEST("worldbeyond.end_gen", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    guard.edit().spiralSpiresRarity     = 1;
    guard.edit().chorusVegetationRarity = 1;
    guard.apply();
    ctx.require(co_await moveTo(ctx, 2, Vec3{100.f, 70.f, 0.f}), "reached the End");
    auto& p     = ctx.player();
    auto  level = ll::service::getLevel();
    auto  dim   = level->getDimension(2).lock();
    ctx.require(dim && static_cast<WorldGenerator*>(dim->mWorldGenerator), "the End has a generator");
    auto const& endGen = static_cast<TheEndGenerator const&>(*static_cast<WorldGenerator*>(dim->mWorldGenerator));
    // find a highlands chunk of the outer islands that no one has generated yet
    int hx = 0, hz = 0;
    bool found = false;
    for (int i = 0; i < 4000 && !found; ++i) {
        int cx = 140 + (i % 60) * 3, cz = 140 + (i / 60) * 3;
        if (endBiomeFromHeight(cx, cz, endGen.getIslandHeightValue(cx, cz, 1, 1)) == "minecraft:end_highlands") {
            hx = cx, hz = cz, found = true;
        }
    }
    ctx.require(found, "a highlands chunk exists");
    ctx.info("End highlands chunk " + std::to_string(hx) + " " + std::to_string(hz));
    auto before = genStats();
    Vec3 target{hx * 16.f + 8.f, 120.f, hz * 16.f + 8.f};
    ctx.require(co_await moveTo(ctx, 2, target), "the outer island is loaded");
    Found weeds, crystal, dusky;
    for (int t = 0; t < 30 && !(weeds.count && crystal.count && dusky.count); ++t) {
        co_await ctx.ticks(40);
        auto& r  = p.getDimensionBlockSource();
        BlockPos c = lp::game::blockPosOf(target);
        weeds    = scan(r, c, 24, 30, 120, [](Block const& b) { return blocks().isChorus(b); });
        crystal  = scan(r, c, 24, 30, 200, [](Block const& b) { return blocks().isCrystal(b); });
        dusky    = scan(r, c, 24, 30, 140, [](Block const& b) { return b.getTypeName() == kDuskyMyalite; });
    }
    auto after = genStats();
    ctx.info(
        "End decoration: chunks +" + std::to_string(after.chunks - before.chunks) + ", spires +" + std::to_string(after.spires - before.spires)
        + ", chorus +" + std::to_string(after.chorus - before.chorus)
    );
    ctx.check(after.chunks > before.chunks, "End chunks were decorated by Lepton");
    ctx.check(dusky.count > 0, "spiral spires of dusky myalite generated (" + std::to_string(dusky.count) + ")");
    ctx.check(crystal.count > 0, "myalite crystals generated (" + std::to_string(crystal.count) + ")");
    ctx.check(weeds.count > 0, "chorus vegetation generated (" + std::to_string(weeds.count) + ")");
    if (crystal.count) {
        auto h = lp::game::blockstates::getInt(p.getDimensionBlockSource().getBlock(crystal.at), kHueState);
        ctx.check(h && *h == myaliteHueStep(crystal.at.x, crystal.at.y, crystal.at.z), "generated crystals have their position's colour");
    }
    lp::log().info("worldbeyond: End generation checked: {} weeds, {} crystals, {} dusky myalite", weeds.count, crystal.count, dusky.count);
});

LP_REGISTER_SELFTEST("worldbeyond.nether_gen", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    guard.edit().chancePerChunk = 1.0;
    guard.apply();
    auto before = genStats();
    Vec3 target{2900.f, 100.f, 2900.f};
    ctx.require(co_await moveTo(ctx, 1, target), "reached the Nether");
    auto& p = ctx.player();
    Found spikes;
    for (int t = 0; t < 30 && !spikes.count; ++t) {
        co_await ctx.ticks(40);
        auto& r = p.getDimensionBlockSource();
        // obsidian standing in the lava sea (lava level 31) up into the open air
        spikes = scan(r, lp::game::blockPosOf(target), 32, 33, 40, [](Block const& b) { return b.getTypeName() == "minecraft:obsidian"; });
    }
    auto after = genStats();
    ctx.info(
        "Nether decoration: chunks +" + std::to_string(after.chunks - before.chunks) + ", spikes +" + std::to_string(after.spikes - before.spikes)
        + ", lava springs removed +" + std::to_string(after.springsRemoved - before.springsRemoved)
    );
    ctx.check(after.spikes > before.spikes, "obsidian spikes were generated");
    ctx.check(spikes.count > 0, "obsidian above the lava sea (" + std::to_string(spikes.count) + ")");
    ctx.check(after.springsRemoved > before.springsRemoved, "hidden lava springs were not placed (NoMoreLavaPockets)");
    lp::log().info("worldbeyond: Nether generation checked: {} spike blocks", spikes.count);
});
