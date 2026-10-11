#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <string_view>
#include <vector>

namespace Graphite {

	class EditorSettings;

	class GitPage final : public SettingsPage
	{
	public:
		explicit GitPage(EditorSettings& settings) : m_Settings(settings) {}

		std::string_view GetId() const override { return "git"; }

		std::string_view GetTitle() const override { return "Git"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void DrawDiffWordWrap();

		EditorSettings& m_Settings;
	};
}
