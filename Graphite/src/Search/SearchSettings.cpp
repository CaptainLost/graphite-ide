#include "Search/SearchSettings.h"

#include "Settings/AppSettings.h"

namespace Graphite {

	void SearchSettings::Load()
	{
		m_Options.RespectGitIgnore = AppSettings::LoadSearchRespectGitIgnore();
		m_Options.ProjectsOnly = AppSettings::LoadSearchProjectsOnly();
	}

	void SearchSettings::SetRespectGitIgnore(bool respect)
	{
		m_Options.RespectGitIgnore = respect;
		AppSettings::SaveSearchRespectGitIgnore(respect);
	}

	void SearchSettings::SetProjectsOnly(bool projectsOnly)
	{
		m_Options.ProjectsOnly = projectsOnly;
		AppSettings::SaveSearchProjectsOnly(projectsOnly);
	}
}
