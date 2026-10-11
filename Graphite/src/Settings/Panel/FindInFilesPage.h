#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <string_view>
#include <vector>

namespace Graphite {

	class SearchSettings;

	class FindInFilesPage final : public SettingsPage
	{
	public:
		// The flag is the application's own, kept up to date as solutions open and close.
		FindInFilesPage(SearchSettings& settings, const bool& solutionLoaded) : m_Settings(settings), m_SolutionLoaded(solutionLoaded) {}

		std::string_view GetId() const override { return "findInFiles"; }

		std::string_view GetTitle() const override { return "Find in Files"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void DrawRespectGitIgnore();
		void DrawProjectsOnly();

		SearchSettings& m_Settings;
		const bool& m_SolutionLoaded;
	};
}
