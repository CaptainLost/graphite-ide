#pragma once

#include <string>
#include <vector>

namespace core
{
    /// One entry in a menu tree. Entries name commands by id rather than carrying
    /// behaviour, so a menu is data — which is what lets the shortcut text beside an
    /// item come from the keymap instead of from a literal nobody keeps honest.
    class MenuItem
    {
    public:
        enum class Kind
        {
            Command,
            Separator,
            Submenu,
            Group
        };

        [[nodiscard]] static MenuItem Command(std::string id);
        [[nodiscard]] static MenuItem Separator();
        [[nodiscard]] static MenuItem Submenu(std::string label, std::vector<MenuItem> children);

        /// Every command registered under `group`, in registration order. For menus whose
        /// contents are not known when the tree is built — the panel toggles, which
        /// arrive as panels are added.
        [[nodiscard]] static MenuItem Group(std::string group);

        [[nodiscard]] Kind GetKind() const noexcept { return m_kind; }
        [[nodiscard]] const std::string& Text() const noexcept { return m_text; }
        [[nodiscard]] const std::vector<MenuItem>& Children() const noexcept { return m_children; }

    private:
        MenuItem() = default;

        Kind m_kind = Kind::Separator;

        /// A command id, a group name, or a submenu label, depending on the kind.
        std::string m_text;
        std::vector<MenuItem> m_children;
    };
}
