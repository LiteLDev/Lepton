// GrabChickens, client render part (Quark's LivingEntityRendererMixin + ChickenModel leg tweak):
// a chicken carried by a player is drawn on top of the player's head, pushed down by
// `0.25 - chickenOffset` blocks, with its legs hidden. Bedrock seats the chicken on the player's
// shoulder seat (the vanilla player's only rideable seats); only its drawing is moved, as in Quark.
#ifdef LP_CLIENT

#include <algorithm>
#include <atomic>
#include <string_view>

#include "ll/api/memory/Hook.h"

#include "mc/client/model/geom/ModelPart.h"
#include "mc/client/model/models/DataDrivenGeometry.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/RenderParams.h"
#include "mc/world/phys/AABB.h"

#include "feature/farming/Farming.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::farming {

namespace {

namespace farm = lp::feature::farming;

/// Java's player passenger attachment point: the top of the 1.8 block high player box.
constexpr float kPlayerHeight = 1.8f;

/// The player carrying `actor` if it is a chicken on a player and GrabChickens is on.
Actor* carrier(Actor const& actor) {
    if (actor.getEntityTypeId() != ActorType::Chicken) return nullptr;
    if (!config::get<farm::FarmingConfig>().enableGrabChickens) return nullptr;
    Actor* vehicle = actor.getVehicle();
    return vehicle && vehicle->isPlayer() ? vehicle : nullptr;
}

/// `chickenOffset` clamped to Quark's -1..1 (read only: this runs on the render thread).
double offset() { return std::clamp(config::get<farm::FarmingConfig>().chickenOffset, -1.0, 1.0); }

std::atomic_bool gLoggedMove{false};
std::atomic_bool gLoggedLegs{false};

using RenderFn = void (ActorRenderDispatcher::*)(
    BaseActorRenderContext&,
    Actor&,
    Vec3 const&,
    Vec3 const&,
    Vec2 const&,
    bool
);

LL_TYPE_INSTANCE_HOOK(
    CarriedChickenRenderHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    static_cast<RenderFn>(&ActorRenderDispatcher::render),
    void,
    BaseActorRenderContext& context,
    Actor&                  entity,
    Vec3 const&             cameraTargetPos,
    Vec3 const&             pos,
    Vec2 const&             rot,
    bool                    ignoreLighting
) {
    if (Actor* player = carrier(entity)) {
        // the render position is relative to the camera: shift it by the world-space difference
        // between the head top (minus Quark's push) and where the chicken actually is
        auto const& box   = player->getAABB();
        Vec3        feet  = entity.getPosition();
        Vec3        top{
            (box.min.x + box.max.x) * 0.5f,
            box.min.y + kPlayerHeight - static_cast<float>(0.25 - offset()),
            (box.min.z + box.max.z) * 0.5f
        };
        Vec3 shifted{pos.x + top.x - feet.x, pos.y + top.y - feet.y, pos.z + top.z - feet.z};
        if (!gLoggedMove.exchange(true)) log().info("farming: carried chicken drawn on the player's head");
        return origin(context, entity, cameraTargetPos, shifted, rot, ignoreLighting);
    }
    origin(context, entity, cameraTargetPos, pos, rot, ignoreLighting);
}

bool isLeg(ModelPart const& part) {
    std::string_view name = part.mName->getString();
    return name == "leg0" || name == "leg1";
}

LL_TYPE_INSTANCE_HOOK(
    CarriedChickenLegsHook,
    ll::memory::HookPriority::Normal,
    DataDrivenGeometry,
    &DataDrivenGeometry::copyTransformMatricesToModelParts,
    void,
    RenderParams& params
) {
    origin(params);
    // the geometry is shared by every chicken: set the leg visibility for each one drawn
    std::string_view geo = mGeoName->getString();
    if (!geo.starts_with("geometry.chicken")) return;
    Actor const* actor  = params.mActor;
    bool         hidden = actor && carrier(*actor);
    for (auto& part : *mModelParts) {
        if (isLeg(part)) part.mVisible = !hidden;
    }
    if (hidden && !gLoggedLegs.exchange(true)) log().info("farming: carried chicken legs hidden");
}

class ChickenRenderModule {
public:
    void install() {
        CarriedChickenRenderHook::hook();
        CarriedChickenLegsHook::hook();
    }
    void uninstall() {
        CarriedChickenRenderHook::unhook();
        CarriedChickenLegsHook::unhook();
    }
};

} // namespace

} // namespace lp::client::farming

LP_REGISTER_MODULE(lp::client::farming::ChickenRenderModule);

#endif // LP_CLIENT
