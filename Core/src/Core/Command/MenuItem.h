#pragma once

#include <string>
#include <vector>

namespace Core
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

        [[nodiscard]] Kind GetKind() const noexcept { return m_Kind; }
        [[nodiscard]] const std::string& GetText() const noexcept { return m_Text; }
        [[nodiscard]] const std::vector<MenuItem>& GetChildren() const noexcept { return m_Children; }

    private:
        MenuItem() = default;

        Kind m_Kind = Kind::Separator;

        /// A command id, a group name, or a submenu label, depending on the kind.
        std::string m_Text;
        std::vector<MenuItem> m_Children;
    };
}
