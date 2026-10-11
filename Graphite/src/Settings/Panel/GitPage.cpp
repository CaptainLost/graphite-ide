#include "Settings/Panel/GitPage.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/ImGui/Toggle.h"

#include <IconsMaterialDesignIcons.h>

namespace Graphite {

	const char* GitPage::GetIcon() const
	{
		return ICON_MDI_SOURCE_BRANCH;
	}

	std::vector<SettingsCard> GitPage::Describe()
	{
		SettingsCard card{.Heading = "Diffs"};

		card.Rows.push_back(SettingsRow{.Title = "Wrap long lines",
										.Description = "Long lines in a diff continue on the next line. The diff toolbar has the same switch.",
										.Keywords = {"diff", "word wrap"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawDiffWordWrap(); }});

		return {card};
	}

	void GitPage::DrawDiffWordWrap()
	{
		bool wrap = m_Settings.IsDiffWordWrap();

		if (Core::Toggle("##diffWordWrap", &wrap))
		{
			m_Settings.SetDiffWordWrap(wrap);
		}
	}
}
