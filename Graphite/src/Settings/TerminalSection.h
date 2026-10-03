#pragma once

#include "Settings/SettingsSection.h"

#include <string_view>

namespace Graphite {

	class AppSettings;
	class TerminalWindows;

	// The settings screen for terminals: which of the discovered shells a new terminal starts with.
	class TerminalSection final : public SettingsSection
	{
	public:
		TerminalSection(AppSettings& settings, TerminalWindows& terminals);

		std::string_view GetTitle() const override { return "Terminal"; }

		void OnImGuiRender() override;

	private:
		AppSettings& m_Settings;
		TerminalWindows& m_Terminals;
	};
}
