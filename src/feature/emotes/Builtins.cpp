#include "feature/emotes/Builtins.h"

#include <map>
#include <mutex>

namespace lp::feature::emotes {

// Each script reproduces the motion of the Quark emote of the same name (parts, timings in ms,
// target angles in radians, repeats), written for Lepton in the emote script language.
// Angles: x = swing forward/back (negative raises an arm forward), y = turn, z = sideways.

namespace {

constexpr std::string_view kNo = R"(# shake the head
use head
animation sequence
  section parallel
    move head_x 200 0
    move head_y 200 0.2
    move head_z 200 0
  end
  move head_y 200 -0.2 yoyo 4 0
  move head_y 200 0
end
)";

constexpr std::string_view kYes = R"(# nod
use head
animation sequence
  section parallel
    move head_x 200 0.15
    move head_y 200 0
    move head_z 200 0
  end
  move head_x 200 -0.15 yoyo 4 0
  move head_x 200 0
end
)";

constexpr std::string_view kWave = R"(# raise the right arm and wave it sideways
use right_arm
animation sequence
  move right_arm_x 200 -2.82
  move right_arm_z 200 -0.94 yoyo 5 0
  move right_arm_x 200 0
end
)";

constexpr std::string_view kSalute = R"(# right hand to the forehead, hold, lower
use head
use right_arm
animation sequence
  section parallel
    move head_x 150 0
    move head_y 150 0
    move head_z 150 0
    move right_arm_x 150 -2.34
    move right_arm_z 150 0.4
  end
  pause 2500
  section parallel
    move right_arm_x 300 0
    move right_arm_z 300 0
  end
end
)";

constexpr std::string_view kCheer = R"(# both arms up, pumping
use right_arm
use left_arm
animation sequence
  section parallel
    move right_arm_x 200 -3.04
    move left_arm_x 200 -3.04
    move right_arm_z 200 -0.2
    move left_arm_z 200 0.2
  end
  section parallel
    move right_arm_x 150 -2.64 yoyo 11 0
    move left_arm_x 150 -2.64 yoyo 11 0
  end
  section parallel
    move right_arm_z 400 0
    move left_arm_z 400 0
    move right_arm_x 400 0
    move left_arm_x 400 0
  end
end
)";

// Quark's clap lowers left_arm_x twice in its last section (and never right_arm_y's partner
// left_arm_y): kept as is, it is the shipped behaviour.
constexpr std::string_view kClap = R"(# hands together in front, clapping
use right_arm
use left_arm
animation sequence
  section parallel
    move right_arm_x 400 -1.57
    move left_arm_x 400 -1.57
    move right_arm_y 400 -0.53
    move left_arm_y 400 0.53
  end
  section parallel
    move right_arm_y 100 -0.38 yoyo 11 0
    move left_arm_y 100 0.38 yoyo 11 0
  end
  section parallel
    move right_arm_x 400 0
    move left_arm_x 400 0
    move right_arm_y 400 0
    move left_arm_x 400 0
  end
end
)";

constexpr std::string_view kThink = R"(# hand to the chin, head tilting
use head
use right_arm
animation sequence
  section parallel
    move head_x 150 0
    move head_y 150 0
    move head_z 150 0
    move right_arm_x 400 -1.8
    move right_arm_y 400 -0.78
  end
  move head_z 200 0.2
  pause 200
  move head_z 300 0.1 yoyo 5 200
  section parallel
    move head_z 300 0
    move right_arm_x 300 0
    move right_arm_z 300 0
  end
end
)";

constexpr std::string_view kPoint = R"(# point forward, hold, lower
use right_arm
animation sequence
  move right_arm_x 400 -1.57 yoyo 1 1000
end
)";

constexpr std::string_view kShrug = R"(# arms out and up, then back
use right_arm
use left_arm
animation parallel
  move right_arm_z 400 0.7 yoyo 1 0
  move left_arm_z 400 -0.7 yoyo 1 0
  move right_arm_x 400 -0.7 yoyo 1 0
  move left_arm_x 400 -0.7 yoyo 1 0
end
)";

constexpr std::string_view kHeadbang = R"(# right arm up, head banging
use right_arm
use head
animation sequence
  section parallel
    move right_arm_x 400 -3.14
    move head_x 400 0
    move head_y 400 0
    move head_z 400 0
  end
  section parallel
    move right_arm_x 300 -1.14 yoyo 11 0
    move head_x 300 1.14 yoyo 11 0
  end
  move right_arm_x 600 0
end
)";

constexpr std::string_view kWeep = R"(# hands to the face, sobbing
use head
use left_arm
use right_arm
animation sequence
  section parallel
    move head_y 500 0
    move head_x 500 0.4
    section sequence
      section parallel
        move left_arm_x 300 -1.9
        move right_arm_x 300 -1.9
        move left_arm_y 300 0.5
        move right_arm_y 300 -0.5
      end
      pause 250
      move head_x 500 0 yoyo 1 200
      pause 300
      section sequence
        move head_x 100 0.3
        move head_x 300 0.4
        repeat 5 500
      end
      pause 1000
    end
  end
  section parallel
    move head_x 300 0
    move left_arm_x 300 0
    move left_arm_y 300 0
    move right_arm_x 300 0
    move right_arm_y 300 0
  end
end
)";

constexpr std::string_view kFacepalm = R"(# left hand to the face, head shaking slowly
use head
use left_arm
animation sequence
  section parallel
    move head_x 1000 0.2
    move head_y 1000 0
    move head_z 1000 0
    move left_arm_x 400 -2.34
    move left_arm_z 400 -1
  end
  move head_y 300 0.05
  move head_y 300 -0.05 yoyo 4 0
  move head_y 300 0
  section parallel
    move head_x 500 0
    move left_arm_x 600 0
    move left_arm_z 600 0
  end
end
)";

constexpr std::string_view kDance = R"(# arms up, swaying, kicking legs in turn
use head
use right_arm
use left_arm
use right_leg
use left_leg
tier 1
animation sequence
  section parallel
    move right_arm_x 200 -3.04
    move left_arm_x 200 -3.04
    move right_arm_z 200 -0.2
    move left_arm_z 200 0.2
    move head_x 200 0
    move head_y 200 0
    move head_z 200 0
  end
  section parallel
    move right_arm_x 200 -2.5 yoyo 7 0
    move left_arm_x 200 -2.5 yoyo 7 0
    move head_x 200 0.2 yoyo 7 0
    section sequence
      section parallel
        move right_leg_x 200 -1.6
        move right_leg_y 200 0.3
        yoyo 3 0
      end
      section parallel
        move left_leg_x 200 -1.6
        move left_leg_y 200 -0.3
        yoyo 3 0
      end
    end
    repeat 1 0
  end
  section parallel
    move right_arm_z 400 0
    move left_arm_z 400 0
    move right_arm_x 400 0
    move left_arm_x 400 0
  end
end
)";

constexpr std::string_view kTpose = R"(# arms straight out to the sides, held for ten minutes
use head
use left_arm
use right_arm
tier 1
animation sequence
  section parallel
    move left_arm_z 400 -1.57
    move right_arm_z 400 1.57
    move head_x 400 0
    move head_y 400 0
    move head_z 400 0
  end
  yoyo 1 600000
end
)";

constexpr std::string_view kDab = R"(# the dab, held for two seconds
use head
use left_arm
use right_arm
tier 1
animation sequence
  section parallel
    move left_arm_y 400 1
    move left_arm_x 400 -1.9
    move right_arm_z 400 2
    move right_arm_x 400 0.1
    move head_x 400 0.2
    move head_y 400 0.3
    move head_z 400 0
  end
  yoyo 1 2000
end
)";

constexpr std::string_view kJet = R"(# arms raised slowly, then shaking like a jet engine
use head
use left_arm
use right_arm
tier 2
animation sequence
  section parallel
    reset all all 200
  end
  section parallel
    move right_arm_x 2000 -3.12
    move left_arm_x 2000 -3.12
    move head_x 2000 -1.57
  end
  section parallel
    move right_arm_x 100 -3.16 yoyo 11 0
    move right_arm_z 80 0.03 yoyo 13 0
    move left_arm_x 100 -3.16 yoyo 11 0
    move left_arm_z 80 0.04 yoyo 13 0
    repeat 5 0
  end
  reset all all 500
end
)";

constexpr std::string_view kExorcist = R"(# the head turns all the way round, bobbing
use head
tier 2
animation parallel
  move head_y 7500 6.28
  move head_x 750 -0.05 yoyo 9 0
end
)";

constexpr std::string_view kZombie = R"(# arms forward swinging in turn, head lolling
use head
use left_arm
use right_arm
tier 2
animation parallel
  section sequence
    section parallel
      move head_x 200 0
      move head_y 200 0
    end
    move head_z 1000 0.52
    move head_z 2000 -0.52 yoyo 3 0
    move head_z 500 0
  end
  section sequence
    section parallel
      move left_arm_x 1000 -1.27
      move right_arm_x 1000 -1.87
    end
    section parallel
      move left_arm_x 1000 -1.87
      move right_arm_x 1000 -1.27
      yoyo 7 0
    end
    section parallel
      move left_arm_x 500 0
      move right_arm_x 500 0
    end
  end
end
)";

} // namespace

std::vector<BuiltinEmote> const& builtinEmotes() {
    static std::vector<BuiltinEmote> const list{
        {"no", false, kNo},
        {"yes", false, kYes},
        {"wave", false, kWave},
        {"salute", false, kSalute},
        {"cheer", false, kCheer},
        {"clap", false, kClap},
        {"think", false, kThink},
        {"point", false, kPoint},
        {"shrug", false, kShrug},
        {"headbang", false, kHeadbang},
        {"weep", false, kWeep},
        {"facepalm", false, kFacepalm},
        {"dance", true, kDance},
        {"tpose", true, kTpose},
        {"dab", true, kDab},
        {"jet", true, kJet},
        {"exorcist", true, kExorcist},
        {"zombie", true, kZombie},
    };
    return list;
}

BuiltinEmote const* findBuiltin(std::string_view id) {
    for (auto const& e : builtinEmotes())
        if (e.id == id) return &e;
    return nullptr;
}

script::Emote const* compiledBuiltin(std::string_view id) {
    static std::mutex                           mutex;
    static std::map<std::string, script::Emote> cache;
    std::lock_guard                             lock(mutex);
    if (auto it = cache.find(std::string(id)); it != cache.end()) return &it->second;
    auto const* b = findBuiltin(id);
    if (!b) return nullptr;
    return &cache.emplace(std::string(id), script::compile(b->script).emote).first->second;
}

std::vector<std::string> defaultEnabledEmotes() {
    std::vector<std::string> out;
    for (auto const& e : builtinEmotes())
        if (!e.patreon) out.emplace_back(e.id);
    return out;
}

} // namespace lp::feature::emotes
