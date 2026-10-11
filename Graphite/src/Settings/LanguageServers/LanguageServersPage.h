#pragma once

#include "Settings/Panel/SettingsDirectory.h"
#include "Settings/Panel/SettingsPage.h"

#include <string_view>
#include <vector>

namespace Graphite {

	class AnalysisSettings;
	class AppSettings;
	class LanguageServices;
	class ServerInstalls;

	// A page per language with servers, then Background Analysis, listed beside whichever is open.
	class LanguageServersPage final : public SettingsPage
	{
	public:
		LanguageServersPage(AnalysisSettings& analysis, AppSettings& store, ServerInstalls& installs, LanguageServices& services);

		std::string_view GetId() const override { return "languageServers"; }

		std::string_view GetTitle() const override { return "Language Servers"; }

		const char* GetIcon() const override;

		// Never drawn: the panel draws the page open in the inner list instead.
		std::vector<SettingsCard> Describe() override { return {}; }

		SettingsDirectory* GetChildren() override { return &m_Children; }

	private:
		SettingsDirectory m_Children;
	};
}
