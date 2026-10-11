#pragma once

#include "Sessions/Pricing.h"
#include "Settings/SettingsSection.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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
		void RenderNewPrice();

		ClaudeSettings& m_Settings;

		// The path being typed, written on release like the sliders on the settings pages: every
		// keystroke would otherwise rewrite the settings file.
		std::string m_PendingBinary;
		bool m_EditingBinary = false;

		// What the binary setting resolves to, worked out again only when the setting changes:
		// an empty setting means a search of PATH, too much to repeat every frame.
		std::optional<std::filesystem::path> m_ResolvedFor;
		std::filesystem::path m_Resolved;

		// The row that prices a model the defaults do not know, such as one newer than this
		// build. Nothing is saved until Add.
		std::string m_NewModel;
		Price m_NewPrice;
	};
}
