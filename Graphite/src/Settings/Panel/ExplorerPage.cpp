#include "Settings/Panel/ExplorerPage.h"

#include "Settings/Panel/SettingsLayout.h"

#include <IconsLucide.h>

namespace Graphite {

	const char* ExplorerPage::GetIcon() const
	{
		return ICON_LC_FOLDER_TREE;
	}

	std::vector<SettingsCard> ExplorerPage::Describe()
	{
		SettingsCard card;

		card.Rows.push_back(SettingsRow{.Title = "Open folders to the active file",
										.Description = "Switching to a file expands the tree down to it and selects it.",
										.Keywords = {"follow", "reveal"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawFollowActiveFile(); }});
		card.Rows.push_back(SettingsRow{.Title = "Files ignored by Git",
										.Description = "How files matched by .gitignore appear in the tree.",
										.Keywords = {"gitignore", "hidden"},
										.ControlWidth = SettingsLayout::ComboWidth(LabelsOf(kIgnoredFileChoices)),
										.DrawControl = [this] { DrawIgnoredFiles(); }});

		return {card};
	}

	void ExplorerPage::DrawFollowActiveFile()
	{
		DrawCommandToggle(m_Commands, "explorer.followActiveFile");
	}

	void ExplorerPage::DrawIgnoredFiles()
	{
		DrawCommandChoice(m_Commands, "##ignoredFiles", kIgnoredFileChoices);
	}
}
