#include "corepch.h"
#include "Core/Command/MenuBar.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>

#include <utility>

namespace Core {

	namespace {
		constexpr char kShortcutHintSeparator = '\t';
		constexpr const char* kIconGap = "  ";
		constexpr const char* kImGuiIdSeparator = "###";

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

		// A label may end in a tab and a hint for a shortcut the application does not own ("Exit\tAlt+F4").
		// ImGui's text renderer handles only '\n' and '\r', so a tab left in the label draws as the
		// fallback glyph instead of reaching the right-aligned shortcut column MenuItem keeps for it.
		std::string TextBeforeShortcutHint(const std::string& label)
		{
			return label.substr(0, label.find(kShortcutHintSeparator));
		}

		std::string ShortcutHintOf(const std::string& label)
		{
			const std::size_t separatorPosition = label.find(kShortcutHintSeparator);

			if (separatorPosition == std::string::npos)
			{
				return std::string();
			}

			return label.substr(separatorPosition + 1);
		}

		// What follows "###" is the item's ImGui identity, so a label that changes with state
		// stays the same item.
		std::string LabelWithStableId(const std::string& text, const std::string& id)
		{
			return text + kImGuiIdSeparator + id;
		}

		std::string WithIcon(const std::string& text, const std::string& icon)
		{
			if (icon.empty())
			{
				return text;
			}

			return icon + kIconGap + text;
		}

		std::string PrimaryChordTextOf(const Binding* binding)
		{
			if (binding == nullptr)
			{
				return std::string();
			}

			return binding->Primary.ToString();
		}

		// Drawn disabled under the id the tree names rather than skipped: a menu naming a command
		// nobody registered is a wiring mistake, and drawing nothing would hide it.
		void RenderUnregisteredCommand(const std::string& id)
		{
			const char* const noShortcut = nullptr;
			const bool selected = false;
			const bool enabled = false;

			ImGui::MenuItem(id.c_str(), noShortcut, selected, enabled);
		}

		void RenderCommand(CommandRegistry& commands, const Keymap& keymap, const std::string& id)
		{
			const Command* command = commands.Find(id);

			if (command == nullptr)
			{
				RenderUnregisteredCommand(id);
				return;
			}

			// A bound chord wins over the label's hint: it is the shortcut the user can rebind, and
			// a command must never show two.
			std::string shortcut = PrimaryChordTextOf(keymap.For(id));

			if (shortcut.empty())
			{
				shortcut = ShortcutHintOf(command->Label);
			}

			const bool enabled = IsCommandEnabled(*command);
			const bool checked = IsCommandChecked(*command);
			const std::string text = TextBeforeShortcutHint(command->Label);
			const std::string label = LabelWithStableId(WithIcon(text, command->Icon), id);
			const char* shortcutOrNothing = shortcut.empty() ? nullptr : shortcut.c_str();

			if (ImGui::MenuItem(label.c_str(), shortcutOrNothing, checked, enabled))
			{
				commands.RunFromMenu(id);
			}
		}

		void RenderItem(CommandRegistry& commands, const Keymap& keymap, const MenuItem& item)
		{
			switch (item.GetKind())
			{
			case MenuItem::Kind::Separator:
				ImGui::Separator();
				break;

			case MenuItem::Kind::Section:
				ImGui::SeparatorText(item.GetText().c_str());
				break;

			case MenuItem::Kind::Command:
				RenderCommand(commands, keymap, item.GetText());
				break;

			case MenuItem::Kind::Group:
			{
				// A copy of the members rather than a view into the registry, because a command run from this loop may register another and resize the registry.
				const std::vector<const Command*> members = commands.InGroup(item.GetText());

				for (const Command* member : members)
				{
					RenderCommand(commands, keymap, member->Id);
				}
				break;
			}

			case MenuItem::Kind::Submenu:
				if (ImGui::BeginMenu(item.GetText().c_str()))
				{
					RenderMenuItems(commands, keymap, item.GetChildren());
					ImGui::EndMenu();
				}
				break;
			}
		}
	}

	void RenderMenuItems(CommandRegistry& commands, const Keymap& keymap,
						 const std::vector<MenuItem>& items)
	{
		for (const MenuItem& item : items)
		{
			RenderItem(commands, keymap, item);
		}
	}

	void MenuBar::Render(CommandRegistry& commands, const Keymap& keymap,
						 const std::vector<MenuItem>& menuTree, float rowHeight,
						 const std::function<void()>& drawLeading, const std::function<void()>& drawTrailing)
	{
		// Read before the bar's own padding is pushed over it: BeginTopLevelMenu hands this
		// padding back to each dropdown.
		const ImVec2 dropdownPadding = ImGui::GetStyle().FramePadding;

		// DockSpaceHost::Begin pops its own padding before opening the dockspace, so the bar has
		// to push one that centres its items, and what drawLeading and drawTrailing draw, in the row.
		const bool rowHeightGiven = rowHeight > 0.0f;

		if (rowHeightGiven)
		{
			const float paddingThatCentresText = (rowHeight - ImGui::GetFontSize()) * 0.5f;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dropdownPadding.x, paddingThatCentresText));
		}

		// Not an early return on BeginMenuBar: the bar's padding still has to be popped.
		if (ImGui::BeginMenuBar())
		{
			if (drawLeading)
			{
				drawLeading();
			}

			for (const MenuItem& item : menuTree)
			{
				// Only a submenu belongs at the top level: a bare item there would look like a
				// menu that failed to open.
				const bool isTopLevelMenu = item.GetKind() == MenuItem::Kind::Submenu;
				if (!isTopLevelMenu)
				{
					continue;
				}

				if (BeginTopLevelMenu(item.GetText().c_str(), dropdownPadding))
				{
					RenderMenuItems(commands, keymap, item.GetChildren());
					EndTopLevelMenu();
				}
			}

			if (drawTrailing)
			{
				drawTrailing();
			}

			ImGui::EndMenuBar();
		}

		if (rowHeightGiven)
		{
			ImGui::PopStyleVar();
		}
	}
}
