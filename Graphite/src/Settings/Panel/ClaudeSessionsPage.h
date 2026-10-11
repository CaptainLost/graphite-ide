#pragma once

#include "Sessions/Pricing.h"
#include "Settings/Panel/SettingsPage.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class ClaudeSettings;

	class ClaudeSessionsPage final : public SettingsPage
	{
	public:
		explicit ClaudeSessionsPage(ClaudeSettings& settings) : m_Settings(settings) {}

		std::string_view GetId() const override { return "claudeSessions"; }

		std::string_view GetTitle() const override { return "Claude Sessions"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void RefreshResolved();
		void DrawBinary();
		void DrawPrices();
		void DrawNewPrice();

		ClaudeSettings& m_Settings;

		// The path being typed, written when the field is left: every keystroke would otherwise rewrite
		// the settings file.
		std::string m_PendingBinary;
		bool m_EditingBinary = false;

		// What the binary setting resolves to, worked out again only when the setting changes: an empty
		// setting means a search of PATH, too much to repeat every frame.
		std::optional<std::filesystem::path> m_ResolvedFor;
		std::filesystem::path m_Resolved;

		// The row that prices a model the defaults do not know, such as one newer than this build.
		// Nothing is saved until Add.
		std::string m_NewModel;
		Price m_NewPrice;
	};
}
