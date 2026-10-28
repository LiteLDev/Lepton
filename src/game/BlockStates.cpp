#include "game/BlockStates.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/level/block/Block.h"

namespace lp::game::blockstates {

namespace {
CompoundTag const* statesOf(Block const& block) {
    auto const& id = *block.mSerializationId;
    if (!id.contains("states", Tag::Type::Compound)) return nullptr;
    return &id.at("states").get<CompoundTag>();
}

std::optional<Value> toValue(CompoundTagVariant const& v) {
    switch (v.index()) {
    case Tag::Type::Byte:
        return Value{static_cast<bool>(v.get<ByteTag>().data != 0)};
    case Tag::Type::Short:
        return Value{static_cast<int>(v.get<ShortTag>().data)};
    case Tag::Type::Int:
        return Value{v.get<IntTag>().data};
    case Tag::Type::String:
        return Value{std::string(static_cast<std::string const&>(v.get<StringTag>()))};
    default:
        return std::nullopt;
    }
}

CompoundTagVariant toTag(Value const& v) {
    if (auto const* b = std::get_if<bool>(&v)) return ByteTag{static_cast<schar>(*b ? 1 : 0)};
    if (auto const* i = std::get_if<int>(&v)) return IntTag{*i};
    return StringTag{std::get<std::string>(v)};
}

/// The block whose serialization id is `block`'s with `states` (complete list). The lookup goes
/// through the NBT path (Block::tryGetFromRegistry(CompoundTag)), which knows every block type;
/// the result is checked, since an unknown combination falls back to another state.
Block const* lookup(Block const& block, std::vector<std::pair<std::string, Value>> const& states) {
    CompoundTag nbt = *block.mSerializationId;
    CompoundTag st;
    for (auto const& [k, v] : states) st[k] = toTag(v);
    nbt["states"] = std::move(st);
    auto b        = Block::tryGetFromRegistry(nbt);
    if (!b || b->getTypeName() != block.getTypeName()) return nullptr;
    for (auto const& [k, v] : states) {
        if (get(*b, k) != v) return nullptr;
    }
    return b.as_ptr();
}
} // namespace

std::vector<std::pair<std::string, Value>> all(Block const& block) {
    std::vector<std::pair<std::string, Value>> out;
    if (auto const* states = statesOf(block)) {
        for (auto const& [k, v] : *states) {
            if (auto value = toValue(v)) out.emplace_back(k, std::move(*value));
        }
    }
    return out;
}

std::optional<Value> get(Block const& block, std::string_view state) {
    auto const* states = statesOf(block);
    if (!states || !states->contains(state)) return std::nullopt;
    return toValue(states->at(state));
}

std::optional<std::string> getString(Block const& block, std::string_view state) {
    auto v = get(block, state);
    if (!v || !std::holds_alternative<std::string>(*v)) return std::nullopt;
    return std::get<std::string>(*v);
}

std::optional<int> getInt(Block const& block, std::string_view state) {
    auto v = get(block, state);
    if (!v) return std::nullopt;
    if (auto const* i = std::get_if<int>(&*v)) return *i;
    if (auto const* b = std::get_if<bool>(&*v)) return *b ? 1 : 0;
    return std::nullopt;
}

std::optional<bool> getBool(Block const& block, std::string_view state) {
    auto v = get(block, state);
    if (!v) return std::nullopt;
    if (auto const* b = std::get_if<bool>(&*v)) return *b;
    if (auto const* i = std::get_if<int>(&*v)) return *i != 0;
    return std::nullopt;
}

Block const* with(Block const& block, Changes const& changes) {
    auto states = all(block);
    for (auto const& [name, value] : changes) {
        bool found = false;
        for (auto& [k, v] : states) {
            if (k != name) continue;
            // keep the stored type: a bool state given as int (or the reverse) is converted
            if (std::holds_alternative<bool>(v) && std::holds_alternative<int>(value)) v = std::get<int>(value) != 0;
            else if (std::holds_alternative<int>(v) && std::holds_alternative<bool>(value))
                v = std::get<bool>(value) ? 1 : 0;
            else v = value;
            found = true;
            break;
        }
        if (!found) return nullptr;
    }
    return lookup(block, states);
}

Block const* of(std::string_view id, Changes const& changes) {
    auto base = Block::tryGetFromRegistry(HashedString(std::string(id)));
    if (!base) return nullptr;
    if (changes.empty()) return base.as_ptr();
    return with(*base, changes);
}

Block const* copyStates(Block const& from, Block const& to) {
    Changes changes;
    auto    target = all(to);
    for (auto& [k, v] : all(from)) {
        for (auto const& [tk, tv] : target) {
            if (tk == k && tv.index() == v.index()) {
                changes.emplace_back(k, v);
                break;
            }
        }
    }
    auto const* b = with(to, changes);
    return b ? b : &to;
}

} // namespace lp::game::blockstates
