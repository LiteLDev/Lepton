#include "feature/misctweaks/Config.h"
#include "feature/misctweaks/Internal.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("misctweaks", lp::feature::misctweaks::MiscTweaksConfig);

namespace lp::feature::misctweaks {

MiscTweaksConfig const& cfg() { return config::get<MiscTweaksConfig>(); }

namespace {

class MiscTweaksModule {
public:
    void install() {
        installElytra();
        installNoteBlocks();
        installRecipes();
        installArmorStands();
        installNerfs();
        installSpawners();
        installVillagers();
        installClimate();
        log().debug("misctweaks: installed");
    }
    void uninstall() {
        uninstallClimate();
        uninstallVillagers();
        uninstallSpawners();
        uninstallNerfs();
        uninstallArmorStands();
        uninstallRecipes();
        uninstallNoteBlocks();
        uninstallElytra();
    }
};

} // namespace

} // namespace lp::feature::misctweaks

LP_REGISTER_MODULE(lp::feature::misctweaks::MiscTweaksModule);
