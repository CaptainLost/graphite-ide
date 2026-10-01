#pragma once

#include "Search/SearchQuery.h"

namespace Graphite {

	class AppSettings;

	// One owner of the two persisted search settings. The section edits it and the panel reads
	// it; without this they would each hold their own copy and drift apart the first time one
	// of them forgot to reload.
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
