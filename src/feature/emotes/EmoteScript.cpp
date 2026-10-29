#include "feature/emotes/EmoteScript.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace lp::feature::emotes::script {

namespace {

constexpr std::array<std::string_view, kParts> kPartNames{"head", "body", "right_arm", "left_arm", "right_leg", "left_leg", "model"};
constexpr std::array<std::string_view, kParts> kBoneNames{"head", "body", "rightarm", "leftarm", "rightleg", "leftleg", "root"};

struct EaseName {
    std::string_view name;
    Ease             ease;
};
// Quark derives the names from the tween engine's `TweenEquations.easeInOutQuad` style fields
// ("in_out_quad"); `none` and `linear` are the linear equation.
constexpr EaseName kEases[] = {
    {"none", Ease::Linear},           {"linear", Ease::Linear},           {"in_quad", Ease::InQuad},
    {"out_quad", Ease::OutQuad},      {"in_out_quad", Ease::InOutQuad},   {"in_cubic", Ease::InCubic},
    {"out_cubic", Ease::OutCubic},    {"in_out_cubic", Ease::InOutCubic}, {"in_quart", Ease::InQuart},
    {"out_quart", Ease::OutQuart},    {"in_out_quart", Ease::InOutQuart}, {"in_quint", Ease::InQuint},
    {"out_quint", Ease::OutQuint},    {"in_out_quint", Ease::InOutQuint}, {"in_circ", Ease::InCirc},
    {"out_circ", Ease::OutCirc},      {"in_out_circ", Ease::InOutCirc},   {"in_sine", Ease::InSine},
    {"out_sine", Ease::OutSine},      {"in_out_sine", Ease::InOutSine},   {"in_expo", Ease::InExpo},
    {"out_expo", Ease::OutExpo},      {"in_out_expo", Ease::InOutExpo},   {"in_back", Ease::InBack},
    {"out_back", Ease::OutBack},      {"in_out_back", Ease::InOutBack},   {"in_bounce", Ease::InBounce},
    {"out_bounce", Ease::OutBounce},  {"in_out_bounce", Ease::InOutBounce}, {"in_elastic", Ease::InElastic},
    {"out_elastic", Ease::OutElastic}, {"in_out_elastic", Ease::InOutElastic},
};

constexpr float kPi = std::numbers::pi_v<float>;

float bounceOut(float t) {
    if (t < 1 / 2.75f) return 7.5625f * t * t;
    if (t < 2 / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + .75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + .9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + .984375f;
}

float elastic(Ease e, float t) {
    if (t == 0 || t == 1) return t;
    constexpr float p = .3f, a = 1, s = p / 4;
    if (e == Ease::InElastic) {
        t -= 1;
        return -(a * std::pow(2.f, 10 * t) * std::sin((t - s) * (2 * kPi) / p));
    }
    if (e == Ease::OutElastic) return a * std::pow(2.f, -10 * t) * std::sin((t - s) * (2 * kPi) / p) + 1;
    // in-out: the tween engine uses a period of .3 * 1.5 here
    constexpr float p2 = .3f * 1.5f, s2 = p2 / 4;
    t *= 2;
    if (t < 1) {
        t -= 1;
        return -.5f * (a * std::pow(2.f, 10 * t) * std::sin((t - s2) * (2 * kPi) / p2));
    }
    t -= 1;
    return a * std::pow(2.f, -10 * t) * std::sin((t - s2) * (2 * kPi) / p2) * .5f + 1;
}

/// Trims ASCII whitespace.
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> tokenize(std::string_view s) {
    std::vector<std::string_view> out;
    size_t                        i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

struct ParseError {
    std::string message;
};

float parseFloat(std::string_view s) {
    std::string str(s);
    if (!str.empty() && str.back() == 'f') str.pop_back(); // Java accepts "1.5f"
    char* end = nullptr;
    float v   = std::strtof(str.c_str(), &end);
    if (str.empty() || end != str.c_str() + str.size() || !std::isfinite(v)) throw ParseError{"Illegal number " + std::string(s)};
    return v;
}

int parseInt(std::string_view s) {
    int  v       = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) throw ParseError{"Illegal number " + std::string(s)};
    return v;
}

/// The tween engine repeats forever for a negative count (Tween.INFINITY = -1): a count far
/// beyond any playable length (the bake is capped at an hour, the client stops it on a swing or
/// a hurt like the animation).
int repeatCountOf(int count) { return count < 0 ? kInfiniteRepeat : count; }

int partIndex(std::string_view name) {
    for (int i = 0; i < kParts; ++i)
        if (kPartNames[static_cast<size_t>(i)] == name) return i;
    return -1;
}

/// "head_x" -> channel 0, ..., "model_z" -> 20.
int channelIndex(std::string_view name) {
    if (name.size() < 3 || name[name.size() - 2] != '_') return -1;
    int axis = name.back() - 'x';
    if (axis < 0 || axis > 2) return -1;
    int part = partIndex(name.substr(0, name.size() - 2));
    return part < 0 ? -1 : part * 3 + axis;
}

class Parser {
public:
    Emote emote;
    bool  compiled = false;

    void line(std::string_view raw) {
        auto s = trim(raw);
        if (s.empty() || s.front() == '#') return;
        auto tokens = tokenize(s);
        auto fn     = tokens[0];
        if (fn == "name") name(tokens);
        else if (fn == "use") use(tokens);
        else if (fn == "unit") {
            count(tokens, 2);
            mSpeed = parseFloat(tokens[1]);
        } else if (fn == "tier") {
            count(tokens, 2);
            emote.tier = parseInt(tokens[1]);
        } else if (fn == "animation") animation(tokens);
        else if (fn == "section") section(tokens);
        else if (fn == "end") end(tokens);
        else if (fn == "move") move(tokens);
        else if (fn == "reset") reset(tokens);
        else if (fn == "pause") pause(tokens);
        else if (fn == "yoyo" || fn == "repeat") repeat(tokens, fn == "yoyo");
        else if (fn == "sound") sound(tokens);
        else throw ParseError{"Illegal function name " + std::string(fn)};
    }

    /// The timeline as Quark returns it: the root once closed, else whatever is open.
    std::optional<Node> result() {
        if (!mCurrent) return std::nullopt;
        return std::move(*mCurrent);
    }

private:
    float                            mSpeed = 1;
    std::optional<Node>              mCurrent;
    std::vector<Node>                mStack;

    static void count(std::vector<std::string_view> const& t, size_t n) {
        if (t.size() != n)
            throw ParseError{"Illegal parameter amount for function " + std::string(t[0]) + ": " + std::to_string(t.size())
                             + " (expected " + std::to_string(n) + ")"};
    }
    void requireStarted(std::string_view fn) const {
        if (!mCurrent) throw ParseError{"Illegal use of function " + std::string(fn) + ", animation not started"};
    }
    static Node timeline(std::string_view type, std::string_view what) {
        Node n;
        n.kind = Node::Kind::Timeline;
        if (type == "sequence") n.parallel = false;
        else if (type == "parallel") n.parallel = true;
        else throw ParseError{"Illegal " + std::string(what) + " type: " + std::string(type)};
        return n;
    }

    void name(std::vector<std::string_view> const& t) {
        std::string n;
        for (size_t i = 1; i < t.size(); ++i) {
            if (!n.empty()) n += ' ';
            n += t[i];
        }
        emote.name = n;
    }
    void use(std::vector<std::string_view> const& t) {
        count(t, 2);
        int p = partIndex(t[1]);
        if (p < 0) throw ParseError{"Illegal part name for function use: " + std::string(t[1])};
        emote.used[static_cast<size_t>(p)] = true;
    }
    void animation(std::vector<std::string_view> const& t) {
        if (mCurrent) throw ParseError{"Illegal use of function animation, animation already started"};
        count(t, 2);
        mCurrent                = timeline(t[1], "animation");
        mCurrent->endAllOnStart = true;
    }
    void section(std::vector<std::string_view> const& t) {
        requireStarted("section");
        count(t, 2);
        Node n = timeline(t[1], "section");
        mStack.push_back(std::move(*mCurrent));
        mCurrent = std::move(n);
    }
    void end(std::vector<std::string_view> const& t) {
        requireStarted("end");
        count(t, 1);
        if (mStack.empty()) {
            compiled = true;
            return;
        }
        Node parent = std::move(mStack.back());
        mStack.pop_back();
        parent.children.push_back(std::move(*mCurrent));
        mCurrent = std::move(parent);
    }
    void move(std::vector<std::string_view> const& t) {
        requireStarted("move");
        if (t.size() < 4)
            throw ParseError{"Illegal parameter amount for function move: " + std::to_string(t.size()) + " (at least 4 are required)"};
        int ch = channelIndex(t[1]);
        if (ch < 0) throw ParseError{"Illegal part name for function move: " + std::string(t[1])};
        Node n;
        n.kind     = Node::Kind::Tween;
        n.channel  = ch;
        n.duration = parseFloat(t[2]) * mSpeed;
        n.target   = parseFloat(t[3]);
        size_t i   = 4;
        auto   need = [&](std::string_view mod, size_t k) {
            if (t.size() - i < k)
                throw ParseError{"Illegal parameter amount for move modifier " + std::string(mod) + ": " + std::to_string(t.size())
                                 + " (expected at least " + std::to_string(k) + ")"};
        };
        while (i < t.size()) {
            auto cmd = t[i++];
            if (cmd == "delay") {
                need(cmd, 1);
                n.delay += parseFloat(t[i++]) * mSpeed;
            } else if (cmd == "yoyo" || cmd == "repeat") {
                need(cmd, 2);
                n.repeatCount = repeatCountOf(parseInt(t[i++]));
                n.repeatDelay = parseFloat(t[i++]) * mSpeed;
                n.yoyo        = cmd == "yoyo";
            } else if (cmd == "ease") {
                need(cmd, 1);
                auto e = easeByName(t[i++]);
                if (!e) throw ParseError{"Easing type " + std::string(t[i - 1]) + " doesn't exist"};
                n.ease = *e;
            } else {
                throw ParseError{"Invalid modifier " + std::string(cmd) + " for move function"};
            }
        }
        mCurrent->children.push_back(std::move(n));
    }
    void reset(std::vector<std::string_view> const& t) {
        if (t.size() < 4)
            throw ParseError{"Illegal parameter amount for function reset: " + std::to_string(t.size()) + " (at least 4 are required)"};
        bool allParts = t[1] == "all";
        int  part     = allParts ? 0 : partIndex(t[1]);
        if (part < 0) throw ParseError{"Illegal part name for function reset: " + std::string(t[1])};
        bool all = t[2] == "all";
        bool rot = all || t[2] == "rotation";
        bool off = all || t[2] == "offset";
        if (!rot && !off) throw ParseError{"Illegal reset type: " + std::string(t[2])};
        float time = parseFloat(t[3]) * mSpeed;
        requireStarted("reset");
        // Quark's channel range (an "offset" reset covers the parts after the named one).
        int  base  = part * 3;
        int  lower = allParts ? 0 : base + (rot ? 0 : 3);
        int  upper = allParts ? kChannels : base + (off ? kChannels : 3);
        Node par;
        par.kind     = Node::Kind::Timeline;
        par.parallel = true;
        for (int i = lower; i < upper && i < kChannels; ++i) {
            if (!emote.used[static_cast<size_t>(i / 3)]) continue;
            Node n;
            n.kind     = Node::Kind::Tween;
            n.channel  = i;
            n.duration = time;
            n.target   = 0;
            par.children.push_back(std::move(n));
        }
        mCurrent->children.push_back(std::move(par));
    }
    void pause(std::vector<std::string_view> const& t) {
        count(t, 2);
        requireStarted("pause");
        Node n;
        n.kind     = Node::Kind::Pause;
        n.duration = parseFloat(t[1]) * mSpeed;
        mCurrent->children.push_back(std::move(n));
    }
    void repeat(std::vector<std::string_view> const& t, bool yoyo) {
        count(t, 3);
        requireStarted(t[0]);
        mCurrent->repeatCount = repeatCountOf(parseInt(t[1]));
        mCurrent->repeatDelay = parseFloat(t[2]) * mSpeed;
        mCurrent->yoyo        = yoyo;
    }
    /// The callback list a statement at this point attaches to (Quark: the completion of the
    /// statement before it in the section, or the section's start).
    std::vector<int>& attachPoint() {
        if (mCurrent->children.empty()) return mCurrent->onStart;
        return mCurrent->children.back().onComplete;
    }
    void sound(std::vector<std::string_view> const& t) {
        requireStarted("sound");
        if (t.size() < 2) throw ParseError{"Expected action (continuous, instant, stop) for function sound"};
        SoundCue cue;
        if (t[1] == "stop") {
            cue.kind = SoundCue::Kind::Stop;
            emote.sounds.push_back(cue);
            attachPoint().push_back(static_cast<int>(emote.sounds.size() - 1));
            return;
        }
        if (t[1] == "continuous") cue.kind = SoundCue::Kind::Continuous;
        else if (t[1] == "instant") cue.kind = SoundCue::Kind::Instant;
        else throw ParseError{"Invalid modifier " + std::string(t[1]) + " for sound function"};
        if (t.size() < 4 || t.size() > 6)
            throw ParseError{"Illegal parameter amount for function sound: " + std::to_string(t.size()) + " (expected between 4 and 6)"};
        if (t[2] == "section") cue.endWithSection = true;
        else if (t[2] != "emote") throw ParseError{"Invalid modifier " + std::string(t[2]) + " for sound function"};
        cue.sound = std::string(t[3]);
        try {
            if (t.size() >= 5) cue.volume = std::min(parseFloat(t[4]), 1.5f);
            if (t.size() >= 6) cue.pitch = parseFloat(t[5]);
        } catch (ParseError const&) {
            throw ParseError{"Illegal number in function sound"};
        }
        emote.sounds.push_back(cue);
        int idx = static_cast<int>(emote.sounds.size() - 1);
        attachPoint().push_back(idx);
        mCurrent->endOnComplete.push_back(idx);
    }
};

std::string fmtNum(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

double round3(double v) {
    double r = std::round(v * 1000.0) / 1000.0;
    return r == 0 ? 0.0 : r;
}

} // namespace

std::string_view partName(int part) { return part >= 0 && part < kParts ? kPartNames[static_cast<size_t>(part)] : ""; }
std::string_view boneName(int part) { return part >= 0 && part < kParts ? kBoneNames[static_cast<size_t>(part)] : ""; }

std::optional<Ease> easeByName(std::string_view name) {
    for (auto const& e : kEases)
        if (e.name == name) return e.ease;
    return std::nullopt;
}

float applyEase(Ease e, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto inOut = [t](auto in) { return t < .5f ? in(t * 2) / 2 : 1 - in((1 - t) * 2) / 2; };
    auto pw    = [](float x, int n) { return std::pow(x, static_cast<float>(n)); };
    switch (e) {
    case Ease::Linear: return t;
    case Ease::InQuad: return t * t;
    case Ease::OutQuad: return 1 - pw(1 - t, 2);
    case Ease::InOutQuad: return inOut([&](float x) { return pw(x, 2); });
    case Ease::InCubic: return pw(t, 3);
    case Ease::OutCubic: return 1 - pw(1 - t, 3);
    case Ease::InOutCubic: return inOut([&](float x) { return pw(x, 3); });
    case Ease::InQuart: return pw(t, 4);
    case Ease::OutQuart: return 1 - pw(1 - t, 4);
    case Ease::InOutQuart: return inOut([&](float x) { return pw(x, 4); });
    case Ease::InQuint: return pw(t, 5);
    case Ease::OutQuint: return 1 - pw(1 - t, 5);
    case Ease::InOutQuint: return inOut([&](float x) { return pw(x, 5); });
    case Ease::InCirc: return 1 - std::sqrt(1 - t * t);
    case Ease::OutCirc: return std::sqrt(1 - (1 - t) * (1 - t));
    case Ease::InOutCirc: return inOut([](float x) { return 1 - std::sqrt(1 - x * x); });
    case Ease::InSine: return 1 - std::cos(t * kPi / 2);
    case Ease::OutSine: return std::sin(t * kPi / 2);
    case Ease::InOutSine: return -.5f * (std::cos(kPi * t) - 1);
    case Ease::InExpo: return t == 0 ? 0.f : std::pow(2.f, 10 * (t - 1));
    case Ease::OutExpo: return t == 1 ? 1.f : 1 - std::pow(2.f, -10 * t);
    case Ease::InOutExpo:
        if (t == 0 || t == 1) return t;
        return t < .5f ? .5f * std::pow(2.f, 10 * (t * 2 - 1)) : .5f * (2 - std::pow(2.f, -10 * (t * 2 - 1)));
    case Ease::InBack: {
        constexpr float s = 1.70158f;
        return t * t * ((s + 1) * t - s);
    }
    case Ease::OutBack: {
        constexpr float s = 1.70158f;
        float           x = t - 1;
        return x * x * ((s + 1) * x + s) + 1;
    }
    case Ease::InOutBack: {
        constexpr float s = 1.70158f * 1.525f;
        float           x = t * 2;
        if (x < 1) return .5f * (x * x * ((s + 1) * x - s));
        x -= 2;
        return .5f * (x * x * ((s + 1) * x + s) + 2);
    }
    case Ease::InBounce: return 1 - bounceOut(1 - t);
    case Ease::OutBounce: return bounceOut(t);
    case Ease::InOutBounce: return t < .5f ? (1 - bounceOut(1 - t * 2)) * .5f : bounceOut(t * 2 - 1) * .5f + .5f;
    case Ease::InElastic:
    case Ease::OutElastic:
    case Ease::InOutElastic: return elastic(e, t);
    }
    return t;
}

float Node::iterationDuration() const {
    if (kind != Kind::Timeline) return duration;
    float d = 0;
    for (auto const& c : children) d = parallel ? std::max(d, c.fullDuration()) : d + c.fullDuration();
    return d;
}

float Node::fullDuration() const {
    float it = iterationDuration();
    return delay + it + static_cast<float>(std::max(repeatCount, 0)) * (repeatDelay + it);
}

CompileResult compile(std::string_view text) {
    CompileResult r;
    Parser        p;
    int           lineNo = 0;
    try {
        size_t pos = 0;
        while (pos <= text.size() && !p.compiled) {
            size_t nl = text.find('\n', pos);
            auto   ln = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
            ++lineNo;
            p.line(ln);
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
        r.emote      = std::move(p.emote);
        r.emote.root = p.result();
    } catch (ParseError const& e) {
        r.ok        = false;
        r.errorLine = lineNo;
        r.error     = e.message;
        r.emote     = std::move(p.emote);
        r.emote.root.reset();
    }
    return r;
}

// ---- sampler ---------------------------------------------------------------------------------------

struct Sampler::State {
    Node const*        node = nullptr;
    std::vector<State> children;
    bool               initialized = false;
    Affine             start;
    float              prevLocal = -1e30f;

    explicit State(Node const& n) : node(&n) {
        children.reserve(n.children.size());
        for (auto const& c : n.children) children.emplace_back(c);
    }
};

Sampler::Sampler(Emote const& emote) : mEmote(emote) {
    mValues.fill(Affine{1, 0});
    if (emote.root) mRoot = std::make_unique<State>(*emote.root);
}

Sampler::~Sampler() = default;

std::vector<Sampler::SoundEvent> Sampler::takeSoundEvents() { return std::exchange(mEvents, {}); }

std::array<Affine, kChannels> const& Sampler::advance(float ms) {
    if (ms < mNow) ms = mNow;
    mNow = ms;
    if (mRoot) visit(*mRoot, ms, false);
    return mValues;
}

void Sampler::visit(State& s, float local, bool mirrored) {
    Node const& n      = *s.node;
    float       iter   = n.iterationDuration();
    float       period = iter + n.repeatDelay;
    float       full   = n.fullDuration();
    float       u      = local - n.delay;

    auto fireCues = [&](std::vector<int> const& cues) {
        for (int c : cues) {
            if (c < 0 || c >= static_cast<int>(mEmote.sounds.size())) continue;
            auto kind = mEmote.sounds[static_cast<size_t>(c)].kind;
            mEvents.push_back({kind == SoundCue::Kind::Stop ? SoundEvent::What::StopAll : SoundEvent::What::Play, c});
        }
    };

    // Iteration starts and the completion, when crossed going forward.
    // A local time below the previous one going forward means a parent restarted this node (a
    // repeat, or a forward iteration after a yoyo): its edges are crossed again.
    float prev = local < s.prevLocal ? -1e30f : s.prevLocal;
    if (!mirrored && local > prev) {
        int last = std::max(n.repeatCount, 0);
        // Only the iterations whose start lies in (prev, local]: never loop over huge repeat counts.
        int kFrom = 0, kTo = last;
        if (period > 0) {
            kFrom = prev < n.delay ? 0 : static_cast<int>(std::min<double>(last + 1.0, std::floor((prev - n.delay) / period) + 1));
            kTo   = static_cast<int>(std::min<double>(last, std::floor((local - n.delay) / period)));
        } else {
            kTo = 0;
        }
        for (int k = kFrom; k <= kTo; ++k) {
            float edge = n.delay + static_cast<float>(k) * period;
            if (prev < edge && edge <= local) {
                if (n.endAllOnStart) mEvents.push_back({SoundEvent::What::StopAll, -1});
                fireCues(n.onStart);
            }
        }
    }
    bool completing = !mirrored && prev < full && full <= local;
    s.prevLocal     = local;

    // Position inside the node: iteration k, time w in it (clamped to the iteration's end
    // during repeat delays and after the end).
    auto position = [&](int& k, float& w) {
        if (u >= full - n.delay || period <= 0) {
            k = std::max(n.repeatCount, 0);
            w = iter;
            return;
        }
        k = static_cast<int>(std::floor(u / period));
        k = std::clamp(k, 0, std::max(n.repeatCount, 0));
        w = std::min(u - static_cast<float>(k) * period, iter);
    };

    if (n.kind == Node::Kind::Tween) {
        auto&  value  = mValues[static_cast<size_t>(n.channel)];
        auto*& owner  = mOwner[static_cast<size_t>(n.channel)];
        bool   active = u >= 0 && u <= full - n.delay;
        if (u >= 0 && !s.initialized) {
            // Also when a large step jumps over the whole tween: it still starts from the value
            // its predecessors left and ends on its target (the tween engine does the same).
            s.start       = value;
            s.initialized = true;
            active        = true;
        }
        if (s.initialized && (active || owner == &s)) {
            float p;
            int   k;
            if (u < 0) {
                k = 0;
                p = 0;
            } else {
                float w;
                position(k, w);
                p = iter > 0 ? w / iter : 1.f;
            }
            bool  reverse = n.yoyo && (k % 2) == 1;
            float e       = applyEase(n.ease, reverse ? 1 - p : p);
            value         = Affine{s.start.a * (1 - e), s.start.b * (1 - e) + n.target * e};
            if (active) owner = &s;
        }
    } else if (n.kind == Node::Kind::Timeline) {
        float inner;
        bool  childMirrored = mirrored;
        if (u < 0) {
            inner = -1;
        } else {
            int   k;
            float w;
            position(k, w);
            bool reverse  = n.yoyo && (k % 2) == 1;
            inner         = reverse ? iter - w : w;
            childMirrored = mirrored || reverse;
        }
        float offset = 0;
        for (auto& c : s.children) {
            visit(c, inner - offset, childMirrored);
            if (!n.parallel) offset += c.node->fullDuration();
        }
    }

    if (completing) {
        fireCues(n.onComplete);
        for (int c : n.endOnComplete) mEvents.push_back({SoundEvent::What::StopSection, c});
    }
}

// ---- baking --------------------------------------------------------------------------------------

namespace {

/// One keyframe of a bone: rotation offset b - w * this per axis (degrees), plus the root's
/// position offset (pixels).
struct BoneSample {
    float                t;
    std::array<float, 3> b{}, w{}, pos{};
};

bool nearLine(BoneSample const& a, BoneSample const& m, BoneSample const& c, float tolDeg) {
    float f = c.t == a.t ? 0.f : (m.t - a.t) / (c.t - a.t);
    for (int i = 0; i < 3; ++i) {
        if (std::abs(a.b[i] + (c.b[i] - a.b[i]) * f - m.b[i]) > tolDeg) return false;
        if (std::abs(a.w[i] + (c.w[i] - a.w[i]) * f - m.w[i]) > 0.01f) return false;
        if (std::abs(a.pos[i] + (c.pos[i] - a.pos[i]) * f - m.pos[i]) > 0.05f) return false;
    }
    return true;
}

bool sameSample(BoneSample const& a, BoneSample const& b) { return a.b == b.b && a.w == b.w && a.pos == b.pos; }

std::vector<BoneSample> simplify(std::vector<BoneSample> const& in, float tolDeg) {
    if (in.size() <= 2) return in;
    std::vector<BoneSample> out{in.front()};
    size_t                  anchor   = 0;
    bool                    constant = true; // every sample since the anchor equals it
    for (size_t j = anchor + 2; j < in.size(); ++j) {
        bool ok;
        if (constant && sameSample(in[anchor], in[j - 1]) && sameSample(in[anchor], in[j])) {
            ok = true;
        } else {
            constant = false;
            ok       = j - anchor < 400;
            for (size_t k = anchor + 1; ok && k < j; ++k) ok = nearLine(in[anchor], in[k], in[j], tolDeg);
        }
        if (!ok) {
            out.push_back(in[j - 1]);
            anchor   = j - 1;
            constant = sameSample(in[anchor], in[j]);
        }
    }
    out.push_back(in.back());
    return out;
}

nlohmann::ordered_json value(float b, float w) {
    double bb = round3(b);
    double ww = std::round(static_cast<double>(w) * 10000.0) / 10000.0;
    if (std::abs(ww) < 1e-4) return bb;
    std::string term = std::abs(ww - 1) < 1e-4 ? "this" : fmtNum(ww, 4) + " * this";
    if (bb == 0) return "-" + term;
    return fmtNum(bb, 3) + " - " + term;
}

} // namespace

nlohmann::ordered_json bakeAnimation(Emote const& emote, float stepMs, float toleranceDeg) {
    constexpr float kDeg = 180.f / kPi;
    float           total = emote.totalMs();
    nlohmann::ordered_json anim;
    anim["animation_length"] = std::max(0.05, round3(std::min(total, 3600000.f) / 1000.0));
    anim["blend_weight"]     = std::string(kBlendWeight);
    auto& bones              = anim["bones"] = nlohmann::ordered_json::object();
    if (!emote.root) return anim;

    bool bodyUsed = emote.usesPart(Body);
    std::array<std::vector<BoneSample>, kParts> samples;
    Sampler                                     sampler(emote);
    // Baked animations are capped at an hour (Quark's longest emote, T-Pose, is ten minutes).
    constexpr double kMaxMs = 3600000.0;
    double           span   = std::min<double>(total, kMaxMs);
    std::vector<float> times;
    double             step  = std::max(stepMs, 1.f);
    auto               steps = static_cast<size_t>(std::ceil(span / step));
    times.reserve(steps + 1);
    for (size_t i = 0; i < steps; ++i) times.push_back(static_cast<float>(static_cast<double>(i) * step));
    times.push_back(static_cast<float>(span));
    for (float t : times) {
        auto const& v = sampler.advance(t);
        std::array<float, 3> bodyB{};
        if (bodyUsed)
            for (int a = 0; a < 3; ++a) bodyB[static_cast<size_t>(a)] = v[static_cast<size_t>(Body * 3 + a)].b * kDeg;
        for (int part = 0; part < kParts; ++part) {
            bool limb = part != Body && part != Model;
            if (!emote.usesPart(part) && !(bodyUsed && limb)) continue;
            BoneSample s{t / 1000.f};
            for (int a = 0; a < 3; ++a) {
                auto const& av = v[static_cast<size_t>(part * 3 + a)];
                if (!emote.usesPart(part)) continue;
                if (part == Model) {
                    // Quark rotates the whole model from a zero start; Bedrock's model space
                    // mirrors x and y (see docs/spec/emotes.md).
                    float sign                    = a == 2 ? 1.f : -1.f;
                    s.b[static_cast<size_t>(a)]   = sign * av.b * kDeg;
                } else {
                    s.b[static_cast<size_t>(a)] = av.b * kDeg;
                    s.w[static_cast<size_t>(a)] = 1 - av.a;
                }
            }
            if (bodyUsed && limb)
                for (int a = 0; a < 3; ++a) s.b[static_cast<size_t>(a)] -= bodyB[static_cast<size_t>(a)];
            if (part == Model) {
                // Rotate around the middle of the player (Quark: half its height) instead of the
                // root bone's pivot at the feet: offset = C - R * C.
                constexpr float c  = 14.4f;
                float           rx = s.b[0] / kDeg, ry = s.b[1] / kDeg, rz = s.b[2] / kDeg;
                float           x = -c * std::sin(rz), y = c * std::cos(rz), z = 0;
                float           y2 = y * std::cos(rx) - z * std::sin(rx), z2 = y * std::sin(rx) + z * std::cos(rx);
                float           x3 = x * std::cos(ry) + z2 * std::sin(ry), z3 = -x * std::sin(ry) + z2 * std::cos(ry);
                s.pos = {-x3, c - y2, -z3};
            }
            samples[static_cast<size_t>(part)].push_back(s);
        }
    }

    for (int part = 0; part < kParts; ++part) {
        auto const& list = samples[static_cast<size_t>(part)];
        if (list.empty()) continue;
        auto  keys = simplify(list, toleranceDeg);
        auto& bone = bones[std::string(boneName(part))];
        auto& rot  = bone["rotation"] = nlohmann::ordered_json::object();
        auto* pos  = part == Model ? &(bone["position"] = nlohmann::ordered_json::object()) : nullptr;
        // Keys grow with time: append to the ordered map's vector directly (its operator[] searches
        // linearly, which makes long animations quadratic). Equal rounded times keep the last one.
        auto put = [](nlohmann::ordered_json& obj, std::string key, nlohmann::ordered_json v) {
            auto& map = obj.get_ref<nlohmann::ordered_json::object_t&>();
            if (!map.empty() && map.back().first == key) map.back().second = std::move(v);
            else map.emplace_back(std::move(key), std::move(v));
        };
        for (auto const& k : keys) {
            auto t = fmtNum(round3(k.t), 3);
            put(rot, t, nlohmann::ordered_json::array({value(k.b[0], k.w[0]), value(k.b[1], k.w[1]), value(k.b[2], k.w[2])}));
            if (pos) put(*pos, t, nlohmann::ordered_json::array({round3(k.pos[0]), round3(k.pos[1]), round3(k.pos[2])}));
        }
    }
    return anim;
}

std::string animationId(std::string_view emoteId) {
    std::string id(emoteId);
    if (id.starts_with("custom:")) id = "custom." + id.substr(7);
    return "animation.lepton.emote." + id;
}

nlohmann::ordered_json bakeAnimationFile(std::vector<std::pair<std::string, Emote const*>> const& emotes, bool withStop) {
    nlohmann::ordered_json file;
    file["format_version"] = "1.8.0";
    auto& anims            = file["animations"] = nlohmann::ordered_json::object();
    for (auto const& [id, emote] : emotes)
        if (emote) anims[animationId(id)] = bakeAnimation(*emote);
    if (withStop) anims[std::string(kStopAnimation)] = {{"animation_length", 0.05}, {"bones", nlohmann::ordered_json::object()}};
    return file;
}

} // namespace lp::feature::emotes::script
