#include "feature/enchanting/Matrix.h"

#include <algorithm>
#include <cmath>

namespace lp::feature::enchanting {

bool EnchantDef::compatibleWith(int other) const {
    return std::find(incompatible.begin(), incompatible.end(), other) == incompatible.end();
}

int maxXpFor(EnchantDef const& e, int level) { return level >= e.maxLevel ? 0 : level; }

int valueFor(EnchantDef const& e, int level) {
    int total = 1;
    for (int i = 1; i < level; ++i) total += maxXpFor(e, i);
    return total;
}

void Piece::rotate() {
    for (auto& b : blocks) {
        int x = b[0], y = b[1];
        b[0]  = y;
        b[1]  = -x;
    }
}

EnchantDef const* MatrixEnv::def(int type) const {
    if (!registry) return nullptr;
    for (auto const& d : *registry)
        if (d.type == type) return &d;
    return nullptr;
}

namespace {
bool contains(std::vector<std::string> const& v, std::string const& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int nextInt(std::mt19937& rng, int bound) {
    if (bound <= 0) return 0;
    return std::uniform_int_distribution<int>(0, bound - 1)(rng);
}

/// java.awt.Color.HSBtoRGB
int hsbToRgb(float hue, float saturation, float brightness) {
    int r = 0, g = 0, b = 0;
    if (saturation == 0) {
        r = g = b = static_cast<int>(brightness * 255.0f + 0.5f);
    } else {
        float h = (hue - std::floor(hue)) * 6.0f;
        float f = h - std::floor(h);
        float p = brightness * (1.0f - saturation);
        float q = brightness * (1.0f - saturation * f);
        float t = brightness * (1.0f - (saturation * (1.0f - f)));
        switch (static_cast<int>(h)) {
        case 0: r = int(brightness * 255 + .5f), g = int(t * 255 + .5f), b = int(p * 255 + .5f); break;
        case 1: r = int(q * 255 + .5f), g = int(brightness * 255 + .5f), b = int(p * 255 + .5f); break;
        case 2: r = int(p * 255 + .5f), g = int(brightness * 255 + .5f), b = int(t * 255 + .5f); break;
        case 3: r = int(p * 255 + .5f), g = int(q * 255 + .5f), b = int(brightness * 255 + .5f); break;
        case 4: r = int(t * 255 + .5f), g = int(p * 255 + .5f), b = int(brightness * 255 + .5f); break;
        case 5: r = int(brightness * 255 + .5f), g = int(p * 255 + .5f), b = int(q * 255 + .5f); break;
        default: break;
        }
    }
    return static_cast<int>(0xff000000u | (r << 16) | (g << 8) | b);
}
} // namespace

Matrix::Matrix(bool isBook) : book(isBook), rng(std::random_device{}()) { computeGrid(); }

std::vector<std::vector<std::array<int, 2>>> const& Matrix::shapes() {
    static std::vector<std::vector<std::array<int, 2>>> const k = {
        {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}},           // plus
        {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}, {0, -1}},         // block
        {{0, 0}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},           // U
        {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}, {1, 1}},          // S
        {{0, 0}, {-1, 0}, {1, 0}, {1, -1}, {1, 1}},           // T
        {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {1, 1}},           // twig
        {{0, 0}, {-1, 0}, {0, -1}, {-1, -1}, {1, 1}},         // squiggle
        {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}},   // fish
        {{0, 0}, {-1, 0}, {0, -1}, {-1, -1}, {-1, 1}, {1, -1}}, // stairs
        {{0, 0}, {-1, 0}, {0, -1}, {-1, -1}, {-1, 1}, {1, 1}},  // J
        {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {1, 1}},   // H
        {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-1, -1}, {1, 1}},   // odd block
    };
    return k;
}

int Matrix::colorFor(std::string const& enchantId) {
    uint32_t h = 2166136261u;
    for (unsigned char c : enchantId) h = (h ^ c) * 16777619u;
    std::mt19937                          r(h);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    float                                 hue = u(r);
    float                                 sat = u(r) * 0.2f + 0.8f;
    float                                 bri = u(r) * 0.25f + 0.75f;
    return hsbToRgb(hue, sat, bri);
}

// ---- rules -------------------------------------------------------------------------------------

bool Matrix::canGeneratePiece(MatrixEnv const& env) {
    if (env.enchantability == 0 && !env.allowZeroEnchantability && env.zeroButEnchantable) return false;
    if (!generatePiece(env, true)) return false;
    if (book) {
        if (!env.allowBooks) return false;
        int shelves  = std::max(0, std::min(env.bookshelfPower - 1, env.maxBookshelves)) / 7;
        int maxCount = env.baseMaxPieceCountBook + shelves;
        return count < maxCount;
    }
    int shelves           = std::min(env.bookshelfPower, env.maxBookshelves);
    int enchantabilityCnt = env.maxBookshelves <= 0
                              ? 0
                              : static_cast<int>(std::lround(static_cast<float>(env.enchantability)
                                                             * (static_cast<float>(shelves) / static_cast<float>(env.maxBookshelves))));
    int maxCount = env.baseMaxPieceCount + ((shelves + 1) / 2) + (enchantabilityCnt / 2);
    return count < maxCount;
}

bool Matrix::validateXp(int playerLevel, bool creative, int bookshelfPower, MatrixEnv const& env) const {
    return creative || (playerLevel >= minXpLevel(bookshelfPower, env) && playerLevel >= newPiecePrice(env));
}

int Matrix::minXpLevel(int bookshelfPower, MatrixEnv const& env) const {
    auto scale  = static_cast<float>(env.minLevelScaleFactor);
    int  cutoff = env.minLevelCutoff;
    if (book) return static_cast<int>(std::min(bookshelfPower, env.maxBookshelves) * env.minLevelScaleFactorBook);
    return count > cutoff ? (static_cast<int>(cutoff * scale) - cutoff + count) : static_cast<int>(count * scale);
}

int Matrix::newPiecePrice(MatrixEnv const& env) const {
    return 1 + (env.piecePriceScale == 0 ? 0 : count / env.piecePriceScale);
}

int Matrix::totalValue(int ench, MatrixEnv const& env) const {
    auto const* d = env.def(ench);
    if (!d) return 0;
    int total = 0;
    for (auto const& [id, p] : pieces)
        if (p.ench == ench) total += valueFor(*d, p.level) + p.xp;
    return total;
}

bool Matrix::generatePiece(MatrixEnv const& env, bool simulate) {
    if (!env.registry) return false;
    int level = book ? env.bookEnchantability + nextInt(rng, std::max(1, env.bookshelfPower) * 2) : 0;

    std::vector<Piece const*> marked;
    for (auto const& [id, p] : pieces)
        if (p.marked) marked.push_back(&p);

    struct Candidate {
        EnchantDef const* def;
        int               level;
        double            weight;
        bool              marked;
        int               influence;
    };
    std::vector<Candidate> valid;
    for (auto const& e : *env.registry) {
        bool ok = true;
        if (!e.discoverable) ok = env.allowUndiscoverable || contains(env.undiscoverableWhitelist, e.id);
        if (ok && e.treasure) ok = env.allowTreasures || (book && contains(env.treasureWhitelist, e.id));
        if (!ok || contains(env.disallowed, e.id)) continue;
        if (!book && !(env.applicable && env.applicable(e))) continue;

        int enchLevel = 1;
        if (book) {
            for (int i = e.maxLevel; i > e.minLevel - 1; --i) {
                int lo = i < static_cast<int>(e.minCost.size()) ? e.minCost[i] : 0;
                int hi = i < static_cast<int>(e.maxCost.size()) ? e.maxCost[i] : 0;
                if (level >= lo && level <= hi) {
                    enchLevel = i;
                    break;
                }
            }
        }
        int added   = valueFor(e, enchLevel);
        int current = totalValue(e.type, env);
        if (added + current > valueFor(e, e.maxLevel) + maxXpFor(e, e.maxLevel)) continue;

        Candidate c{&e, enchLevel, static_cast<double>(e.weight), false, 0};
        if (env.normalizeRarity) {
            c.weight = e.weight * 8.0;
            if (c.weight == 8) c.weight = 5;
            else if (c.weight == 16) c.weight = 25;
            auto it     = env.influences.find(e.type);
            c.influence = std::clamp(it == env.influences.end() ? 0 : it->second, -env.influenceMax, env.influenceMax);
            c.weight *= 1.0f + c.influence * static_cast<float>(env.influencePower);
            bool mark = true;
            for (auto const* other : marked) {
                if (other->ench == e.type) {
                    c.weight *= env.dupeMultiplier;
                    mark = false;
                    break;
                }
                auto const* od = env.def(other->ench);
                if ((od && !od->compatibleWith(e.type)) || !e.compatibleWith(other->ench)) {
                    c.weight *= env.incompatibleMultiplier;
                    mark = false;
                    break;
                }
            }
            c.marked = mark;
        }
        valid.push_back(c);
    }
    if (valid.empty()) return false;

    int total = 0;
    for (auto const& c : valid) total = static_cast<int>(total + c.weight);
    if (total == 0)
        for (auto& c : valid) c.weight += 1;

    int weightSum = 0;
    for (auto const& c : valid) weightSum += static_cast<int>(c.weight);
    if (weightSum <= 0) return false;
    int              roll   = nextInt(rng, weightSum);
    Candidate const* chosen = nullptr;
    for (auto const& c : valid) {
        roll -= static_cast<int>(c.weight);
        if (roll < 0) {
            chosen = &c;
            break;
        }
    }
    if (!chosen) return false;
    if (simulate) return true;

    if (auto it = env.influences.find(chosen->def->type); it != env.influences.end() && it->second > 0) influenced = true;

    int type = -1;
    for (auto const& [id, p] : pieces)
        if (p.ench == chosen->def->type) type = p.type;
    if (type == -1) type = typeCount++ % kPieceVariants;

    Piece p;
    p.ench      = chosen->def->type;
    p.level     = chosen->level;
    p.marked    = chosen->marked;
    p.influence = chosen->influence;
    p.type      = type;
    p.color     = colorFor(chosen->def->id);
    auto const& all = shapes();
    p.blocks        = all[nextInt(rng, static_cast<int>(all.size()))];
    int rotations   = nextInt(rng, 4);
    for (int i = 0; i < rotations; ++i) p.rotate();

    pieces[count] = std::move(p);
    benched.insert(benched.begin(), count);
    ++count;
    if (book && count == 1)
        for (int i = 0; i < 2; ++i)
            if (nextInt(rng, 2) == 1) ++count;
    return true;
}

// ---- operations --------------------------------------------------------------------------------

Piece const* Matrix::piece(int id) const {
    auto it = pieces.find(id);
    return it == pieces.end() ? nullptr : &it->second;
}

bool Matrix::canPlace(Piece const& p, int x, int y) const {
    for (auto const& b : p.blocks) {
        int bx = b[0] + x, by = b[1] + y;
        if (bx < 0 || by < 0 || bx >= kWidth || by >= kHeight) return false;
        if (grid[bx][by] != -1) return false;
    }
    return true;
}

static bool has(std::vector<int> const& v, int id) { return std::find(v.begin(), v.end(), id) != v.end(); }
static void erase(std::vector<int>& v, int id) { v.erase(std::remove(v.begin(), v.end(), id), v.end()); }

bool Matrix::place(int id, int x, int y) {
    auto it = pieces.find(id);
    if (it == pieces.end() || !has(benched, id) || !canPlace(it->second, x, y)) return false;
    it->second.x = x;
    it->second.y = y;
    erase(benched, id);
    placed.push_back(id);
    computeGrid();
    return true;
}

bool Matrix::remove(int id) {
    if (!pieces.contains(id) || !has(placed, id)) return false;
    erase(placed, id);
    benched.push_back(id);
    computeGrid();
    return true;
}

bool Matrix::rotate(int id) {
    auto it = pieces.find(id);
    if (it == pieces.end() || !has(benched, id)) return false;
    it->second.rotate();
    return true;
}

bool Matrix::merge(int placedId, int benchedId, MatrixEnv const& env) {
    auto pi = pieces.find(placedId);
    auto hi = pieces.find(benchedId);
    if (pi == pieces.end() || hi == pieces.end() || placedId == benchedId) return false;
    if (!has(placed, placedId) || !has(benched, benchedId)) return false;
    auto&       target = pi->second;
    auto const& source = hi->second;
    auto const* d      = env.def(target.ench);
    if (!d || source.ench != target.ench || target.level >= d->maxLevel) return false;
    target.xp += valueFor(*d, source.level) + source.xp;
    int max = maxXpFor(*d, target.level);
    while (target.xp >= max) {
        if (target.level >= d->maxLevel) break;
        ++target.level;
        target.xp -= max;
        max = maxXpFor(*d, target.level);
    }
    if (source.marked) target.marked = true;
    erase(benched, benchedId);
    pieces.erase(hi);
    return true;
}

std::map<int, int> Matrix::result(MatrixEnv const& env, bool& ok) const {
    std::map<int, int> out;
    ok = true;
    for (int id : placed) {
        auto const* p = piece(id);
        if (!p) continue;
        auto const* d = env.def(p->ench);
        for (auto const& [other, lvl] : out) {
            auto const* od = env.def(other);
            if (other == p->ench || (d && !d->compatibleWith(other)) || (od && !od->compatibleWith(p->ench))) {
                ok = false;
                return {};
            }
        }
        out[p->ench] = p->level;
    }
    return out;
}

void Matrix::computeGrid() {
    for (auto& col : grid) col.fill(-1);
    for (int id : placed) {
        auto const* p = piece(id);
        if (!p) continue;
        for (auto const& b : p->blocks) {
            int bx = p->x + b[0], by = p->y + b[1];
            if (bx >= 0 && by >= 0 && bx < kWidth && by < kHeight) grid[bx][by] = id;
        }
    }
}

// ---- JSON --------------------------------------------------------------------------------------

nlohmann::json Matrix::toJson() const {
    nlohmann::json j;
    auto&          arr = j["pieces"] = nlohmann::json::array();
    for (auto const& [id, p] : pieces) {
        nlohmann::json blocks = nlohmann::json::array();
        for (auto const& b : p.blocks) blocks.push_back({b[0], b[1]});
        arr.push_back({
            {"id",        id         },
            {"enchant",   p.ench     },
            {"level",     p.level    },
            {"color",     p.color    },
            {"type",      p.type     },
            {"x",         p.x        },
            {"y",         p.y        },
            {"xp",        p.xp       },
            {"marked",    p.marked   },
            {"influence", p.influence},
            {"blocks",    blocks     },
        });
    }
    j["benchedPieces"] = benched;
    j["placedPieces"]  = placed;
    j["count"]         = count;
    j["typeCount"]     = typeCount;
    j["influenced"]    = influenced;
    return j;
}

Matrix Matrix::fromJson(nlohmann::json const& j, bool isBook) {
    Matrix m(isBook);
    if (!j.is_object()) return m;
    try {
        for (auto const& pj : j.value("pieces", nlohmann::json::array())) {
            Piece p;
            p.ench      = pj.value("enchant", -1);
            p.level     = pj.value("level", 1);
            p.color     = pj.value("color", 0);
            p.type      = pj.value("type", 0);
            p.x         = pj.value("x", 0);
            p.y         = pj.value("y", 0);
            p.xp        = pj.value("xp", 0);
            p.marked    = pj.value("marked", false);
            p.influence = pj.value("influence", 0);
            for (auto const& b : pj.value("blocks", nlohmann::json::array()))
                if (b.is_array() && b.size() == 2) p.blocks.push_back({b[0].get<int>(), b[1].get<int>()});
            m.pieces[pj.value("id", 0)] = std::move(p);
        }
        for (auto const& v : j.value("benchedPieces", nlohmann::json::array()))
            if (m.pieces.contains(v.get<int>())) m.benched.push_back(v.get<int>());
        for (auto const& v : j.value("placedPieces", nlohmann::json::array()))
            if (m.pieces.contains(v.get<int>())) m.placed.push_back(v.get<int>());
        m.count      = j.value("count", 0);
        m.typeCount  = j.value("typeCount", 0);
        m.influenced = j.value("influenced", false);
    } catch (...) {
        return Matrix(isBook);
    }
    m.computeGrid();
    return m;
}

} // namespace lp::feature::enchanting
