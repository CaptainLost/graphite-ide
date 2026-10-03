#include "Settings/TerminalSection.h"

#include "Settings/AppSettings.h"
#include "Terminal/TerminalWindows.h"

#include <imgui.h>

#include <cstddef>

namespace Graphite {

	TerminalSection::TerminalSection(AppSettings& settings, TerminalWindows& terminals)
		: m_Settings(settings), m_Terminals(terminals)
	{
	}

	void TerminalSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Shell");

		const std::vector<Core::ShellProfile>& shells = m_Terminals.GetShells();

		if (shells.empty())
		{
			ImGui::TextDisabled("No shell was found on this machine.");
			return;
		}

		const std::size_t current = m_Terminals.GetDefaultShellIndex();

		if (ImGui::BeginCombo("Default shell", shells[current].Name.c_str()))
		{
			for (std::size_t index = 0; index < shells.size(); ++index)
			{
				const bool selected = index == current;

				if (ImGui::Selectable(shells[index].Name.c_str(), selected) && !selected)
				{
					m_Settings.SetDefaultShell(shells[index].Name);
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::TextDisabled("Used by New Terminal, the + button and Open in Terminal. Already open terminals keep theirs.");
	}
}
