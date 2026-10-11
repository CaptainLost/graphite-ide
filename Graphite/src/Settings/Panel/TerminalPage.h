#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <string_view>
#include <vector>

namespace Graphite {

	class AppSettings;
	class TerminalWindows;

	class TerminalPage final : public SettingsPage
	{
	public:
		TerminalPage(AppSettings& settings, TerminalWindows& terminals) : m_Settings(settings), m_Terminals(terminals) {}

		std::string_view GetId() const override { return "terminal"; }

		std::string_view GetTitle() const override { return "Terminal"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void DrawDefaultShell();

		AppSettings& m_Settings;
		TerminalWindows& m_Terminals;
	};
}
