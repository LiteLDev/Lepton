#pragma once

// Server forms (server thread): the fallback UI for players without the client mod
// (`net::hasClientMod(player) == false`), and simple dialogs for everyone.
//
// Thin wrappers over LeviLamina's ll::form with per-element callbacks, a close callback and a
// test seam (`forms::testing`) so selftests can answer a form that was "sent" to a
// SimulatedPlayer. Texts are sent as given: translate them with `lp::trFor(player, ...)`.
//
//     lp::game::forms::SimpleForm(trFor(p, "lp.unit.title", "Pick one"))
//         .button("Stone", [](Player& p) { ... }, "textures/blocks/stone")
//         .button("Dirt",  [](Player& p) { ... })
//         .onClose([](Player& p) { ... })
//         .sendTo(player);
//
//     lp::game::forms::CustomForm("Settings")
//         .toggle("sound", "Play sounds", true)
//         .slider("volume", "Volume", 0, 100, 1, 50)
//         .onSubmit([](Player& p, forms::Response const& r) { bool s = r.boolean("sound"); ... })
//         .sendTo(player);
//
// Only one form is open per player: sending a new one replaces the pending callbacks.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class Player;

namespace lp::game::forms {

using PlayerFn = std::function<void(Player&)>;

class SimpleForm {
public:
    explicit SimpleForm(std::string title, std::string content = {});
    SimpleForm& content(std::string text);
    /// `image`: a resource pack path ("textures/items/apple") or an http(s) URL; empty = none.
    SimpleForm& button(std::string text, PlayerFn onClick = {}, std::string image = {});
    SimpleForm& header(std::string text);
    SimpleForm& label(std::string text);
    SimpleForm& divider();
    /// The player closed the form (Esc / X) or was busy.
    SimpleForm& onClose(PlayerFn fn);
    void        sendTo(Player& player);

    struct Element {
        enum class Kind { Button, Header, Label, Divider } kind;
        std::string text;
        std::string image;
        PlayerFn    onClick;
    };

private:
    std::string          mTitle, mContent;
    std::vector<Element> mElements;
    PlayerFn             mOnClose;
};

/// Two buttons. `onYes` = upper button, `onNo` = lower button.
class ModalForm {
public:
    ModalForm(std::string title, std::string content, std::string yes, std::string no);
    ModalForm& onYes(PlayerFn fn);
    ModalForm& onNo(PlayerFn fn);
    ModalForm& onClose(PlayerFn fn);
    void       sendTo(Player& player);

private:
    std::string mTitle, mContent, mYes, mNo;
    PlayerFn    mOnYes, mOnNo, mOnClose;
};

/// Submitted values of a CustomForm, by element key.
class Response {
public:
    using Value = std::variant<std::monostate, bool, double, std::string>;
    std::map<std::string, Value> values;

    [[nodiscard]] bool        has(std::string const& key) const { return values.contains(key); }
    [[nodiscard]] bool        boolean(std::string const& key, bool fallback = false) const;
    [[nodiscard]] double      number(std::string const& key, double fallback = 0) const;
    /// Dropdown / step slider: the selected option index.
    [[nodiscard]] int         index(std::string const& key, int fallback = 0) const;
    [[nodiscard]] std::string text(std::string const& key, std::string const& fallback = {}) const;
};

class CustomForm {
public:
    explicit CustomForm(std::string title);
    CustomForm& submitText(std::string text);
    CustomForm& header(std::string text);
    CustomForm& label(std::string text);
    CustomForm& divider();
    CustomForm& input(std::string key, std::string label, std::string placeholder = {}, std::string value = {});
    CustomForm& toggle(std::string key, std::string label, bool value = false);
    CustomForm& dropdown(std::string key, std::string label, std::vector<std::string> options, int value = 0);
    CustomForm& slider(std::string key, std::string label, double min, double max, double step = 1, double value = 0);
    CustomForm& stepSlider(std::string key, std::string label, std::vector<std::string> steps, int value = 0);
    CustomForm& onSubmit(std::function<void(Player&, Response const&)> fn);
    CustomForm& onClose(PlayerFn fn);
    void        sendTo(Player& player);

    struct Element {
        enum class Kind { Header, Label, Divider, Input, Toggle, Dropdown, Slider, StepSlider } kind;
        std::string              key, text, placeholder, value;
        std::vector<std::string> options;
        double                   min = 0, max = 0, step = 1, number = 0;
        bool                     flag = false;
    };

private:
    std::string                                     mTitle, mSubmit;
    std::vector<Element>                            mElements;
    std::function<void(Player&, Response const&)>   mOnSubmit;
    PlayerFn                                        mOnClose;
};

/// Closes the player's open form (if any, also client side) without running callbacks.
void closeFor(Player& player);

/// Test seam: answer the last form sent to a player as the client would. All return false when
/// the player has no pending form of the right kind.
namespace testing {
/// Title of the pending form ("" if none).
[[nodiscard]] std::string pendingTitle(Player const& player);
/// SimpleForm: button index (counting buttons only). ModalForm: 0 = yes / upper, 1 = no / lower.
bool click(Player& player, int button);
/// CustomForm: missing keys take the element defaults.
bool submit(Player& player, Response const& response);
/// Any form: the player closes it.
bool close(Player& player);
} // namespace testing

} // namespace lp::game::forms
