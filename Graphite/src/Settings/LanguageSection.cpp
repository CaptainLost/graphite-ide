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
							"opened, so that a file opens already coloured. The file you are "
							"reading always goes first.");

		// Disabled rather than hidden, the same reasoning SearchSection gives for its own
		// project-only checkbox: a control that vanishes reads as a bug, and there is nothing to
		// budget once the pass itself is off.
		ImGui::BeginDisabled(!enabled);

		int budget = static_cast<int>(m_Settings.GetBudgetMegabytes());

		if (ImGui::SliderInt("Analysis kept (MB)", &budget,
							  static_cast<int>(AnalysisSettings::kMinimumBudgetMegabytes),
							  static_cast<int>(AnalysisSettings::kMaximumBudgetMegabytes),
							  "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_Settings.SetBudgetMegabytes(static_cast<std::size_t>(budget));
		}

		ImGui::EndDisabled();
	}
}
