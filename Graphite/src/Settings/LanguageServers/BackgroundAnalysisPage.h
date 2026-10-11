#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	class AnalysisSettings;

	class BackgroundAnalysisPage final : public SettingsPage
	{
	public:
		explicit BackgroundAnalysisPage(AnalysisSettings& analysis) : m_Analysis(analysis) {}

		std::string_view GetId() const override { return "backgroundAnalysis"; }

		std::string_view GetTitle() const override { return "Background Analysis"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

		void OnDeactivated() override;

	private:
		void DrawEnabled();
		void DrawBudget();
		void DrawConcurrencyAutomatic();
		void DrawConcurrency();

		AnalysisSettings& m_Analysis;

		// The slider's value while it is dragged, committed on release so a drag writes graphite.json once.
		std::optional<int> m_PendingBudgetMegabytes;
	};
}
