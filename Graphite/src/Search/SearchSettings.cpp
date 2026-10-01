#include "Search/SearchSettings.h"

#include "Settings/AppSettings.h"

namespace Graphite {

	void SearchSettings::Load()
	{
		m_Options.RespectGitIgnore = m_Store.GetSearchRespectGitIgnore();
		m_Options.ProjectsOnly = m_Store.GetSearchProjectsOnly();
	}

	void SearchSettings::SetRespectGitIgnore(bool respect)
	{
		m_Options.RespectGitIgnore = respect;
		m_Store.SetSearchRespectGitIgnore(respect);
	}

	void SearchSettings::SetProjectsOnly(bool projectsOnly)
	{
		m_Options.ProjectsOnly = projectsOnly;
		m_Store.SetSearchProjectsOnly(projectsOnly);
	}
}
