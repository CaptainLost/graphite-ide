#pragma once

#include "Core/Platform/ShellProfiles.h"

#include "Persistence/PersistedState.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// What a terminal's tab says, from what its shell announced. A shell's announced title is a
	// program's own text, and a shell announces the least useful thing it could: PowerShell and
	// cmd each name their own executable, path and all. That path tells a reader nothing the
	// profile name does not tell them better, so a title that is only the shell introducing
	// itself is dropped. A title a program actually chose - vim naming its file, a build naming
	// its target - is kept, because that is the one worth reading.
	std::string TerminalTitleFor(const std::string& title, const std::string& profileName);

	// The id of the command that opens a shell, from its profile name rather than its place in
	// the discovered list, so a key bound to it stays on the same shell when discovery finds one
	// more or one fewer.
	std::string NewTerminalCommandId(std::string_view profileName);

	// One open terminal as the rules below see it: no session, no view, only what decides
	// whether and how it is kept.
	struct OpenTerminal
	{
		std::string Id;
		std::string ProfileName;
		std::filesystem::path WorkingDirectory;

		// Started by a command - a Claude session - rather than as a shell. Never kept: starting
		// it again unasked at the next launch would be a surprise.
		bool FromCommand = false;

		// Still open from a folder another one replaced, and already written to that folder.
		bool CarriedOver = false;
	};

	std::vector<TerminalTabState> CaptureTerminals(const std::vector<OpenTerminal>& open);

	struct TerminalToStart
	{
		std::string Id;

		// Index into the discovered shells.
		std::size_t Shell = 0;
		std::filesystem::path WorkingDirectory;
	};

	struct TerminalRestorePlan
	{
		std::vector<TerminalToStart> Start;

		// Saved ids that are open already: those terminals belong to the folder being opened and
		// stop being carried over.
		std::vector<std::string> AlreadyOpen;

		// Saved profile names no discovered shell has, for the log.
		std::vector<std::string> UnknownProfiles;
	};

	// `resolveDirectory` turns a saved directory into the one to start in: itself when it still
	// exists, the folder's root otherwise. Passed in so the rule can be tested without a disk.
	TerminalRestorePlan PlanTerminalRestore(
		const std::vector<TerminalTabState>& saved, const std::vector<Core::ShellProfile>& shells,
		const std::vector<OpenTerminal>& open,
		const std::function<std::filesystem::path(const std::filesystem::path&)>& resolveDirectory);

	// A folder that never kept terminals opens one, as the terminal always did; one that kept an
	// empty list had them all closed on purpose. Either way, terminals already open - carried
	// over from the folder before - are enough.
	bool OpensDefaultTerminal(const std::optional<std::vector<TerminalTabState>>& saved,
							  std::size_t openCount);
}
