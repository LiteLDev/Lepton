#include "game/Entities.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

namespace lp::game::entities {

Actor* spawn(BlockSource& region, std::string_view type, Vec3 const& pos, Actor* summoner) {
    if (!region.hasChunksAt(BlockPos{pos}, 0, false)) return nullptr;
    ActorDefinitionIdentifier ident{std::string(type)};
    ActorUniqueID             id{};
    return CommandUtils::spawnEntityAt(region, pos, ident, id, summoner);
}

Actor* spawn(int dim, std::string_view type, Vec3 const& pos, Actor* summoner) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return spawn(d->getBlockSourceFromMainChunkSource(), type, pos, summoner);
}

int64_t idOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* find(int64_t uniqueId) {
    auto level = ll::service::getLevel();
    if (!level || uniqueId == 0) return nullptr;
    ActorUniqueID id{};
    id.rawID = uniqueId;
    return level->fetchEntity(id, false);
}

std::string typeOf(Actor const& actor) { return actor.getTypeName(); }

std::vector<Actor*> inBox(BlockSource& region, AABB const& box, std::string_view type) {
    auto list = region.getEntities(box, 0.0f);
    if (type.empty()) return list;
    std::erase_if(list, [&](Actor* a) { return !a || a->getTypeName() != type; });
    return list;
}

std::vector<Actor*> ofType(std::string_view type, int dim) {
    std::vector<Actor*> out;
    auto                level = ll::service::getLevel();
    if (!level) return out;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || a->getTypeName() != type) continue;
        if (dim >= 0 && static_cast<int>(a->getDimensionId()) != dim) continue;
        out.push_back(a);
    }
    return out;
}

bool triggerEvent(Actor& actor, std::string const& event) {
    VariantParameterList params{};
    params.mSelf = &actor;
    return actor.executeEvent(event, params);
}

void despawn(Actor& actor) { actor.despawn(); }

} // namespace lp::game::entities
