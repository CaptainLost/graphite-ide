#include "Settings/LanguageServers/BackgroundAnalysisPage.h"

#include "Settings/AnalysisSettings.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/ImGui/Toggle.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>

namespace Graphite {

	const char* BackgroundAnalysisPage::GetIcon() const
	{
		return ICON_MDI_PROGRESS_CLOCK;
	}

	void BackgroundAnalysisPage::OnDeactivated()
	{
		m_PendingBudgetMegabytes.reset();
	}

	std::vector<SettingsCard> BackgroundAnalysisPage::Describe()
	{
		const std::string concurrencyDescription = m_Analysis.IsConcurrencyAutomatic()
													   ? std::format("Automatic: {}.", m_Analysis.GetConcurrency())
													   : std::string{"How many files are sent to the server together."};
		SettingsCard card;

		card.Rows.push_back(SettingsRow{.Title = "Analyse in the background",
										.Description = "Sends a few files at a time to the language server before you open them, so they open "
													   "already coloured. Waits while the file you are reading has questions of its own outstanding.",
										.Keywords = {"analysis", "index", "language server"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawEnabled(); }});
		card.Rows.push_back(SettingsRow{.Title = "Analysis kept",
										.Description = "Past this, the analysis read longest ago is dropped.",
										.Keywords = {"memory", "budget", "cache", "MB"},
										.ControlWidth = SettingsLayout::SliderWidth(),
										.DrawControl = [this] { DrawBudget(); }});
		card.Rows.push_back(SettingsRow{.Title = "Set the number of files automatically",
										.Keywords = {"concurrency"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawConcurrencyAutomatic(); }});
		card.Rows.push_back(SettingsRow{.Title = "Files analysed at once",
										.Description = concurrencyDescription,
										.Keywords = {"concurrency"},
										.ControlWidth = SettingsLayout::NumberWidth(),
										.DrawControl = [this] { DrawConcurrency(); }});

		return {card};
	}

	void BackgroundAnalysisPage::DrawEnabled()
	{
		bool enabled = m_Analysis.IsEnabled();

		if (Core::Toggle("##analyseInBackground", &enabled))
		{
			m_Analysis.SetEnabled(enabled);
		}
	}

	void BackgroundAnalysisPage::DrawBudget()
	{
		ImGui::BeginDisabled(!m_Analysis.IsEnabled());

		int budget = m_PendingBudgetMegabytes.value_or(static_cast<int>(m_Analysis.GetBudgetMegabytes()));

		if (ImGui::SliderInt("##analysisKept", &budget, static_cast<int>(AnalysisSettings::kMinimumBudgetMegabytes),
							 static_cast<int>(AnalysisSettings::kMaximumBudgetMegabytes), "%d MB", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingBudgetMegabytes = budget;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingBudgetMegabytes.has_value())
		{
			m_Analysis.SetBudgetMegabytes(static_cast<std::size_t>(*m_PendingBudgetMegabytes));
			m_PendingBudgetMegabytes.reset();
		}

		ImGui::EndDisabled();
	}

	void BackgroundAnalysisPage::DrawConcurrencyAutomatic()
	{
		ImGui::BeginDisabled(!m_Analysis.IsEnabled());

		bool automatic = m_Analysis.IsConcurrencyAutomatic();

		if (Core::Toggle("##concurrencyAutomatic", &automatic))
		{
			m_Analysis.SetConcurrencyAutomatic(automatic);
		}

		ImGui::EndDisabled();
	}

	void BackgroundAnalysisPage::DrawConcurrency()
	{
		ImGui::BeginDisabled(!m_Analysis.IsEnabled() || m_Analysis.IsConcurrencyAutomatic());

		int concurrency = static_cast<int>(m_Analysis.GetConcurrency());

		if (ImGui::InputInt("##filesAtOnce", &concurrency))
		{
			const int clamped = std::clamp(concurrency, static_cast<int>(AnalysisSettings::kMinimumConcurrency),
										   static_cast<int>(AnalysisSettings::kMaximumConcurrency));
			m_Analysis.SetConcurrency(static_cast<std::size_t>(clamped));
		}

		ImGui::EndDisabled();
	}
}
