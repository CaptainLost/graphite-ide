#include "Settings/LanguageServers/LanguageServersPage.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Settings/LanguageServers/BackgroundAnalysisPage.h"
#include "Settings/LanguageServers/LanguageServerPage.h"

#include "Core/Base/Base.h"

#include <IconsMaterialDesignIcons.h>

#include <utility>

namespace Graphite {

	namespace {

		SettingsDirectory BuildChildren(AnalysisSettings& analysis, AppSettings& store, ServerInstalls& installs, LanguageServices& services)
		{
			SettingsGroup languages;

			for (const LanguageId language : LanguagesWithServers())
			{
				languages.Pages.push_back(Core::CreateScope<LanguageServerPage>(language, store, installs, services));
			}

			SettingsGroup backgroundAnalysis;
			backgroundAnalysis.Pages.push_back(Core::CreateScope<BackgroundAnalysisPage>(analysis));

			std::vector<SettingsGroup> groups;
			groups.push_back(std::move(languages));
			groups.push_back(std::move(backgroundAnalysis));

			return SettingsDirectory{std::move(groups)};
		}
	}

	LanguageServersPage::LanguageServersPage(AnalysisSettings& analysis, AppSettings& store, ServerInstalls& installs, LanguageServices& services)
		: m_Children(BuildChildren(analysis, store, installs, services))
	{
	}

	const char* LanguageServersPage::GetIcon() const
	{
		return ICON_MDI_SERVER_OUTLINE;
	}
}
