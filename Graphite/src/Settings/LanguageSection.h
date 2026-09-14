#pragma once

#include "Settings/SettingsSection.h"

#include <optional>

namespace Graphite {

	class AnalysisSettings;

	// The settings screen for everything about language servers. Today it holds the two
	// background-analysis values; the per-language binary paths belong here too — reachable now
	// only through LanguageServices::SetConfiguredBinary and a hand-edited graphite.json — which
	// is why this is its own section rather than two more rows under Editor.
	class LanguageSection final : public SettingsSection
	{
	public:
		explicit LanguageSection(AnalysisSettings& settings);

		std::string_view GetTitle() const override { return "Language"; }

		void OnImGuiRender() override;

	private:
		AnalysisSettings& m_Settings;

		// The slider's working value while it is being dragged. Committed to m_Settings only on
		// ImGui::IsItemDeactivatedAfterEdit, so a drag writes graphite.json once instead of once
		// per frame. Empty when no drag is in progress. Same reason EditorSection's font-size and
		// tab-width sliders hold their own pending value.
		std::optional<int> m_PendingBudgetMegabytes;
	};
}
