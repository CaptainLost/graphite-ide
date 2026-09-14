#include "Settings/LanguageSection.h"

#include "Settings/AnalysisSettings.h"

#include <imgui.h>

namespace Graphite {

	LanguageSection::LanguageSection(AnalysisSettings& settings) : m_Settings(settings) {}

	void LanguageSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Background analysis");

		bool enabled = m_Settings.IsEnabled();

		if (ImGui::Checkbox("Analyse the workspace in the background", &enabled))
		{
			m_Settings.SetEnabled(enabled);
		}

		ImGui::TextWrapped("Files are sent to the language server one at a time, ahead of being "
							"opened, so that a file opens already coloured. The pass waits while "
							"the file you are reading has questions of its own outstanding.");

		// Disabled rather than hidden, the same reasoning SearchSection gives for its own
		// project-only checkbox: a control that vanishes reads as a bug, and there is nothing to
		// budget once the pass itself is off.
		ImGui::BeginDisabled(!enabled);

		int budget = static_cast<int>(m_Settings.GetBudgetMegabytes());

		// Written on release, not on every frame of the drag: SetBudgetMegabytes persists through
		// AppSettings::SaveAnalysisBudgetMegabytes, which does a full JsonFile load, Set and Save,
		// and a drag across the range produces a new value every frame. Same shape EditorSection
		// uses for its font-size and tab-width sliders, and the same reason.
		if (ImGui::SliderInt("Analysis kept (MB)", &budget,
							  static_cast<int>(AnalysisSettings::kMinimumBudgetMegabytes),
							  static_cast<int>(AnalysisSettings::kMaximumBudgetMegabytes),
							  "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingBudgetMegabytes = budget;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingBudgetMegabytes.has_value())
		{
			m_Settings.SetBudgetMegabytes(static_cast<std::size_t>(*m_PendingBudgetMegabytes));
			m_PendingBudgetMegabytes.reset();
		}

		ImGui::EndDisabled();
	}
}
