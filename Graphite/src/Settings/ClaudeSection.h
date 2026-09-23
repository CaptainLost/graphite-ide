#pragma once

#include "Settings/SettingsSection.h"

#include <optional>
#include <string>

namespace Graphite {

	class ClaudeSettings;

	class ClaudeSection final : public SettingsSection
	{
	public:
		explicit ClaudeSection(ClaudeSettings& settings);

		std::string_view GetTitle() const override { return "Claude"; }

		void OnImGuiRender() override;

	private:
		void RenderBinary();
		void RenderPrices();

		ClaudeSettings& m_Settings;

		// The path being typed, written on release like LanguageSection's sliders: every
		// keystroke would otherwise rewrite the settings file.
		std::string m_PendingBinary;
		bool m_EditingBinary = false;
	};
}
