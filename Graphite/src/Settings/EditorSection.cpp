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
		//
		// Set on release, not on every frame of the drag: a drag across the range produces a new
		// value every frame at "%.1f", and a frame that changed a setting ends by writing the whole
		// settings file (GraphiteLayer flushes AppSettings once per frame). This does mean the
		// editor's own font does not change live while dragging — it updates on release, along
		// with the file.
		float fontSize = m_Settings.GetFontSize();

		if (ImGui::SliderFloat("Font size", &fontSize, 8.0f, 32.0f, "%.1f"))
		{
			m_PendingFontSize = fontSize;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingFontSize.has_value())
		{
			m_Settings.SetFontSize(*m_PendingFontSize);
			m_PendingFontSize.reset();
		}

		RenderCommandCheckbox("Word wrap", "editor.toggleWordWrap");

		ImGui::SeparatorText("Explorer");

		RenderCommandCheckbox("Open folders to the active file", "explorer.followActiveFile");

		ImGui::SeparatorText("Git");

		// Plain checkboxes rather than commands like word wrap above: nobody toggles these while
		// typing, so they need neither a shortcut nor a menu entry.
		bool gutterChanges = m_Settings.ShowsGutterChangeMarkers();

		if (ImGui::Checkbox("Change markers in the gutter", &gutterChanges))
		{
			m_Settings.SetGutterChangeMarkers(gutterChanges);
		}

		bool scrollbarChanges = m_Settings.ShowsScrollbarChangeMarkers();

		if (ImGui::Checkbox("Change markers on the scrollbar", &scrollbarChanges))
		{
			m_Settings.SetScrollbarChangeMarkers(scrollbarChanges);
		}

		ImGui::SeparatorText("Indentation");

		bool useSpaces = m_Settings.GetIndentStyle().UseSpaces;

		if (ImGui::RadioButton("Spaces", useSpaces) && !useSpaces)
		{
			m_Settings.SetIndentWithSpaces(true);
		}

		ImGui::SameLine();

		if (ImGui::RadioButton("Tabs", !useSpaces) && useSpaces)
		{
			m_Settings.SetIndentWithSpaces(false);
		}

		// One number now governs both how wide a tab draws — expansion when drawing, selection
		// columns, indent guides, the status bar column, the syntax lexer — and how many spaces
		// Indent inserts and Outdent removes. The two were only ever split by accident, so this
		// control does something visible in both Spaces and Tabs mode.
		int width = static_cast<int>(m_Settings.GetIndentStyle().Width);

		// AlwaysClamp: Ctrl+Click turns this into a text input, and without the flag ImGui
		// does not clamp a typed value to the given range — a width the setter and Load both
		// have to defend against independently, since it reaches them the same way a
		// hand-edited graphite.json would.
		if (ImGui::SliderInt("Tab width", &width, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingIndentWidth = width;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingIndentWidth.has_value())
		{
			if (*m_PendingIndentWidth > 0)
			{
				m_Settings.SetIndentWidth(static_cast<std::size_t>(*m_PendingIndentWidth));
			}

			m_PendingIndentWidth.reset();
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
