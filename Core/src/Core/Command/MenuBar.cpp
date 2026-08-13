#include "corepch.h"
#include "Core/Command/MenuBar.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>

#include <utility>

namespace core
{
    namespace
    {
        /// Opens one of the bar's top-level menus and gives its dropdown back the padding
        /// the theme asks for. The enlarged row padding has to be live while BeginMenu
        /// runs — it is what sizes the label to the row and what puts the dropdown
        /// directly beneath it rather than a few pixels into it — but everything
        /// submitted next belongs to the popup, where rows as tall as a title bar would
        /// be nonsense. Pushing the theme's value back inside the popup rather than
        /// popping the outer one is what ImGui allows: it compares the style stack at
        /// End() with the one at Begin(), so a pop crossing that boundary trips its own
        /// check. Pair with EndTopLevelMenu.
        bool BeginTopLevelMenu(const char* label, const ImVec2& dropdownPadding)
        {
            if (!ImGui::BeginMenu(label))
            {
                return false;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dropdownPadding);
            return true;
        }

        void EndTopLevelMenu()
        {
            ImGui::PopStyleVar();
            ImGui::EndMenu();
        }

        void RenderCommand(CommandRegistry& commands, const Keymap& keys, const std::string& id)
        {
            const Command* command = commands.Find(id);

            if (command == nullptr)
            {
                // A menu naming a command nobody registered is a wiring mistake, and
                // drawing nothing hides it. The id itself is the most useful thing to
                // show: it is what the tree names and what a search would find.
                ImGui::MenuItem(id.c_str(), nullptr, false, false);
                return;
            }

            // A label may carry a hint after a tab ("Exit\tAlt+F4") for a shortcut the
            // application does not own and cannot rebind, so no keymap entry exists to
            // advertise it. The tab has to be split off rather than passed through:
            // ImGui's text renderer handles only '\n' and '\r', so a tab draws as the
            // fallback glyph in one left-aligned run instead of reaching the right-aligned
            // shortcut column MenuItem keeps for exactly this.
            const std::size_t tab = command->label.find('\t');
            const std::string text = command->label.substr(0, tab);

            const Binding* binding = keys.For(id);
            std::string shortcut = binding != nullptr ? binding->primary.ToString() : std::string();

            // A binding wins over the hint: a command must never show two shortcut texts,
            // and the one that can actually be rebound is the truthful one.
            if (shortcut.empty() && tab != std::string::npos)
            {
                shortcut = command->label.substr(tab + 1);
            }

            const bool enabled = !command->enabled || command->enabled();
            const bool checked = command->checked && command->checked();

            // "###id" fixes the item's identity to the command, so a label that changes
            // with state does not make ImGui treat it as a different item.
            const std::string label = text + "###" + id;

            if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked,
                                 enabled))
            {
                commands.Run(id);
            }
        }

        void RenderItems(CommandRegistry& commands, const Keymap& keys,
                          const std::vector<MenuItem>& items);

        void RenderItem(CommandRegistry& commands, const Keymap& keys, const MenuItem& item)
        {
            switch (item.GetKind())
            {
                case MenuItem::Kind::Separator:
                    ImGui::Separator();
                    break;

                case MenuItem::Kind::Command:
                    RenderCommand(commands, keys, item.Text());
                    break;

                case MenuItem::Kind::Group:
                    // Copied out before drawing: RenderCommand runs commands, and one of
                    // them registering a command would otherwise resize the registry
                    // mid-iteration.
                    for (const Command* member : commands.InGroup(item.Text()))
                    {
                        RenderCommand(commands, keys, member->id);
                    }
                    break;

                case MenuItem::Kind::Submenu:
                    if (ImGui::BeginMenu(item.Text().c_str()))
                    {
                        RenderItems(commands, keys, item.Children());
                        ImGui::EndMenu();
                    }
                    break;
            }
        }

        void RenderItems(CommandRegistry& commands, const Keymap& keys,
                          const std::vector<MenuItem>& items)
        {
            for (const MenuItem& item : items)
            {
                RenderItem(commands, keys, item);
            }
        }
    }

    void MenuBar::Render(CommandRegistry& commands, const Keymap& keys,
                          const std::vector<MenuItem>& tree, float rowHeight,
                          const std::function<void()>& trailing)
    {
        // Read before anything is pushed over it: this is the padding the dropdowns are
        // entitled to, and BeginTopLevelMenu hands it back to each of them.
        const ImVec2 dropdownPadding = ImGui::GetStyle().FramePadding;

        // The host window's menu-bar height was already fixed by DockSpaceHost::Begin,
        // which popped its own override before opening the dockspace so panels dock at
        // their normal tab-bar height. The bar itself therefore sees plain default
        // padding unless pushed again here — and it has to match, or the top-level items
        // sit at the wrong height for the row and anything the caller draws in `trailing`
        // (which relies on the padding-driven text baseline BeginMenuBar sets up) lands
        // off-centre.
        const bool overridePadding = rowHeight > 0.0f;

        if (overridePadding)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                 ImVec2(dropdownPadding.x, (rowHeight - ImGui::GetFontSize()) * 0.5f));
        }

        // Not an early return on BeginMenuBar: the push above still has to be popped.
        if (ImGui::BeginMenuBar())
        {
            for (const MenuItem& item : tree)
            {
                // Only submenus can sit at the top level of a bar; anything else there is
                // a malformed tree, and drawing it as a bare item would look like a menu
                // that failed to open.
                if (item.GetKind() != MenuItem::Kind::Submenu)
                {
                    continue;
                }

                if (BeginTopLevelMenu(item.Text().c_str(), dropdownPadding))
                {
                    RenderItems(commands, keys, item.Children());
                    EndTopLevelMenu();
                }
            }

            if (trailing)
            {
                trailing();
            }

            ImGui::EndMenuBar();
        }

        if (overridePadding)
        {
            ImGui::PopStyleVar();
        }
    }
}
