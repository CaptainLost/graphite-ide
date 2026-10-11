#include "Settings/Panel/TerminalPage.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/SettingsLayout.h"
#include "Terminal/TerminalWindows.h"

#include <IconsMaterialDesignIcons.h>

#include <cstddef>
#include <string>

namespace Graphite {

	namespace {

		const std::vector<std::string> kShellKeywords{"shell", "powershell", "cmd", "bash"};

		std::vector<std::string> ShellNames(const std::vector<Core::ShellProfile>& shells)
		{
			std::vector<std::string> names;

			for (const Core::ShellProfile& shell : shells)
			{
				names.push_back(shell.Name);
			}

			return names;
		}
	}

	const char* TerminalPage::GetIcon() const
	{
		return ICON_MDI_CONSOLE;
	}

	std::vector<SettingsCard> TerminalPage::Describe()
	{
		const std::vector<Core::ShellProfile>& shells = m_Terminals.GetShells();
		SettingsCard card;

		if (shells.empty())
		{
			card.Rows.push_back(SettingsRow{.Title = "Default shell", .Description = "No shell was found on this machine.", .Keywords = kShellKeywords});
			return {card};
		}

		card.Rows.push_back(SettingsRow{.Title = "Default shell",
										.Description = "Used by New Terminal, the + button and Open in Terminal. Terminals already open keep theirs.",
										.Keywords = kShellKeywords,
										.ControlWidth = SettingsLayout::ComboWidth(ShellNames(shells)),
										.DrawControl = [this] { DrawDefaultShell(); }});

		return {card};
	}

	void TerminalPage::DrawDefaultShell()
	{
		const std::vector<std::string> names = ShellNames(m_Terminals.GetShells());
		std::size_t selected = m_Terminals.GetDefaultShellIndex();

		if (SettingsLayout::Choice("##defaultShell", names, selected))
		{
			m_Settings.SetDefaultShell(names[selected]);
		}
	}
}
