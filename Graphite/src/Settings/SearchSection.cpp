#include "Settings/SearchSection.h"

#include "Search/SearchSettings.h"

#include <imgui.h>

namespace Graphite {

	SearchSection::SearchSection(SearchSettings& settings, bool& solutionLoaded)
		: m_Settings(settings), m_SolutionLoaded(solutionLoaded)
	{
	}

	void SearchSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Files");

		bool respect = m_Settings.Get().RespectGitIgnore;

		if (ImGui::Checkbox("Respect .gitignore", &respect))
		{
			m_Settings.SetRespectGitIgnore(respect);
		}

		bool projectsOnly = m_Settings.Get().ProjectsOnly;

		// Disabled rather than hidden: a control that vanishes reads as a bug, and the reason it
		// cannot be used is worth saying.
		ImGui::BeginDisabled(!m_SolutionLoaded);

		if (ImGui::Checkbox("Search only files listed in projects", &projectsOnly))
		{
			m_Settings.SetProjectsOnly(projectsOnly);
		}

		ImGui::EndDisabled();

		if (!m_SolutionLoaded)
		{
			ImGui::TextDisabled("No solution is open, so there are no project files to limit to.");
		}
	}
}
