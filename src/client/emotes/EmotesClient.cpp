// U19 Emotes, client mod part (client build): Quark's emote buttons on the chat screen, the emote
// keybinds, the HUD icon above the crosshair, emote sounds and custom emotes
// (`config/quark_emotes/<name>.emote`, baked into the resource pack at game start).
// The animation itself is played by the server for every client (Emotes.cpp).

#ifdef LP_CLIENT

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/deps/audio/SoundPlayerInterface.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/MobHurtTimeComponent.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/emotes/Builtins.h"
#include "feature/emotes/EmoteScript.h"
#include "feature/emotes/Emotes.h"
#include "feature/emotes/EmotesConfig.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace lp::client::emotes {

using namespace lp::feature::emotes;
namespace ui = lp::client::ui;

namespace {

struct IconPaletteEntry {
    char key;
    int  rgb;
};
struct IconDef {
    std::string_view                 id;
    std::array<std::string_view, 16> rows;
};
#include "client/emotes/EmoteIcons.inc"

constexpr int kButtonWidth  = 25; // Quark: EMOTE_BUTTON_WIDTH
constexpr int kEmotesPerRow = 3;  // Quark: EMOTES_PER_ROW

EmotesConfig const& cfg() { return config::get<EmotesConfig>(); }

IconDef const* iconOf(std::string_view id) {
    for (auto const& i : kIcons)
        if (i.id == id) return &i;
    return nullptr;
}

/// Draws a 16x16 icon at (x, y), `scale` UI units per pixel.
void drawIcon(ui::DrawContext& dc, std::string_view id, float x, float y, float scale = 1, float alpha = 1) {
    auto const* icon = iconOf(id);
    if (!icon) icon = iconOf("custom");
    if (!icon) return;
    for (int row = 0; row < 16; ++row) {
        auto const& r = icon->rows[static_cast<size_t>(row)];
        for (size_t col = 0; col < r.size();) {
            char c = r[col];
            size_t end = col + 1;
            while (end < r.size() && r[end] == c) ++end;
            if (c != '.') {
                for (auto const& p : kIconPalette) {
                    if (p.key != c) continue;
                    dc.fill({x + static_cast<float>(col) * scale, y + static_cast<float>(row) * scale,
                             static_cast<float>(end - col) * scale, scale},
                            ui::Color::rgb(p.rgb, alpha));
                    break;
                }
            }
            col = end;
        }
    }
}

// ---- custom emotes ------------------------------------------------------------------------------------

struct CustomEmote {
    std::string                    file; ///< file name without ".emote"
    std::shared_ptr<script::Emote> emote;
};
std::map<std::string, CustomEmote> gCustom; ///< by file name

std::filesystem::path emotesDir() { return config::configDir() / "quark_emotes"; }

std::shared_ptr<script::Emote> readCustom(std::string const& file) {
    auto          path = emotesDir() / (file + ".emote");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log().warn("[Custom Emotes] Failed to load emote {}: {} not found", file, path.string());
        return nullptr;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    auto r = script::compile(ss.str());
    if (!r.ok) {
        log().error("[Custom Emotes] Error loading line {} of emote {}.emote", r.errorLine, file);
        log().error("[Custom Emotes] {}", r.error);
    }
    return std::make_shared<script::Emote>(std::move(r.emote));
}

std::filesystem::path customAnimationFile() {
    return lp::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "Lepton_RP" / "animations"
         / "lepton_custom_emotes.animation.json";
}

/// Loads every `customEmotes` file and bakes their animations into the client's resource pack
/// (read by the game when it loads its resource packs, after mod load).
void loadCustomEmotes() {
    gCustom.clear();
    std::error_code ec;
    std::filesystem::create_directories(emotesDir(), ec);
    std::vector<std::pair<std::string, script::Emote const*>> bake;
    for (auto const& file : cfg().customEmotes) {
        std::string id = "custom:" + file;
        if (!isValidCustomId(id)) {
            log().warn("[Custom Emotes] Invalid emote name {}", file);
            continue;
        }
        auto e = readCustom(file);
        if (!e) continue;
        gCustom[file] = CustomEmote{file, e};
        bake.emplace_back(id, e.get());
    }
    auto out = customAnimationFile();
    if (bake.empty()) {
        std::filesystem::remove(out, ec);
        return;
    }
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    f << script::bakeAnimationFile(bake, false).dump(1) << "\n";
    log().info("emotes: {} custom emote(s) baked into {}", bake.size(), out.string());
}

// ---- server state -----------------------------------------------------------------------------------

struct ServerState {
    bool                     have    = false;
    bool                     enabled = true;
    std::vector<std::string> emotes;
    int                      tier = 0;
};
ServerState gServer;

struct MenuEntry {
    std::string id;      ///< "wave", "custom:file"
    std::string icon;    ///< icon id
    std::string name;    ///< display name
    int         tier = 0;
};

std::string builtinName(std::string const& id) {
    std::string fallback = id;
    if (!fallback.empty()) fallback[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(fallback[0])));
    return ctr("quark.emote." + id, fallback);
}

std::string customName(std::string const& file) {
    auto it = gCustom.find(file);
    if (it != gCustom.end() && it->second.emote && !it->second.emote->name.empty()) return it->second.emote->name;
    return file;
}

/// Quark's emote map order (enabled default emotes, patreon emotes, custom emotes), filtered by
/// the local tier and grouped by tier (lower tiers first).
std::vector<MenuEntry> menuEntries() {
    std::vector<MenuEntry> all;
    for (auto const& id : gServer.emotes) {
        auto const* e = compiledBuiltin(id);
        all.push_back({id, id, builtinName(id), e ? e->tier : 0});
    }
    for (auto const& file : cfg().customEmotes) {
        auto it = gCustom.find(file);
        if (it == gCustom.end()) continue;
        int tier = it->second.emote ? it->second.emote->tier : 0;
        if (tier > gServer.tier) continue;
        all.push_back({"custom:" + file, "custom", customName(file), tier});
    }
    std::stable_sort(all.begin(), all.end(), [](MenuEntry const& a, MenuEntry const& b) { return a.tier < b.tier; });
    return all;
}

void request(std::string const& id) {
    if (!net::serverHasMod()) return;
    log().info("emotes: requesting {}", id);
    net::sendToServer(EmoteRequest{id});
}

// ---- playing emotes (HUD and sounds) ---------------------------------------------------------------

struct Active {
    std::string                           id;
    std::string                           icon;
    std::string                           name;
    std::shared_ptr<script::Emote const>  emote;
    std::unique_ptr<script::Sampler>      sampler;
    std::chrono::steady_clock::time_point start;
    std::vector<std::pair<int, uint64_t>> sounds; ///< (cue, handle) of playing sounds
    std::vector<std::pair<int, float>>    loops;  ///< continuous cues: (cue, next replay ms)
};
std::map<std::int64_t, Active> gActive; ///< by player unique id

/// Continuous sounds are replayed at this interval (the client sound API has no looping call
/// usable from the mod; see docs/spec/emotes.md).
constexpr float kLoopMs = 1000.f;

Level* clientLevel() {
    auto* p = client::currentPlayer();
    return p ? &p->getLevel() : nullptr;
}

void stopSounds(Active& a, int onlyCue = -2) {
    auto* level = clientLevel();
    for (auto it = a.sounds.begin(); it != a.sounds.end();) {
        bool match = onlyCue == -2 || it->first == onlyCue;
        if (!match) {
            ++it;
            continue;
        }
        if (level) level->getSoundPlayer()->stop(it->second);
        it = a.sounds.erase(it);
    }
    std::erase_if(a.loops, [&](auto const& l) { return onlyCue == -2 || l.first == onlyCue; });
}

void soundOnce(Active& a, std::int64_t player, int cue) {
    auto* level = clientLevel();
    if (!level || cue < 0 || cue >= static_cast<int>(a.emote->sounds.size())) return;
    auto const& c = a.emote->sounds[static_cast<size_t>(cue)];
    ActorUniqueID uid;
    uid.rawID   = player;
    auto* actor = level->fetchEntity(uid, false);
    if (!actor) return;
    std::string name = c.sound;
    if (name.starts_with("minecraft:")) name = name.substr(10);
    auto handle = level->getSoundPlayer()->play(name, actor->getPosition(), c.volume, c.pitch, std::nullopt);
    a.sounds.emplace_back(cue, handle);
}

void playCue(Active& a, std::int64_t player, int cue, float elapsed) {
    soundOnce(a, player, cue);
    if (cue >= 0 && cue < static_cast<int>(a.emote->sounds.size())
        && a.emote->sounds[static_cast<size_t>(cue)].kind == script::SoundCue::Kind::Continuous)
        a.loops.emplace_back(cue, elapsed + kLoopMs);
}

void startEmote(EmotePlay const& m) {
    std::shared_ptr<script::Emote const> emote;
    std::string                          icon = m.emote, name;
    if (m.emote.starts_with("custom:")) {
        auto file = m.emote.substr(7);
        auto it   = gCustom.find(file);
        if (it == gCustom.end()) return; // Quark: clients without the file ignore it
        if (cfg().customEmoteDebug) {
            if (auto fresh = readCustom(file)) it->second.emote = fresh;
        }
        if (it->second.emote && it->second.emote->tier > m.tier) return;
        emote = it->second.emote;
        icon  = "custom";
        name  = customName(file);
    } else {
        auto const* b = compiledBuiltin(m.emote);
        if (!b || b->tier > m.tier) return;
        emote = std::shared_ptr<script::Emote const>(b, [](script::Emote const*) {});
        name  = builtinName(m.emote);
    }
    if (auto it = gActive.find(m.player); it != gActive.end()) stopSounds(it->second);
    Active a;
    a.id      = m.emote;
    a.icon    = icon;
    a.name    = name;
    a.emote   = emote;
    a.sampler = std::make_unique<script::Sampler>(*emote);
    a.start   = std::chrono::steady_clock::now();
    gActive[m.player] = std::move(a);
    log().info("emotes: {} plays {} (client)", m.playerName, m.emote);
}

/// Quark's EmoteBase.isDone: an emote also ends when its player swings (attackAnim > 0) or is
/// hurt (hurtTime > 0); the animation itself stops on the same condition (kStopExpression).
bool interrupted(Level& level, std::int64_t player) {
    ActorUniqueID uid;
    uid.rawID   = player;
    auto* actor = level.fetchEntity(uid, false);
    if (!actor || !actor->isPlayer()) return false;
    auto& mob = static_cast<Mob&>(*actor);
    if (static_cast<float>(mob.mAttackAnim) > 0.f || static_cast<bool>(mob.mSwinging)) return true;
    auto hurt = actor->getEntityContext().tryGetComponent<MobHurtTimeComponent>();
    return hurt && static_cast<int>(hurt->mValue) > 0;
}

void updateActive() {
    // Another world or server: the old emotes (and their sound handles) belong to a gone level.
    static Level* lastLevel = nullptr;
    auto*         level     = clientLevel();
    if (level != lastLevel) {
        lastLevel = level;
        gActive.clear();
    }
    if (!net::serverHasMod()) gServer = {};
    auto now = std::chrono::steady_clock::now();
    for (auto it = gActive.begin(); it != gActive.end();) {
        auto& a       = it->second;
        float elapsed = std::chrono::duration<float, std::milli>(now - a.start).count();
        float total   = a.emote->totalMs();
        if (level && interrupted(*level, it->first)) elapsed = total;
        if (elapsed < total) a.sampler->advance(elapsed);
        for (auto const& ev : a.sampler->takeSoundEvents()) {
            using What = script::Sampler::SoundEvent::What;
            if (ev.what == What::StopAll) stopSounds(a);
            else if (ev.what == What::StopSection) {
                if (ev.cue >= 0 && ev.cue < static_cast<int>(a.emote->sounds.size())
                    && a.emote->sounds[static_cast<size_t>(ev.cue)].endWithSection)
                    stopSounds(a, ev.cue);
            } else playCue(a, it->first, ev.cue, elapsed);
        }
        for (auto& [cue, next] : a.loops) {
            if (elapsed < next) continue;
            soundOnce(a, it->first, cue);
            next = elapsed + kLoopMs;
        }
        if (elapsed >= total) {
            stopSounds(a);
            it = gActive.erase(it);
        } else ++it;
    }
}

// ---- chat screen buttons ---------------------------------------------------------------------------

bool gVisible      = false; // Quark: emotesVisible (kept while the game runs)
bool gReported     = false; // positions logged for this opening of the chat screen (e2e scenarios)
bool gReportedGrid = false;

void drawEmoteButton(ui::Overlay& o, MenuEntry const& e, ui::Rect r) {
    auto& dc = o.draw();
    o.button(r, "", [id = e.id] { request(id); });
    // Quark's look: a dark translucent square (darker on hover), the icon, the tier badge.
    auto m     = o.mouse();
    bool hover = r.contains(m.x, m.y);
    dc.fill(r, ui::Color{0.12f, 0.12f, 0.12f, 1});
    dc.fill(r, ui::Color{0, 0, 0, hover ? 0.63f : 0.5f});
    if (hover) dc.outline(r, ui::colors::white);
    drawIcon(dc, e.icon, r.x + 4, r.y + 4);
    if (e.tier >= 2) drawIcon(dc, "patreon_t2", r.x + 4, r.y + 4);
    else if (e.tier >= 1) drawIcon(dc, "patreon_t1", r.x + 4, r.y + 4);
}

void chatOverlay(ui::Overlay& o, ui::ScreenInfo const& s) {
    if (!cfg().enableEmotes || !net::serverHasMod() || !gServer.enabled) return;
    // A gap between frames means the chat screen was opened again: log the positions again.
    static auto lastFrame = std::chrono::steady_clock::time_point{};
    auto        frameNow  = std::chrono::steady_clock::now();
    if (frameNow - lastFrame > std::chrono::milliseconds(500)) gReported = gReportedGrid = false;
    lastFrame    = frameNow;
    auto entries = menuEntries();
    int   W       = static_cast<int>(s.size.x);
    int   H       = static_cast<int>(s.size.y);
    int   buttonX = cfg().buttonShiftX;
    int   buttonY = H - 40 + cfg().buttonShiftY;

    // Rows per tier group, 3 buttons per row, stacked upwards from the toggle button.
    std::map<int, std::vector<MenuEntry const*>> groups;
    for (auto const& e : entries) groups[e.tier].push_back(&e);
    int rows = 0;
    for (auto const& [tier, list] : groups) rows += static_cast<int>((list.size() + kEmotesPerRow - 1) / kEmotesPerRow);

    ui::Rect toggle{static_cast<float>(buttonX + W - 2 - kButtonWidth * kEmotesPerRow), static_cast<float>(buttonY),
                    static_cast<float>(kButtonWidth * kEmotesPerRow), 20};
    o.button(toggle, ctr("quark.gui.button.emotes", "Emotes"), [] {
        gVisible = !gVisible;
        log().info("emotes: menu {}", gVisible ? "shown" : "hidden");
    });

    std::vector<std::pair<MenuEntry const*, ui::Rect>> placed;
    if (gVisible) {
        int row = 0;
        for (auto const& [tier, list] : groups) {
            int rowPos = 0, tierRow = 0;
            for (auto const* e : list) {
                int rowSize = std::min(static_cast<int>(list.size()) - tierRow * kEmotesPerRow, kEmotesPerRow);
                int x = buttonX + W - 2 - (kButtonWidth * (kEmotesPerRow + 1))
                      + (((rowPos + 1) * 2 + kEmotesPerRow - rowSize) * kButtonWidth / 2 + 1);
                int y = buttonY - kButtonWidth * (rows - row);
                ui::Rect r{static_cast<float>(x), static_cast<float>(y), kButtonWidth - 1, kButtonWidth - 1};
                drawEmoteButton(o, *e, r);
                placed.emplace_back(e, r);
                if (++rowPos == kEmotesPerRow) {
                    ++tierRow;
                    ++row;
                    rowPos = 0;
                }
            }
            if (rowPos != 0) ++row;
        }
        // Hover: the emote's name in a bubble above the button (Quark: drawChatBubble), drawn
        // last so no button covers it.
        auto m = o.mouse();
        for (auto const& [e, r] : placed) {
            if (!r.contains(m.x, m.y)) continue;
            auto& dc = o.draw();
            float w  = dc.textWidth(e->name) + 8;
            ui::Rect bubble{std::max(2.f, r.x + r.w / 2 - w / 2), r.y - 16, w, 13};
            bubble.x = std::min(bubble.x, s.size.x - w - 2);
            dc.fill(bubble, ui::Color{0, 0, 0, 0.8f});
            dc.outline(bubble, ui::colors::white);
            dc.text(e->name, bubble.x + 4, bubble.y + 3);
        }
    }
    if (!gReported) {
        gReported = true;
        log().info("emotes: chat buttons drawn ({} emotes) toggle at {:.4f},{:.4f}", entries.size(),
                   (toggle.x + toggle.w / 2) / s.size.x, (toggle.y + toggle.h / 2) / s.size.y);
    }
    if (gVisible && !gReportedGrid && !placed.empty()) {
        gReportedGrid = true;
        for (auto const& [e, r] : placed)
            log().info("emotes: button {} at {:.4f},{:.4f}", e->id, (r.x + r.w / 2) / s.size.x, (r.y + r.h / 2) / s.size.y);
    }
}

void hud(ui::DrawContext& dc) {
    updateActive();
    auto* p = client::currentPlayer();
    if (!p) return;
    auto it = gActive.find(p->getOrCreateUniqueID().rawID);
    if (it == gActive.end()) return;
    auto&  a       = it->second;
    float  elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - a.start).count();
    float  total   = a.emote->totalMs();
    if (elapsed >= total || interrupted(p->getLevel(), it->first)) return;
    // Quark fades over 5 ms at each end (in practice: always opaque).
    constexpr float tween = 5.f;
    float           alpha = elapsed < tween ? elapsed / tween : elapsed > total - tween ? (total - elapsed) / tween : 1.f;
    auto            size  = dc.screenSize();
    float           x     = size.x / 2 - 16;
    float           y     = size.y / 2 - 60;
    drawIcon(dc, a.icon, x, y, 2, std::clamp(alpha, 0.f, 1.f));
    dc.text(a.name, size.x / 2, y + 34, ui::Color{1, 1, 1, std::clamp(alpha, 0.f, 1.f)}, 1, ui::Align::Center);
}

} // namespace

// ---- registrations ---------------------------------------------------------------------------------

LP_REGISTER_HUD(hud);
// The chat screen's root is "chat.chat_screen" (namespace-qualified).
LP_REGISTER_SCREEN_OVERLAY("*chat_screen", chatOverlay);

// Quark's keybinds: `quark.emote.<name>` for the 12 default emotes, `quark.keybind.patreon_emote.<name>`
// for the patreon ones; all unbound by default.
#define LP_EMOTE_KEY(name)    LP_REGISTER_KEYBIND("emote." name, ui::Key::Unbound, [] { request(name); })
#define LP_PATREON_KEY(name)  LP_REGISTER_KEYBIND("patreon_emote." name, ui::Key::Unbound, [] { request(name); })
LP_EMOTE_KEY("no");
LP_EMOTE_KEY("yes");
LP_EMOTE_KEY("wave");
LP_EMOTE_KEY("salute");
LP_EMOTE_KEY("cheer");
LP_EMOTE_KEY("clap");
LP_EMOTE_KEY("think");
LP_EMOTE_KEY("point");
LP_EMOTE_KEY("shrug");
LP_EMOTE_KEY("headbang");
LP_EMOTE_KEY("weep");
LP_EMOTE_KEY("facepalm");
LP_PATREON_KEY("dance");
LP_PATREON_KEY("tpose");
LP_PATREON_KEY("dab");
LP_PATREON_KEY("jet");
LP_PATREON_KEY("exorcist");
LP_PATREON_KEY("zombie");
#undef LP_EMOTE_KEY
#undef LP_PATREON_KEY

class EmotesClientModule {
public:
    void load() { loadCustomEmotes(); }
    void install() {
        mList = net::onClient<EmoteList>([](EmoteList const& m) {
            gServer = {true, m.enabled, m.emotes, m.tier};
            gReported = false;
            log().info("emotes: {} emotes available (tier {})", m.emotes.size(), m.tier);
        });
        mPlay = net::onClient<EmotePlay>([](EmotePlay const& m) {
            if (cfg().enableEmotes) startEmote(m);
        });
    }
    void uninstall() {
        net::removeHandler(mList);
        net::removeHandler(mPlay);
        for (auto& [id, a] : gActive) stopSounds(a);
        gActive.clear();
    }

private:
    net::HandlerId mList = 0, mPlay = 0;
};

// ---- client selftests ------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("emotes.client_icons", [](selftest::Context& ctx) {
    for (auto const& b : builtinEmotes()) ctx.check(iconOf(b.id) != nullptr, "icon for " + std::string(b.id));
    ctx.check(iconOf("custom") && iconOf("patreon_t1") && iconOf("patreon_t2"), "custom icon and tier badges");
    bool found = false;
    for (auto const& k : ui::KeybindRegistry::entries()) found = found || k.name == "patreon_emote.zombie";
    ctx.check(found, "emote keybinds are registered");
});

LP_REGISTER_SELFTEST("emotes.client_play", [](selftest::Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = client::currentPlayer();
    ctx.require(p != nullptr, "a local player");
    EmotePlay m{"wave", p->getOrCreateUniqueID().rawID, "test", 0};
    startEmote(m);
    ctx.check(gActive.contains(m.player), "the emote is tracked for the HUD");
    ctx.check(menuEntries().size() >= gServer.emotes.size(), "menu has the server's emotes");
    co_await ctx.ticks(40); // 2 s > wave's 1.6 s
    updateActive();
    ctx.check(!gActive.contains(m.player), "it ends after its length");
});

} // namespace lp::client::emotes

LP_REGISTER_MODULE(lp::client::emotes::EmotesClientModule);

#endif // LP_CLIENT
