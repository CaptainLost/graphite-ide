#include "App/SettingsPages.h"

#include "Settings/LanguageServers/LanguageServersPage.h"
#include "Settings/Panel/AppearancePage.h"
#include "Settings/Panel/ClaudeSessionsPage.h"
#include "Settings/Panel/EditorPage.h"
#include "Settings/Panel/ExplorerPage.h"
#include "Settings/Panel/FindInFilesPage.h"
#include "Settings/Panel/GitPage.h"
#include "Settings/Panel/KeyBindingsPage.h"
#include "Settings/Panel/TerminalPage.h"
#include "Settings/Panel/TrustedFoldersPage.h"

#include "Core/Base/Base.h"

#include <utility>
#include <vector>

namespace Graphite {

	SettingsDirectory BuildSettingsDirectory(const SettingsSources& sources)
	{
		SettingsGroup general{.Title = "General"};
		general.Pages.push_back(Core::CreateScope<AppearancePage>(sources.Scheme, sources.Store, sources.Modals, sources.Events, sources.ThemesDirectory));
		general.Pages.push_back(Core::CreateScope<KeyBindingsPage>(sources.Commands, sources.Keys, sources.Modals, sources.Scheme, sources.OnKeyBindingsChanged));
		general.Pages.push_back(Core::CreateScope<LanguageServersPage>(sources.Analysis, sources.Store, sources.Installs, sources.Services));
		general.Pages.push_back(Core::CreateScope<TrustedFoldersPage>(sources.Trust));

		SettingsGroup panels{.Title = "Panels"};
		panels.Pages.push_back(Core::CreateScope<EditorPage>(sources.Editor, sources.Commands));
		panels.Pages.push_back(Core::CreateScope<ExplorerPage>(sources.Commands));
		panels.Pages.push_back(Core::CreateScope<FindInFilesPage>(sources.Search, sources.SolutionLoaded));
		panels.Pages.push_back(Core::CreateScope<GitPage>(sources.Editor));
		panels.Pages.push_back(Core::CreateScope<TerminalPage>(sources.Store, sources.Terminals));
		panels.Pages.push_back(Core::CreateScope<ClaudeSessionsPage>(sources.Claude));

		std::vector<SettingsGroup> groups;
		groups.push_back(std::move(general));
		groups.push_back(std::move(panels));

		return SettingsDirectory{std::move(groups)};
	}
}
