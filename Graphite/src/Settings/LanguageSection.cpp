#include "Settings/LanguageSection.h"

#include "Settings/AnalysisSettings.h"

#include <imgui.h>

#include <cstdio>

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

		ImGui::TextWrapped("A few files at a time are sent to the language server, ahead of being "
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

		bool concurrencyAutomatic = m_Settings.IsConcurrencyAutomatic();

		if (ImGui::Checkbox("Set the number of files automatically", &concurrencyAutomatic))
		{
			m_Settings.SetConcurrencyAutomatic(concurrencyAutomatic);
		}

		// Disabled whenever the checkbox above is ticked, on top of the section's own condition:
		// automatic means the number is not this control's to set, not that it stops existing.
		ImGui::BeginDisabled(concurrencyAutomatic);

		int concurrency = static_cast<int>(m_Settings.GetConcurrency());

		// The label carries the automatic figure while that is what governs, so ticking the box
		// does not also make the number a mystery — a fixed "Files analysed at once" would leave
		// nothing on screen saying what "automatically" actually chose.
		char label[64];

		if (concurrencyAutomatic)
		{
			std::snprintf(label, sizeof(label), "Files analysed at once (automatic: %d)###FilesAtOnce", concurrency);
		}
		else
		{
			std::snprintf(label, sizeof(label), "Files analysed at once###FilesAtOnce");
		}

		// Written on release, not on every frame of the drag, for the same reason as the budget
		// slider above.
		if (ImGui::SliderInt(label, &concurrency,
							  static_cast<int>(AnalysisSettings::kMinimumConcurrency),
							  static_cast<int>(AnalysisSettings::kMaximumConcurrency),
							  "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingConcurrency = concurrency;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingConcurrency.has_value())
		{
			m_Settings.SetConcurrency(static_cast<std::size_t>(*m_PendingConcurrency));
			m_PendingConcurrency.reset();
		}

		ImGui::EndDisabled();

		ImGui::EndDisabled();
	}
}
