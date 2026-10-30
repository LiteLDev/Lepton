#include "game/Forms.h"

#include <unordered_map>

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/ModalForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include "mod/Log.h"

namespace lp::game::forms {

namespace {

/// What the client may answer to the pending form of a player.
struct Pending {
    uint64_t    serial = 0;
    std::string title;
    enum class Kind { Simple, Modal, Custom } kind = Kind::Simple;
    /// Simple / Modal: button index, or -1 = closed.
    std::function<void(Player&, int)> onButton;
    /// Custom: response, or nullopt = closed.
    std::function<void(Player&, std::optional<Response> const&)> onCustom;
    std::vector<CustomForm::Element>                             elements; ///< Custom: defaults for testing::submit
};

std::unordered_map<std::string, Pending>& pending() {
    static std::unordered_map<std::string, Pending> m;
    return m;
}
uint64_t gSerial = 0;

std::string keyOf(Player const& p) { return p.getUuid().asString(); }

uint64_t remember(Player const& p, Pending entry) {
    entry.serial      = ++gSerial;
    auto serial       = entry.serial;
    pending()[keyOf(p)] = std::move(entry);
    return serial;
}

/// Removes and returns the pending form if it is still `serial` (0 = any).
std::optional<Pending> take(Player const& p, uint64_t serial) {
    auto it = pending().find(keyOf(p));
    if (it == pending().end() || (serial && it->second.serial != serial)) return std::nullopt;
    auto entry = std::move(it->second);
    pending().erase(it);
    return entry;
}

void runSafely(char const* what, std::function<void()> const& fn) {
    try {
        fn();
    } catch (std::exception const& e) {
        log().error("form {} callback failed: {}", what, e.what());
    }
}

Response convert(std::vector<CustomForm::Element> const& elements, ll::form::CustomFormResult const& raw) {
    Response r;
    if (!raw) return r;
    for (auto const& e : elements) {
        auto it = raw->find(e.key);
        if (e.key.empty() || it == raw->end()) continue;
        auto const& v = it->second;
        double      num = 0;
        if (auto const* u = std::get_if<uint64>(&v)) num = static_cast<double>(*u);
        else if (auto const* d = std::get_if<double>(&v)) num = *d;
        switch (e.kind) {
        case CustomForm::Element::Kind::Input:
            if (auto const* s = std::get_if<std::string>(&v)) r.values[e.key] = *s;
            break;
        case CustomForm::Element::Kind::Toggle:
            r.values[e.key] = num != 0;
            break;
        default:
            r.values[e.key] = num;
            break;
        }
    }
    return r;
}

Response defaults(std::vector<CustomForm::Element> const& elements) {
    Response r;
    for (auto const& e : elements) {
        using K = CustomForm::Element::Kind;
        switch (e.kind) {
        case K::Input:
            r.values[e.key] = e.value;
            break;
        case K::Toggle:
            r.values[e.key] = e.flag;
            break;
        case K::Dropdown:
        case K::StepSlider:
        case K::Slider:
            r.values[e.key] = e.number;
            break;
        default:
            break;
        }
    }
    return r;
}

} // namespace

// ---- SimpleForm ----------------------------------------------------------------------------------

SimpleForm::SimpleForm(std::string title, std::string content) : mTitle(std::move(title)), mContent(std::move(content)) {}
SimpleForm& SimpleForm::content(std::string text) {
    mContent = std::move(text);
    return *this;
}
SimpleForm& SimpleForm::button(std::string text, PlayerFn onClick, std::string image) {
    mElements.push_back({Element::Kind::Button, std::move(text), std::move(image), std::move(onClick)});
    return *this;
}
SimpleForm& SimpleForm::header(std::string text) {
    mElements.push_back({Element::Kind::Header, std::move(text), {}, {}});
    return *this;
}
SimpleForm& SimpleForm::label(std::string text) {
    mElements.push_back({Element::Kind::Label, std::move(text), {}, {}});
    return *this;
}
SimpleForm& SimpleForm::divider() {
    mElements.push_back({Element::Kind::Divider, {}, {}, {}});
    return *this;
}
SimpleForm& SimpleForm::onClose(PlayerFn fn) {
    mOnClose = std::move(fn);
    return *this;
}

void SimpleForm::sendTo(Player& player) {
    std::vector<PlayerFn> buttons;
    ll::form::SimpleForm  form(mTitle, mContent);
    for (auto const& e : mElements) {
        switch (e.kind) {
        case Element::Kind::Button:
            buttons.push_back(e.onClick);
            if (e.image.empty()) form.appendButton(e.text);
            else form.appendButton(e.text, e.image, e.image.starts_with("http") ? "url" : "path");
            break;
        case Element::Kind::Header:
            form.appendHeader(e.text);
            break;
        case Element::Kind::Label:
            form.appendLabel(e.text);
            break;
        case Element::Kind::Divider:
            form.appendDivider();
            break;
        }
    }
    auto onClose = mOnClose;
    auto serial  = remember(
        player,
        Pending{
            0,
            mTitle,
            Pending::Kind::Simple,
            [buttons = std::move(buttons), onClose](Player& p, int index) {
                if (index >= 0 && index < static_cast<int>(buttons.size())) {
                    if (buttons[index]) runSafely("button", [&] { buttons[index](p); });
                } else if (onClose) {
                    runSafely("close", [&] { onClose(p); });
                }
            },
            {},
            {},
        }
    );
    form.sendTo(player, [serial](Player& p, int index, ll::form::FormCancelReason reason) {
        auto entry = take(p, serial);
        if (!entry) return;
        entry->onButton(p, reason ? -1 : index);
    });
}

// ---- ModalForm -----------------------------------------------------------------------------------

ModalForm::ModalForm(std::string title, std::string content, std::string yes, std::string no)
: mTitle(std::move(title)),
  mContent(std::move(content)),
  mYes(std::move(yes)),
  mNo(std::move(no)) {}
ModalForm& ModalForm::onYes(PlayerFn fn) {
    mOnYes = std::move(fn);
    return *this;
}
ModalForm& ModalForm::onNo(PlayerFn fn) {
    mOnNo = std::move(fn);
    return *this;
}
ModalForm& ModalForm::onClose(PlayerFn fn) {
    mOnClose = std::move(fn);
    return *this;
}

void ModalForm::sendTo(Player& player) {
    auto serial = remember(
        player,
        Pending{
            0,
            mTitle,
            Pending::Kind::Modal,
            [yes = mOnYes, no = mOnNo, close = mOnClose](Player& p, int index) {
                auto const& fn = index == 0 ? yes : index == 1 ? no : close;
                if (fn) runSafely("modal", [&] { fn(p); });
            },
            {},
            {},
        }
    );
    ll::form::ModalForm form(mTitle, mContent, mYes, mNo);
    form.sendTo(player, [serial](Player& p, ll::form::ModalFormResult result, ll::form::FormCancelReason) {
        auto entry = take(p, serial);
        if (!entry) return;
        int index = !result ? -1 : (*result == ll::form::ModalFormSelectedButton::Upper ? 0 : 1);
        entry->onButton(p, index);
    });
}

// ---- CustomForm ----------------------------------------------------------------------------------

bool Response::boolean(std::string const& key, bool fallback) const {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (auto const* b = std::get_if<bool>(&it->second)) return *b;
    if (auto const* d = std::get_if<double>(&it->second)) return *d != 0;
    return fallback;
}
double Response::number(std::string const& key, double fallback) const {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (auto const* d = std::get_if<double>(&it->second)) return *d;
    if (auto const* b = std::get_if<bool>(&it->second)) return *b ? 1 : 0;
    return fallback;
}
int Response::index(std::string const& key, int fallback) const {
    return static_cast<int>(number(key, static_cast<double>(fallback)));
}
std::string Response::text(std::string const& key, std::string const& fallback) const {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (auto const* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}

CustomForm::CustomForm(std::string title) : mTitle(std::move(title)) {}
CustomForm& CustomForm::submitText(std::string text) {
    mSubmit = std::move(text);
    return *this;
}
CustomForm& CustomForm::header(std::string text) {
    mElements.push_back({Element::Kind::Header, {}, std::move(text)});
    return *this;
}
CustomForm& CustomForm::label(std::string text) {
    mElements.push_back({Element::Kind::Label, {}, std::move(text)});
    return *this;
}
CustomForm& CustomForm::divider() {
    mElements.push_back({Element::Kind::Divider});
    return *this;
}
CustomForm& CustomForm::input(std::string key, std::string label, std::string placeholder, std::string value) {
    mElements.push_back({Element::Kind::Input, std::move(key), std::move(label), std::move(placeholder), std::move(value)});
    return *this;
}
CustomForm& CustomForm::toggle(std::string key, std::string label, bool value) {
    Element e{Element::Kind::Toggle, std::move(key), std::move(label)};
    e.flag = value;
    mElements.push_back(std::move(e));
    return *this;
}
CustomForm& CustomForm::dropdown(std::string key, std::string label, std::vector<std::string> options, int value) {
    Element e{Element::Kind::Dropdown, std::move(key), std::move(label)};
    e.options = std::move(options);
    e.number  = value;
    mElements.push_back(std::move(e));
    return *this;
}
CustomForm& CustomForm::slider(std::string key, std::string label, double min, double max, double step, double value) {
    Element e{Element::Kind::Slider, std::move(key), std::move(label)};
    e.min    = min;
    e.max    = max;
    e.step   = step;
    e.number = value;
    mElements.push_back(std::move(e));
    return *this;
}
CustomForm& CustomForm::stepSlider(std::string key, std::string label, std::vector<std::string> steps, int value) {
    Element e{Element::Kind::StepSlider, std::move(key), std::move(label)};
    e.options = std::move(steps);
    e.number  = value;
    mElements.push_back(std::move(e));
    return *this;
}
CustomForm& CustomForm::onSubmit(std::function<void(Player&, Response const&)> fn) {
    mOnSubmit = std::move(fn);
    return *this;
}
CustomForm& CustomForm::onClose(PlayerFn fn) {
    mOnClose = std::move(fn);
    return *this;
}

void CustomForm::sendTo(Player& player) {
    ll::form::CustomForm form(mTitle);
    if (!mSubmit.empty()) form.setSubmitButton(mSubmit);
    for (auto const& e : mElements) {
        using K = Element::Kind;
        switch (e.kind) {
        case K::Header:
            form.appendHeader(e.text);
            break;
        case K::Label:
            form.appendLabel(e.text);
            break;
        case K::Divider:
            form.appendDivider();
            break;
        case K::Input:
            form.appendInput(e.key, e.text, e.placeholder, e.value);
            break;
        case K::Toggle:
            form.appendToggle(e.key, e.text, e.flag);
            break;
        case K::Dropdown:
            form.appendDropdown(e.key, e.text, e.options, static_cast<size_t>(e.number));
            break;
        case K::Slider:
            form.appendSlider(e.key, e.text, e.min, e.max, e.step, e.number);
            break;
        case K::StepSlider:
            form.appendStepSlider(e.key, e.text, e.options, static_cast<size_t>(e.number));
            break;
        }
    }
    auto serial = remember(
        player,
        Pending{
            0,
            mTitle,
            Pending::Kind::Custom,
            {},
            [submit = mOnSubmit, close = mOnClose](Player& p, std::optional<Response> const& r) {
                if (r) {
                    if (submit) runSafely("submit", [&] { submit(p, *r); });
                } else if (close) {
                    runSafely("close", [&] { close(p); });
                }
            },
            mElements,
        }
    );
    form.sendTo(player, [serial](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason reason) {
        auto entry = take(p, serial);
        if (!entry) return;
        if (!result || reason) entry->onCustom(p, std::nullopt);
        else entry->onCustom(p, convert(entry->elements, result));
    });
}

void closeFor(Player& player) {
    take(player, 0);
    if (player.isSimulated()) return;
    if (auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::ClientboundCloseScreen)) pkt->sendTo(player);
}

namespace testing {

std::string pendingTitle(Player const& player) {
    auto it = pending().find(keyOf(player));
    return it == pending().end() ? std::string{} : it->second.title;
}

bool click(Player& player, int button) {
    auto it = pending().find(keyOf(player));
    if (it == pending().end() || it->second.kind == Pending::Kind::Custom) return false;
    auto entry = take(player, 0);
    entry->onButton(player, button);
    return true;
}

bool submit(Player& player, Response const& response) {
    auto it = pending().find(keyOf(player));
    if (it == pending().end() || it->second.kind != Pending::Kind::Custom) return false;
    auto     entry = take(player, 0);
    Response full  = defaults(entry->elements);
    for (auto const& [k, v] : response.values) full.values[k] = v;
    entry->onCustom(player, full);
    return true;
}

bool close(Player& player) {
    auto entry = take(player, 0);
    if (!entry) return false;
    if (entry->kind == Pending::Kind::Custom) entry->onCustom(player, std::nullopt);
    else entry->onButton(player, -1);
    return true;
}

} // namespace testing

} // namespace lp::game::forms
