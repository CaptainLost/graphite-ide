#include "Settings/Panel/TrustedFoldersPage.h"

#include "Settings/Panel/SettingsLayout.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/FileSystem/FileSystem.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <array>
#include <string>

namespace Graphite {

	namespace {

		const std::array<std::string, 1> kRemoveLabels{"Remove"};
	}

	const char* TrustedFoldersPage::GetIcon() const
	{
		return ICON_LC_SHIELD_CHECK;
	}

	std::vector<SettingsCard> TrustedFoldersPage::Describe()
	{
		SettingsCard card{.Heading = "Trusted folders",
						  .Note = "The language server and the background analysis run only in these folders and the folders inside them. "
								  "Any other folder opens in restricted mode."};

		if (m_Trust.GetEntries().empty())
		{
			card.Rows.push_back(SettingsRow{.Title = "No folder is trusted yet.", .Keywords = {"trust", "restricted"}});
			return {card};
		}

		for (const std::filesystem::path& folder : m_Trust.GetEntries())
		{
			card.Rows.push_back(SettingsRow{.Title = Core::ToUtf8(folder),
											.Keywords = {"trust", "restricted"},
											.ControlWidth = SettingsLayout::ButtonsWidth(kRemoveLabels),
											.DrawControl = [this, folder] { DrawRemove(folder); }});
		}

		return {card};
	}

	// WARNING: Revoking while the rows are drawn is safe because they are a copy, not the trust list itself.
	void TrustedFoldersPage::DrawRemove(const std::filesystem::path& folder)
	{
		if (ImGui::Button(kRemoveLabels.front().c_str()))
		{
			m_Trust.Revoke(folder);
		}
	}
}
