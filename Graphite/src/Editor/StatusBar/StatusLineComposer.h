#pragma once

#include "Editor/StatusBar/ServerStatusText.h"
#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Install/ServerInstaller.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	// What the status bar says about problems and servers, decided from plain inputs.
	//
	// Kept apart from GraphiteLayer, which gathers the inputs, and from StatusBar.cpp, which draws
	// the result: the choice between four things the server slot could say is a priority table,
	// and a table is worth asserting row by row without a window, a server or a document.

	// "2 errors, 1 warning". Empty - not "0 errors" - when both counts are zero, which is what
	// leaves the slot unoccupied.
	std::optional<std::string> ProblemCountText(std::size_t errors, std::size_t warnings);

	// An install or an uninstall of the active language's chosen server; a check for updates is none.
	struct ServerJob
	{
		InstallJobKind Kind = InstallJobKind::Install;
		std::string Server;
		std::optional<int> Percent;
	};

	struct ServerLineInputs
	{
		// The active text document's own server, or NotConfigured when there is no active text
		// document or its language has no server started for it.
		ServerStatus Active;

		std::optional<ServerJob> ActiveJob;

		// The active document's file name while it still has questions outstanding, so a ready
		// server can name the file whose colours have not arrived yet. Empty otherwise.
		std::optional<std::string> AnalysingFile;

		// PlainText when there is no active text document.
		LanguageId ActiveLanguage = LanguageId::PlainText;

		bool FolderOpen = false;
		bool FolderTrusted = false;

		// The workspace's servers as a whole, when any has been started.
		std::optional<ServerStatus> Overall;
	};

	struct ServerLine
	{
		std::string Text;
		std::string Detail;
		std::optional<LanguageId> SettingsLanguage;
	};

	// The server slot's phrase and tooltip, first match wins:
	//
	// 0. an install or an uninstall of the active language's server;
	// 1. the active document's own server, named "analysing <file>" while its answers are coming;
	// 2. a language recognised but unserved ("C#: no language server");
	// 3. "Restricted mode" for an open folder that is not trusted;
	// 4. the workspace's servers as a whole.
	//
	// Empty when none applies, which leaves the slot unoccupied.
	std::optional<ServerLine> ChooseServerLine(const ServerLineInputs& inputs);
}
