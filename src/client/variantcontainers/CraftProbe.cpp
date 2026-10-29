// e2e aid (client build): while armed by the `variantcontainers.craft_arm` selftest (local
// world), the screen positions of the crafting table's slots are logged once per opening, so the
// client scenario can craft chests and furnaces with real clicks.
#ifdef LP_CLIENT

#include <atomic>
#include <chrono>

#include "client/ui/Ui.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace lp::client::variantcontainers {
namespace {

std::atomic<long long> gArmedUntil{0};

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace
} // namespace lp::client::variantcontainers

LP_REGISTER_SELFTEST("variantcontainers.craft_arm", [](lp::selftest::Context&) {
    lp::client::variantcontainers::gArmedUntil = lp::client::variantcontainers::nowMs() + 10 * 60 * 1000;
});

LP_REGISTER_SCREEN_OVERLAY("*", [](lp::client::ui::Overlay&, lp::client::ui::ScreenInfo const& s) {
    namespace vcc = lp::client::variantcontainers;
    static bool logged = false;
    if (s.frames <= 1) logged = false;
    if (logged || !s.settled || vcc::nowMs() > vcc::gArmedUntil.load()) return;
    if (s.name.find("crafting") == std::string::npos && s.qualifiedName.find("crafting") == std::string::npos) return;
    logged = true;
    for (auto const& slot : s.slots) {
        if (slot.collection == "inventory_items") continue;
        lp::log().info("variantcontainers: slot {}:{} at {:.4f},{:.4f}", slot.collection, slot.index,
            (slot.rect.x + slot.rect.w / 2) / s.size.x, (slot.rect.y + slot.rect.h / 2) / s.size.y);
    }
    lp::log().info("variantcontainers: crafting screen {} logged", s.name);
});

#endif // LP_CLIENT
