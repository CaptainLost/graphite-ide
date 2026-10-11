#include "Settings/Panel/FindInFilesPage.h"

#include "Search/SearchSettings.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/ImGui/Toggle.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <string>

namespace Graphite {

	const char* FindInFilesPage::GetIcon() const
	{
		return ICON_MDI_TEXT_SEARCH;
	}

	std::vector<SettingsCard> FindInFilesPage::Describe()
	{
		const std::string projectsDescription = m_SolutionLoaded ? "Searches only the files the open solution's projects list."
																 : "No solution is open, so there are no project files to limit to.";
		SettingsCard card;

		card.Rows.push_back(SettingsRow{.Title = "Respect .gitignore",
										.Description = "Skips the files Git ignores.",
										.Keywords = {"git", "ignored", "search"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawRespectGitIgnore(); }});
		card.Rows.push_back(SettingsRow{.Title = "Only files listed in projects",
										.Description = projectsDescription,
										.Keywords = {"solution", "project", "search"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawProjectsOnly(); }});

		return {card};
	}

	void FindInFilesPage::DrawRespectGitIgnore()
	{
		bool respect = m_Settings.Get().RespectGitIgnore;

		if (Core::Toggle("##respectGitIgnore", &respect))
		{
			m_Settings.SetRespectGitIgnore(respect);
		}
	}

	// Disabled rather than hidden: a control that vanishes reads as a bug, and the description says why.
	void FindInFilesPage::DrawProjectsOnly()
	{
		bool projectsOnly = m_Settings.Get().ProjectsOnly;

		ImGui::BeginDisabled(!m_SolutionLoaded);

		if (Core::Toggle("##projectsOnly", &projectsOnly))
		{
			m_Settings.SetProjectsOnly(projectsOnly);
		}

		ImGui::EndDisabled();
	}
}
