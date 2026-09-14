#pragma once

#include "Settings/SettingsSection.h"

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
	};
}
