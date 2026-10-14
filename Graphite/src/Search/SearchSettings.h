#pragma once

#include "Search/SearchQuery.h"

namespace Graphite {

	class AppSettings;

	class SearchSettings
	{
	public:
		explicit SearchSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		const SearchOptions& Get() const { return m_Options; }

		void SetRespectGitIgnore(bool respect);
		void SetProjectsOnly(bool projectsOnly);

	private:
		AppSettings& m_Store;
		SearchOptions m_Options;
	};
}
