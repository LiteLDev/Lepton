#pragma once

// The emote script language (pure C++, no game headers; unit-tested in tests/emotes/).
//
// Quark describes every emote with a small line-based script (its `.emote` files, also the format
// of custom emotes): `use <part>`, `unit <factor>`, `tier <n>`, `name <text>`, `animation
// sequence|parallel`, `section sequence|parallel`, `end`, `move <part_axis> <ms> <target>
// [delay <ms>] [yoyo <n> <ms>] [repeat <n> <ms>] [ease <name>]`, `reset <part|all>
// <all|rotation|offset> <ms>`, `pause <ms>`, `yoyo <n> <ms>`, `repeat <n> <ms>` and
// `sound <continuous|instant> <section|emote> <sound> [volume] [pitch]` / `sound stop`.
// Quark plays them with the Universal Tween Engine on the Java player model.
//
// Lepton compiles the same language into a tween tree (`compile`), samples it with the tween
// engine's rules (`Sampler`: Quad in-out easing by default, start values captured when a tween
// starts, yoyo / repeat with delays, timelines in sequence or parallel) and bakes the result into
// a Bedrock resource-pack animation (`bakeAnimation`). Every channel is written relative to the
// bone's current value (`this`), because Bedrock animations add up while Quark sets the model
// parts: a value `a * start + b` (a = the share of the pose the emote started from) becomes the
// offset `b - (1 - a) * this`.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

namespace lp::feature::emotes::script {

/// Body parts (Quark's ModelAccessor order); channel = part * 3 + axis (x, y, z).
enum Part : int { Head = 0, Body = 1, RightArm = 2, LeftArm = 3, RightLeg = 4, LeftLeg = 5, Model = 6 };
inline constexpr int kParts    = 7;
inline constexpr int kChannels = kParts * 3;
/// Iterations played for an infinite repeat/yoyo count (the tween engine's INFINITY, -1).
inline constexpr int kInfiniteRepeats = 1 << 24;

/// Part name as used in scripts ("head", "right_arm", ..., "model"), or "".
[[nodiscard]] std::string_view partName(int part);
/// Bedrock bone of the part ("head", "rightarm", ..., "root").
[[nodiscard]] std::string_view boneName(int part);

/// Easing: Quark exposes every Universal Tween Engine equation (`ease in_out_quad`, `linear`...).
enum class Ease : uint8_t {
    Linear,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InCirc, OutCirc, InOutCirc,
    InSine, OutSine, InOutSine,
    InExpo, OutExpo, InOutExpo,
    InBack, OutBack, InOutBack,
    InBounce, OutBounce, InOutBounce,
    InElastic, OutElastic, InOutElastic,
};
[[nodiscard]] std::optional<Ease> easeByName(std::string_view name);
[[nodiscard]] float               applyEase(Ease e, float t);

/// A sound statement, attached like Quark: to the completion of the statement before it in the
/// same section, or to the start of the section when it is the first one.
struct SoundCue {
    enum class Kind : uint8_t { Instant, Continuous, Stop } kind = Kind::Instant;
    bool        endWithSection = false; ///< `section`: ends when its section completes
    std::string sound;                  ///< sound id as written ("minecraft:entity.cat.purr")
    float       volume = 1, pitch = 1;
};

struct Node {
    enum class Kind : uint8_t { Tween, Pause, Timeline } kind = Kind::Timeline;
    // Timeline
    bool              parallel = false;
    std::vector<Node> children;
    // Tween
    int   channel  = 0;
    float duration = 0; ///< ms (pause: its length)
    float target   = 0;
    Ease  ease     = Ease::InOutQuad;
    // All nodes
    float delay       = 0;
    int   repeatCount = 0;
    bool  yoyo        = false;
    float repeatDelay = 0;
    /// Sound cues (indices into `Emote::sounds`) fired at each iteration start / at completion.
    std::vector<int> onStart, onComplete;
    /// Cues whose `section` sounds end when this timeline completes.
    std::vector<int> endOnComplete;
    /// `animation` root: ends every sound when it starts.
    bool endAllOnStart = false;

    /// Duration of one iteration (timelines: from their children).
    [[nodiscard]] float iterationDuration() const;
    /// delay + iteration + repeatCount * (repeatDelay + iteration).
    [[nodiscard]] float fullDuration() const;
};

struct Emote {
    std::string              name; ///< from `name ...` (custom emotes); empty otherwise
    int                      tier = 0;
    std::array<bool, kParts> used{}; ///< `use <part>`
    std::vector<SoundCue>    sounds;
    std::optional<Node>      root; ///< nullopt: empty animation
    [[nodiscard]] float totalMs() const { return root ? root->fullDuration() : 0.f; }
    [[nodiscard]] bool  usesPart(int part) const { return part >= 0 && part < kParts && used[part]; }
};

struct CompileResult {
    Emote       emote;
    bool        ok        = true;
    int         errorLine = 0; ///< 1-based
    std::string error;
};

/// Parses a script. On an error the animation falls back to an empty one (as Quark does) and
/// `error` / `errorLine` say what is wrong; `tier`, `name` and `use` lines read before stay.
[[nodiscard]] CompileResult compile(std::string_view text);

/// One channel value as `a * start + b` (start = the bone's pose under the emote, radians).
struct Affine {
    float a = 1, b = 0;
};

/// Plays a compiled emote at increasing times (ms), following the tween engine.
class Sampler {
public:
    explicit Sampler(Emote const& emote);
    ~Sampler();
    Sampler(Sampler const&)            = delete;
    Sampler& operator=(Sampler const&) = delete;

    /// Advances to `ms` (never backwards) and returns every channel's value.
    std::array<Affine, kChannels> const& advance(float ms);

    struct SoundEvent {
        enum class What : uint8_t { Play, StopAll, StopSection } what;
        int cue = -1; ///< Play / StopSection: the cue index
    };
    /// Sound events fired since the previous call, in order.
    std::vector<SoundEvent> takeSoundEvents();

    struct State;

private:
    Emote const&                  mEmote;
    std::array<Affine, kChannels> mValues{};
    std::array<State*, kChannels> mOwner{};
    std::unique_ptr<State>        mRoot;
    float                         mNow = -1;
    std::vector<SoundEvent>       mEvents;

    void visit(State& s, float local, bool mirrored);
};

/// Bakes an emote into a Bedrock animation object (`{"animation_length": ..., "bones": ...}`).
/// Samples every `stepMs`; samples that a straight line through their neighbours already gives
/// (within `toleranceDeg`) are dropped.
[[nodiscard]] nlohmann::ordered_json bakeAnimation(Emote const& emote, float stepMs = 10.f, float toleranceDeg = 0.2f);

/// The animation id of an emote id ("wave" -> "animation.lepton.emote.wave",
/// "custom:my_dance" -> "animation.lepton.emote.custom.my_dance").
[[nodiscard]] std::string animationId(std::string_view emoteId);

/// A whole Bedrock animation file with the given emotes (id -> emote) and, with `withStop`, the
/// stop animation.
[[nodiscard]] nlohmann::ordered_json
bakeAnimationFile(std::vector<std::pair<std::string, Emote const*>> const& emotes, bool withStop = true);

/// Molang that hides the emote outside Quark's "standing" pose and in first person.
inline constexpr std::string_view kBlendWeight =
    "(variable.is_first_person || query.is_sneaking || query.is_swimming || query.is_sleeping || query.is_gliding) ? 0.0 : 1.0";
/// Quark ends an emote when the player swings or is hurt.
inline constexpr std::string_view kStopExpression = "variable.attack_time > 0.0 || query.hurt_time > 0.0";
inline constexpr std::string_view kController     = "controller.animation.lepton.emote";
inline constexpr std::string_view kStopAnimation  = "animation.lepton.emote.stop";

} // namespace lp::feature::emotes::script
