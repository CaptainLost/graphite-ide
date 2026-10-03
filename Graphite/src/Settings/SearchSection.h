#pragma once

#include "Settings/SettingsSection.h"

#include <string_view>

namespace Graphite {

	class SearchSettings;

	class SearchSection final : public SettingsSection
	{
	public:
		SearchSection(SearchSettings& settings, bool& solutionLoaded);

		std::string_view GetTitle() const override { return "Search"; }

		void OnImGuiRender() override;

	private:
		SearchSettings& m_Settings;

		// Bound to a flag GraphiteLayer updates, so the section learns whether a solution is
		// loaded without reaching into Workspace itself.
		const bool& m_SolutionLoaded;
	};
}
