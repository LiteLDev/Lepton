// Unit tests of the emote script engine (U19 emotes) and the golden check of the baked
// resource-pack animations. The engine is pure C++ in src/feature/emotes/; its sources are
// compiled into this test binary directly (lp-tests links only lp-core).
//
// Regenerate the committed animations after changing the engine or a built-in emote:
//     $env:LP_UPDATE_EMOTE_ANIMATIONS = '1'; xmake run lp-tests --gtest_filter=Emote*

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <map>
#include <sstream>

#include "feature/emotes/Builtins.cpp"     // NOLINT(bugprone-suspicious-include)
#include "feature/emotes/EmoteScript.cpp"  // NOLINT(bugprone-suspicious-include)

namespace es = lp::feature::emotes::script;
using lp::feature::emotes::builtinEmotes;
using lp::feature::emotes::compiledBuiltin;

namespace {

/// The checkout: from this file's path when it is absolute, else the first parent of the working
/// directory with xmake.lua and assets/.
std::filesystem::path repoRoot() {
    std::filesystem::path file(__FILE__);
    if (file.is_absolute()) return file.parent_path().parent_path().parent_path();
    for (auto dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "xmake.lua") && std::filesystem::exists(dir / "assets")) return dir;
        if (dir == dir.parent_path()) break;
    }
    return std::filesystem::current_path();
}

std::filesystem::path animationDir() { return repoRoot() / "assets" / "pack" / "rp" / "animations"; }

/// file name -> contents of every committed emote animation file (one per emote + the stop).
std::map<std::string, nlohmann::ordered_json> bakedBuiltins() {
    std::map<std::string, nlohmann::ordered_json> files;
    for (auto const& b : builtinEmotes())
        files[es::animationFileName(b.id)] = es::bakeAnimationFile({{std::string(b.id), compiledBuiltin(b.id)}}, false);
    files[std::string(es::kStopAnimationFile)] = es::stopAnimationFile();
    return files;
}

constexpr int kRightArmX = es::RightArm * 3 + 0;
constexpr int kRightArmZ = es::RightArm * 3 + 2;
constexpr int kHeadY     = es::Head * 3 + 1;

} // namespace

TEST(EmoteScript, BuiltinsCompile) {
    ASSERT_EQ(builtinEmotes().size(), 18u);
    for (auto const& b : builtinEmotes()) {
        auto r = es::compile(b.script);
        EXPECT_TRUE(r.ok) << b.id << ": line " << r.errorLine << ": " << r.error;
        EXPECT_TRUE(r.emote.root.has_value()) << b.id;
        EXPECT_EQ(r.emote.tier > 0, b.patreon) << b.id;
    }
    EXPECT_EQ(compiledBuiltin("dance")->tier, 1);
    EXPECT_EQ(compiledBuiltin("jet")->tier, 2);
    EXPECT_EQ(compiledBuiltin("no_such"), nullptr);
}

TEST(EmoteScript, Durations) {
    EXPECT_FLOAT_EQ(compiledBuiltin("wave")->totalMs(), 200 + 200 * 6 + 200);
    EXPECT_FLOAT_EQ(compiledBuiltin("point")->totalMs(), 400 + 1000 + 400);
    EXPECT_FLOAT_EQ(compiledBuiltin("salute")->totalMs(), 150 + 2500 + 300);
    EXPECT_FLOAT_EQ(compiledBuiltin("tpose")->totalMs(), 400 + 600000 + 400);
    EXPECT_FLOAT_EQ(compiledBuiltin("exorcist")->totalMs(), 7500);
    // dance: 200 + 2 * max(8 * 200, 2 * 4 * 200) + 400
    EXPECT_FLOAT_EQ(compiledBuiltin("dance")->totalMs(), 200 + 3200 + 400);
}

TEST(EmoteScript, AllDurations) {
    // Quark's timeline lengths (ms), listed in docs/spec/emotes.md.
    std::map<std::string, float> expected{
        {"no", 1400},       {"yes", 1400},  {"wave", 1600},   {"salute", 2950}, {"cheer", 2400},  {"clap", 2000},
        {"think", 3900},    {"point", 1800}, {"shrug", 800},  {"headbang", 4600}, {"weep", 8250}, {"facepalm", 3700},
        {"dance", 3800},    {"tpose", 600800}, {"dab", 2800}, {"jet", 9900},    {"exorcist", 7500}, {"zombie", 9700},
    };
    for (auto const& b : builtinEmotes()) {
        std::string id(b.id);
        EXPECT_FLOAT_EQ(compiledBuiltin(b.id)->totalMs(), expected[id]) << id;
    }
}

TEST(EmoteScript, SamplerFollowsTheTweenEngine) {
    auto const& wave = *compiledBuiltin("wave");
    es::Sampler s(wave);
    auto        v0 = s.advance(0)[kRightArmX];
    EXPECT_FLOAT_EQ(v0.a, 1); // starts from the current pose
    EXPECT_FLOAT_EQ(v0.b, 0);
    auto mid = s.advance(100)[kRightArmX]; // Quad in-out at half time = 0.5
    EXPECT_NEAR(mid.a, 0.5, 1e-4);
    EXPECT_NEAR(mid.b, -2.82 * 0.5, 1e-4);
    auto up = s.advance(200)[kRightArmX];
    EXPECT_NEAR(up.a, 0, 1e-5);
    EXPECT_NEAR(up.b, -2.82, 1e-5);
    // The yoyo: forward on even iterations, back on odd ones.
    EXPECT_NEAR(s.advance(400)[kRightArmZ].b, -0.94, 1e-4);
    EXPECT_NEAR(s.advance(600)[kRightArmZ].a, 1, 1e-4);
    EXPECT_NEAR(s.advance(1400)[kRightArmZ].a, 1, 1e-4); // 6 iterations: ends where it started
    auto end = s.advance(1600)[kRightArmX];
    EXPECT_NEAR(end.a, 0, 1e-5);
    EXPECT_NEAR(end.b, 0, 1e-5);

    es::Sampler p(*compiledBuiltin("point"));
    EXPECT_NEAR(p.advance(900)[kRightArmX].b, -1.57, 1e-4); // held during the yoyo delay
    auto back = p.advance(1800)[kRightArmX];
    EXPECT_NEAR(back.a, 1, 1e-4); // back to the starting pose
    EXPECT_NEAR(back.b, 0, 1e-4);

    es::Sampler n(*compiledBuiltin("no"));
    EXPECT_NEAR(n.advance(200)[kHeadY].b, 0.2, 1e-4);
    EXPECT_NEAR(n.advance(400)[kHeadY].b, -0.2, 1e-4);
}

TEST(EmoteScript, TimelineYoyoAndRepeat) {
    // A timeline yoyo plays the section back after its delay.
    auto r = es::compile("use head\nanimation sequence\nmove head_x 100 1\nyoyo 1 50\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FLOAT_EQ(r.emote.totalMs(), 250);
    es::Sampler s(r.emote);
    EXPECT_NEAR(s.advance(100)[0].b, 1, 1e-5);
    EXPECT_NEAR(s.advance(150)[0].b, 1, 1e-5);
    EXPECT_NEAR(s.advance(250)[0].a, 1, 1e-5);

    // A repeat restarts each tween from the value it first started from.
    auto q = es::compile("use head\nanimation sequence\nsection sequence\nmove head_x 100 1\nmove head_x 100 2\nrepeat 1 0\nend\nend\n");
    ASSERT_TRUE(q.ok) << q.error;
    es::Sampler t(q.emote);
    EXPECT_NEAR(t.advance(199.9f)[0].b, 2, 1e-3);
    auto restart = t.advance(250)[0]; // second iteration, first tween half way from the pose to 1
    EXPECT_NEAR(restart.a, 0.5, 1e-4);
    EXPECT_NEAR(restart.b, 0.5, 1e-4);
    EXPECT_NEAR(t.advance(400)[0].b, 2, 1e-5);
}

TEST(EmoteScript, RepeatedSectionsReplayTheirStartSounds) {
    auto r = es::compile("use head\nanimation sequence\nsection sequence\nsound instant emote a\nmove head_x 200 1\n"
                         "repeat 3 0\nend\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    es::Sampler s(r.emote);
    int         plays = 0;
    for (float t = 0; t <= 800; t += 50) {
        s.advance(t);
        for (auto const& e : s.takeSoundEvents()) plays += e.what == es::Sampler::SoundEvent::What::Play;
    }
    EXPECT_EQ(plays, 4);
}

TEST(EmoteScript, YoyoForwardPassesReplayTheirStartSounds) {
    // Iterations 0 and 2 play forwards (cues fire), iteration 1 plays back (no cues).
    auto r = es::compile("use head\nanimation sequence\nsection sequence\nsound instant emote a\nmove head_x 100 1\n"
                         "yoyo 2 0\nend\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    es::Sampler s(r.emote);
    int         plays = 0;
    for (float t = 0; t <= 400; t += 10) {
        s.advance(t);
        for (auto const& e : s.takeSoundEvents()) plays += e.what == es::Sampler::SoundEvent::What::Play;
    }
    EXPECT_EQ(plays, 2);
}

TEST(EmoteScript, NegativeRepeatCountsLoopForever) {
    auto r = es::compile("use head\nanimation sequence\nmove head_x 100 1 yoyo -1 0\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_GT(r.emote.totalMs(), 1e9f);
    es::Sampler s(r.emote);
    EXPECT_NEAR(s.advance(100000)[0].b, 0, 1e-3); // an even number of passes later: back at the pose
    EXPECT_NEAR(s.advance(100050)[0].b, 0.5, 1e-3);
}

TEST(EmoteScript, ByteOrderMarkIsIgnored) {
    auto r = es::compile("\xEF\xBB\xBFname BOM\nuse head\nanimation sequence\nmove head_x 100 1\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.emote.name, "BOM");
}

TEST(EmoteScript, EveryBakedAnimationHasBones) {
    // Bedrock drops the whole animation file when one animation has an empty "bones" object.
    auto broken = es::compile("use head\nanimation nonsense\n");
    ASSERT_FALSE(broken.ok);
    EXPECT_FALSE(es::bakeAnimation(broken.emote)["bones"].empty());
    auto still = es::compile("use head\nanimation sequence\npause 100\nend\n");
    ASSERT_TRUE(still.ok) << still.error;
    EXPECT_FALSE(es::bakeAnimation(still.emote)["bones"].empty());
    std::vector<std::pair<std::string, es::Emote const*>> none;
    auto file = es::bakeAnimationFile(none);
    for (auto const& [id, anim] : file["animations"].items()) EXPECT_FALSE(anim["bones"].empty()) << id;
}

TEST(EmoteScript, HugeScriptsStayBounded) {
    auto r = es::compile("use head\nanimation sequence\nmove head_x 100 1 repeat 2000000000 0\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    auto anim = es::bakeAnimation(r.emote); // capped at an hour of animation
    EXPECT_DOUBLE_EQ(anim["animation_length"].get<double>(), 3600.0);
    es::Sampler s(r.emote);
    s.advance(1e9f);
    SUCCEED();
}

TEST(EmoteScript, NegativeRepeatIsInfinite) {
    auto r = es::compile("use head\nanimation sequence\nmove head_x 100 1 yoyo -1 0\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_GT(r.emote.totalMs(), 3600000.f); // longer than any bake
    es::Sampler s(r.emote);
    EXPECT_NEAR(s.advance(100)[0].b, 1, 1e-5);   // forward
    EXPECT_NEAR(s.advance(200)[0].b, 0, 1e-5);   // back (yoyo)
    EXPECT_NEAR(s.advance(100300)[0].b, 1, 1e-5); // still going
    auto anim = es::bakeAnimation(r.emote, 50.f);
    EXPECT_DOUBLE_EQ(anim["animation_length"].get<double>(), 3600.0);
}

TEST(EmoteScript, UnitScalesTimes) {
    auto r = es::compile("use head\nunit 2\nanimation sequence\nmove head_x 100 1 delay 10\npause 50\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FLOAT_EQ(r.emote.totalMs(), 220 + 100);
}

TEST(EmoteScript, ResetTweensUsedPartsToZero) {
    auto r = es::compile("use head\nanimation sequence\nmove head_x 100 1\nreset all all 100\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_TRUE(r.emote.root);
    auto const& reset = r.emote.root->children.at(1);
    EXPECT_EQ(reset.children.size(), 3u); // head x, y, z only (the only used part)
    es::Sampler s(r.emote);
    s.advance(100);
    auto v = s.advance(200)[0];
    EXPECT_NEAR(v.b, 0, 1e-5);
    EXPECT_NEAR(v.a, 0, 1e-5);
}

TEST(EmoteScript, Errors) {
    auto bad = es::compile("use head\nanimation sequence\nmove head_q 100 1\nend\n");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.errorLine, 3);
    EXPECT_EQ(bad.error, "Illegal part name for function move: head_q");
    EXPECT_FALSE(bad.emote.root.has_value()); // falls back to an empty animation
    EXPECT_TRUE(bad.emote.usesPart(es::Head));

    EXPECT_EQ(es::compile("animation sequence\nanimation parallel\n").error,
              "Illegal use of function animation, animation already started");
    EXPECT_EQ(es::compile("move head_x 1 1\n").error, "Illegal use of function move, animation not started");
    EXPECT_EQ(es::compile("use tail\n").error, "Illegal part name for function use: tail");
    EXPECT_EQ(es::compile("jump\n").error, "Illegal function name jump");
    EXPECT_EQ(es::compile("animation sequence\nmove head_x 1 1 ease wobble\n").error, "Easing type wobble doesn't exist");
    EXPECT_EQ(es::compile("animation sequence\nmove head_x 1 1 spin\n").error, "Invalid modifier spin for move function");
    EXPECT_EQ(es::compile("animation sequence\nsound loud emote a\n").error, "Invalid modifier loud for sound function");
    EXPECT_TRUE(es::compile("# comment\n\n   \nuse head\n").ok);
    // Lines after the closing `end` are not read.
    EXPECT_TRUE(es::compile("animation sequence\nend\nthis is ignored\n").ok);
}

TEST(EmoteScript, CustomEmoteName) {
    auto r = es::compile("name  My  Dance\nuse head\n");
    EXPECT_EQ(r.emote.name, "My Dance");
}

TEST(EmoteScript, Easings) {
    for (auto name : {"linear", "none", "in_quad", "out_quad", "in_out_quad", "in_out_cubic", "out_bounce", "in_out_elastic",
                      "in_back", "out_expo", "in_out_sine", "in_circ", "out_quint", "in_out_quart"}) {
        auto e = es::easeByName(name);
        ASSERT_TRUE(e.has_value()) << name;
        EXPECT_NEAR(es::applyEase(*e, 0), 0, 1e-4) << name;
        EXPECT_NEAR(es::applyEase(*e, 1), 1, 1e-4) << name;
    }
    EXPECT_NEAR(es::applyEase(es::Ease::InOutQuad, 0.25f), 0.125f, 1e-5);
    EXPECT_NEAR(es::applyEase(es::Ease::Linear, 0.3f), 0.3f, 1e-6);
}

TEST(EmoteScript, Sounds) {
    auto r = es::compile("use head\nanimation sequence\nsound instant emote minecraft:entity.cat.purr 2 1.5\n"
                         "move head_x 100 1\nsound continuous section minecraft:block.note_block.harp\n"
                         "move head_x 100 0\nsound stop\nend\n");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.emote.sounds.size(), 3u);
    EXPECT_FLOAT_EQ(r.emote.sounds[0].volume, 1.5f); // capped like Quark
    EXPECT_FLOAT_EQ(r.emote.sounds[0].pitch, 1.5f);
    EXPECT_TRUE(r.emote.sounds[1].endWithSection);
    es::Sampler s(r.emote);
    s.advance(0);
    auto start = s.takeSoundEvents();
    ASSERT_EQ(start.size(), 2u);
    EXPECT_EQ(start[0].what, es::Sampler::SoundEvent::What::StopAll); // `animation` ends old sounds
    EXPECT_EQ(start[1].what, es::Sampler::SoundEvent::What::Play);
    EXPECT_EQ(start[1].cue, 0);
    s.advance(100);
    auto mid = s.takeSoundEvents();
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_EQ(mid[0].cue, 1);
    s.advance(200);
    auto end = s.takeSoundEvents();
    ASSERT_GE(end.size(), 2u);
    EXPECT_EQ(end[0].what, es::Sampler::SoundEvent::What::StopAll); // `sound stop`
    EXPECT_EQ(end.back().what, es::Sampler::SoundEvent::What::StopSection);
}

TEST(EmoteScript, BakesRelativeToTheCurrentPose) {
    auto anim = es::bakeAnimation(*compiledBuiltin("point"));
    EXPECT_DOUBLE_EQ(anim["animation_length"].get<double>(), 1.8);
    auto const& rot = anim["bones"]["rightarm"]["rotation"];
    ASSERT_TRUE(rot.contains("0") && rot.contains("1.8")) << rot.dump();
    EXPECT_EQ(rot["0"][0], 0.0); // from the current pose
    EXPECT_EQ(rot["1.8"][0], 0.0); // back to it
    bool held = false;            // -1.57 rad held during the yoyo delay
    for (auto const& [t, v] : rot.items()) held = held || v[0] == "-89.954 - this";
    EXPECT_TRUE(held) << rot.dump();
    EXPECT_TRUE(anim["bones"].size() == 1);
    EXPECT_EQ(es::animationId("custom:my_dance"), "animation.lepton.emote.custom.my_dance");
}

TEST(EmoteScript, CommittedAnimationsAreUpToDate) {
    auto baked = bakedBuiltins();
    auto dir   = animationDir();
    bool update = std::getenv("LP_UPDATE_EMOTE_ANIMATIONS") != nullptr;
    if (update) std::filesystem::create_directories(dir);
    // No stale emote files (removed emotes, the old single file).
    for (auto const& entry : std::filesystem::directory_iterator(dir)) {
        auto name = entry.path().filename().string();
        if (!name.starts_with("lepton_emote") || baked.contains(name)) continue;
        if (update) std::filesystem::remove(entry.path());
        else ADD_FAILURE() << entry.path().string() << " is stale: set LP_UPDATE_EMOTE_ANIMATIONS=1 and rerun";
    }
    for (auto const& [name, json] : baked) {
        auto path = dir / name;
        auto text = json.dump(2) + "\n";
        // Bedrock rejects the whole file when an animation has an empty (or no) "bones" object.
        for (auto const& [id, anim] : json["animations"].items())
            EXPECT_TRUE(anim.contains("bones") && anim["bones"].is_object() && !anim["bones"].empty()) << name << ": " << id << " has no bones";
        if (update) std::ofstream(path, std::ios::binary) << text;
        std::ifstream in(path, std::ios::binary);
        ASSERT_TRUE(in.good()) << "missing " << path.string() << " (set LP_UPDATE_EMOTE_ANIMATIONS=1)";
        std::stringstream ss;
        ss << in.rdbuf();
        EXPECT_EQ(ss.str(), text) << path.string() << " is stale: set LP_UPDATE_EMOTE_ANIMATIONS=1 and rerun";
    }
}
