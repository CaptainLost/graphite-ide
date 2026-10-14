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

		std::string m_BinaryBeingTyped;
		bool m_EditingBinary = false;

		std::optional<std::filesystem::path> m_ResolvedFor;
		std::filesystem::path m_Resolved;

		std::string m_NewModel;
		Price m_NewPrice;
	};
}
