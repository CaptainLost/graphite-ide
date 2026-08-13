#include "Settings/EditorSection.h"

#include "Core/Command/CommandRegistry.h"
#include "Editor/EditorSettings.h"

#include <imgui.h>

namespace Graphite {

	EditorSection::EditorSection(EditorSettings& settings, Core::CommandRegistry& commands)
		: m_Settings(settings), m_Commands(commands)
	{
	}

	// Drawn from the command rather than from the value it changes. The command owns the
	// toggle, its persistence and the Checked predicate the View menu draws its own tick from;
	// a second path to the same state is a second place for the two to disagree, and the menu
	// would be the one that looked broken.
	void EditorSection::RenderCommandCheckbox(const char* label, const char* id)
	{
		const Core::Command* command = m_Commands.Find(id);

		if (command == nullptr)
		{
			return;
		}

		bool value = command->Checked && command->Checked();

		if (ImGui::Checkbox(label, &value) && command->Run)
		{
			command->Run();
		}
	}

	void EditorSection::RenderCommandRadio(const char* label, const char* id)
	{
		const Core::Command* command = m_Commands.Find(id);

		if (command == nullptr)
		{
			return;
		}

		const bool selected = command->Checked && command->Checked();

		if (ImGui::RadioButton(label, selected) && !selected && command->Run)
		{
			command->Run();
		}
	}

	void EditorSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Text");

		// A slider rather than the zoom commands: those step by a fixed amount from wherever
		// the size is, which is the right shape for a shortcut and the wrong one for a control
		// you drag. SetFontSize clamps, so a drag past either end settles on the bound.
		float fontSize = m_Settings.GetFontSize();

		if (ImGui::SliderFloat("Font size", &fontSize, 8.0f, 32.0f, "%.1f"))
		{
			m_Settings.SetFontSize(fontSize);
		}

		RenderCommandCheckbox("Word wrap", "editor.toggleWordWrap");

		ImGui::SeparatorText("Indentation");

		bool useSpaces = m_Settings.GetIndentStyle().UseSpaces;

		if (ImGui::RadioButton("Spaces", useSpaces))
		{
			m_Settings.SetIndentWithSpaces(true);
		}

		ImGui::SameLine();

		if (ImGui::RadioButton("Tabs", !useSpaces))
		{
			m_Settings.SetIndentWithSpaces(false);
		}

		int width = static_cast<int>(m_Settings.GetIndentStyle().Width);

		if (ImGui::SliderInt("Width", &width, 1, 8) && width > 0)
		{
			m_Settings.SetIndentWidth(static_cast<std::size_t>(width));
		}

		ImGui::SeparatorText("Git");

		ImGui::TextUnformatted("Ignored files");

		RenderCommandRadio("Show", "git.ignored.show");
		ImGui::SameLine();
		RenderCommandRadio("Dim", "git.ignored.dim");
		ImGui::SameLine();
		RenderCommandRadio("Hide", "git.ignored.hide");
	}
}
