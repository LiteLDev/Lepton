// Makes the mod's packs load everywhere.
//
// - Behavior pack: LeviLamina adds every mod's `behavior_packs/` to the behavior stack of every
//   world (BDS and client local worlds). Nothing to do.
// - Resource pack, client: LeviLamina adds every mod's `resource_packs/` to the client's global
//   resource stack. Nothing to do.
// - Resource pack, BDS: LeviLamina registers the pack in the repository but doesn't put it on
//   the level stack that is sent to joining players. At load (before the world opens) we add
//   it to `worlds/<level-name>/world_resource_packs.json`, and (optionally) mark it required.
// - Names, BDS: the server builds some texts itself, with its own I18n, which only holds the
//   vanilla strings (e.g. the `/give` feedback "Gave <item name> * 1 to ..."). Item and block names
//   from the resource pack's `texts/<lang>.lang` are appended to it at install, so those messages show
//   "Ancient Tome" instead of `item.lepton:ancient_tome.name`. Clients translate everything else.

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/service/Bedrock.h"

#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"

#include "mod/Config.h"
#include "mod/ModConfig.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace lp::pack {

namespace fs = std::filesystem;

#ifdef LP_SERVER
namespace {
std::optional<nlohmann::json> readJson(fs::path const& p) {
    std::ifstream in(p);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false, true);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

std::string levelName() {
    std::ifstream in("server.properties");
    std::string   line;
    while (std::getline(in, line)) {
        if (line.starts_with("level-name=")) {
            auto v = line.substr(11);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            return v;
        }
    }
    return "Bedrock level";
}

// `key=value` lines of a Bedrock .lang file (`#` comment lines, `\t#` trailing comments).
std::unordered_map<std::string, std::string> readLang(fs::path const& p) {
    std::unordered_map<std::string, std::string> out;
    std::ifstream                                in(p);
    std::string                                  line;
    while (std::getline(in, line)) {
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        if (auto tab = line.find("\t#"); tab != std::string::npos) line.resize(tab);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out.emplace(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

fs::path rpDir() { return Mod::getInstance().getSelf().getModDir() / "resource_packs" / "Lepton_RP"; }

// Appends the pack's block / item / entity names to the server's I18n (see the file comment).
void registerServerNames() {
    auto lang = config::get<ModConfig>().language;
    auto file = rpDir() / "texts" / (lang + ".lang");
    if (!fs::exists(file)) file = rpDir() / "texts" / "en_US.lang";
    // The pack's texts are Lepton's own strings only (names, spawn eggs, subtitles, key names).
    auto names = readLang(file);
    if (names.empty()) {
        log().warn("no names found in {}", file.string());
        return;
    }
    // Straight into the loaded language tables: the server looks names up in its current language
    // (`I18n::appendAdditionalTranslations` only applies to languages loaded later).
    auto& i18n = getI18n();
    std::vector<std::shared_ptr<Localization const>> locales{i18n.getCurrentLanguage().get()};
    if (auto en = i18n.getLocaleFor("en_US"); en && en != locales.front()) locales.push_back(en);
    for (auto const& locale : locales) {
        auto& strings = const_cast<Localization&>(*locale).mStrings.get();
        for (auto const& [k, v] : names) strings.insert_or_assign(k, v);
    }
    log().info("registered {} names with the server ({})", names.size(), file.filename().string());
}
} // namespace
#endif

class PackDeployer {
public:
    void load() {
#ifdef LP_SERVER
        auto dir      = rpDir();
        auto manifest = readJson(dir / "manifest.json");
        if (!manifest) {
            log().warn("resource pack manifest not found in {}", dir.string());
            return;
        }
        auto const& header  = (*manifest)["header"];
        auto        uuid    = header.value("uuid", std::string{});
        auto        version = header.value("version", nlohmann::json::array({0, 0, 1}));
        auto        name    = levelName();
        auto        world   = fs::path("worlds") / fs::path(std::u8string(name.begin(), name.end()));
        std::error_code ec;
        fs::create_directories(world, ec);
        auto file  = world / "world_resource_packs.json";
        auto packs = readJson(file).value_or(nlohmann::json::array());
        if (!packs.is_array()) packs = nlohmann::json::array();
        bool found = false, changed = false;
        for (auto& p : packs) {
            if (p.value("pack_id", std::string{}) != uuid) continue;
            found = true;
            if (p["version"] != version) {
                p["version"] = version;
                changed      = true;
            }
        }
        if (!found) {
            packs.push_back({
                {"pack_id", uuid   },
                {"version", version},
            });
            changed = true;
        }
        if (changed) {
            std::ofstream(file) << packs.dump(2);
            log().info("registered the resource pack in {}", file.string());
        }
#endif
    }

    void install() {
#ifdef LP_SERVER
        registerServerNames();
        if (!config::get<ModConfig>().requireResourcePack) return;
        if (auto level = ll::service::getLevel()) level->getLevelData().mTexturePacksRequired = true;
#endif
    }
    void uninstall() {}
};

} // namespace lp::pack

LP_REGISTER_MODULE_ORDERED(lp::pack::PackDeployer, lp::ModuleOrder::Items);
