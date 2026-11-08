#include "Settings/Panel/TrustedFoldersPage.h"

#include "Settings/Panel/SettingsLayout.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextButton.h"
#include "Core/Platform/FileDialog.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <array>
#include <string>

namespace Graphite {

	namespace {

		const std::array<std::string, 1> kRemoveLabels{"Remove"};
		const std::array<std::string, 1> kAddLabels{"Add..."};
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

		for (const std::filesystem::path& folder : m_Trust.GetEntries())
		{
			card.Rows.push_back(SettingsRow{.Title = Core::ToUtf8(folder),
											.Keywords = {"trust", "restricted"},
											.ControlWidth = SettingsLayout::ButtonsWidth(kRemoveLabels),
											.DrawControl = [this, folder] { DrawRemove(folder); }});
		}

		// Trusting a folder before opening it, or one that is not open, was possible only from its restricted-mode bar.
		card.Rows.push_back(SettingsRow{.Title = m_Trust.GetEntries().empty() ? "No folder is trusted yet." : "Trust another folder",
										.Keywords = {"trust", "restricted", "add"},
										.ControlWidth = SettingsLayout::ButtonsWidth(kAddLabels),
										.DrawControl = [this] { DrawAdd(); }});

		return {card};
	}

	void TrustedFoldersPage::DrawAdd()
	{
		m_AddDialog.Poll();

		ImGui::BeginDisabled(m_AddDialog.IsOpen());

		if (Core::TextButton(kAddLabels.front().c_str()))
		{
			m_AddDialog.Start([] { return Core::FileDialog::PickFolder("Trust Folder"); },
							  [this](const std::filesystem::path& picked) {
								  // An empty path is the picker cancelled.
								  if (!picked.empty())
								  {
									  m_Trust.Trust(picked);
								  }
							  },
							  [](const Core::Error&) {});
		}

		ImGui::EndDisabled();
	}

	// WARNING: Revoking while the rows are drawn is safe because they are a copy, not the trust list itself.
	void TrustedFoldersPage::DrawRemove(const std::filesystem::path& folder)
	{
		if (Core::TextButton(kRemoveLabels.front().c_str()))
		{
			m_Trust.Revoke(folder);
		}
	}
}
