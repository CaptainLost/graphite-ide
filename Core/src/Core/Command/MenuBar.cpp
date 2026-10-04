#include "corepch.h"
#include "Core/Command/MenuBar.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>

#include <utility>

namespace Core {

	namespace {
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

		// A label may carry a hint after a tab ("Exit\tAlt+F4") for a shortcut the application
		// does not own and cannot rebind, so no keymap entry exists to advertise it. The tab
		// has to be split off rather than passed through: ImGui's text renderer handles only
		// '\n' and '\r', so a tab draws as the fallback glyph in one left-aligned run instead
		// of reaching the right-aligned shortcut column MenuItem keeps for exactly this.
		std::string TextBeforeShortcutHint(const std::string& label)
		{
			return label.substr(0, label.find('\t'));
		}

		std::string ShortcutHintIn(const std::string& label)
		{
			const std::size_t tab = label.find('\t');

			return tab == std::string::npos ? std::string() : label.substr(tab + 1);
		}

		// "###id" fixes the item's identity to the command, so a label that changes with state
		// does not make ImGui treat it as a different item.
		std::string LabelWithStableId(const std::string& text, const std::string& id)
		{
			return text + "###" + id;
		}

		// A menu naming a command nobody registered is a wiring mistake, and drawing nothing
		// hides it. The id is what the tree names and what a search would find.
		void RenderUnregisteredCommand(const std::string& id)
		{
			ImGui::MenuItem(id.c_str(), nullptr, false, false);
		}

		void RenderCommand(CommandRegistry& commands, const Keymap& keys, const std::string& id)
		{
			const Command* command = commands.Find(id);

			if (command == nullptr)
			{
				RenderUnregisteredCommand(id);
				return;
			}

			const Binding* binding = keys.For(id);
			std::string shortcut = binding != nullptr ? binding->Primary.ToString() : std::string();

			// A binding wins over the hint: a command must never show two shortcut texts, and
			// the one that can actually be rebound is the truthful one.
			if (shortcut.empty())
			{
				shortcut = ShortcutHintIn(command->Label);
			}

			const bool enabled = !command->Enabled || command->Enabled();
			const bool checked = command->Checked && command->Checked();
			const std::string text = TextBeforeShortcutHint(command->Label);
			const std::string label = LabelWithStableId(
				command->Icon.empty() ? text : command->Icon + "  " + text, id);

			if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked,
								enabled))
			{
				commands.RunFromMenu(id);
			}
		}

		void RenderItem(CommandRegistry& commands, const Keymap& keys, const MenuItem& item)
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
				RenderCommand(commands, keys, item.GetText());
				break;

			case MenuItem::Kind::Group:
				// InGroup hands back a copy, so a command registered by one of the members
				// run below cannot resize the registry mid-iteration.
				for (const Command* member : commands.InGroup(item.GetText()))
				{
					RenderCommand(commands, keys, member->Id);
				}
				break;

			case MenuItem::Kind::Submenu:
				if (ImGui::BeginMenu(item.GetText().c_str()))
				{
					RenderMenuItems(commands, keys, item.GetChildren());
					ImGui::EndMenu();
				}
				break;
			}
		}
	}

	void RenderMenuItems(CommandRegistry& commands, const Keymap& keys,
						 const std::vector<MenuItem>& items)
	{
		for (const MenuItem& item : items)
		{
			RenderItem(commands, keys, item);
		}
	}

	void MenuBar::Render(CommandRegistry& commands, const Keymap& keys,
						 const std::vector<MenuItem>& tree, float rowHeight,
						 const std::function<void()>& trailing)
	{
		// Read before the override below is pushed over it: this is the padding the dropdowns
		// are entitled to, and BeginTopLevelMenu hands it back to each of them.
		const ImVec2 dropdownPadding = ImGui::GetStyle().FramePadding;

		// DockSpaceHost::Begin pops its own override before opening the dockspace, so the bar
		// sees default padding unless it is pushed again here. It has to match the row, or the
		// top-level items and whatever `trailing` draws sit off-centre in it.
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
				// Anything else at the top level is a malformed tree, and drawing it as a bare
				// item would look like a menu that failed to open.
				if (item.GetKind() != MenuItem::Kind::Submenu)
				{
					continue;
				}

				if (BeginTopLevelMenu(item.GetText().c_str(), dropdownPadding))
				{
					RenderMenuItems(commands, keys, item.GetChildren());
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