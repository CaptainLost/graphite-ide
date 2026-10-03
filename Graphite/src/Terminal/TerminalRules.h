#pragma once

#include "Core/Base/KeyCodes.h"
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
	//
	// A folder in restricted mode (`trusted` false) plans nothing: its saved list comes from its
	// own .graphite/workspace.json, so any shell in any directory could be named there, and a
	// terminal it starts is one the reader never asked for. The reader starts one by hand.
	TerminalRestorePlan PlanTerminalRestore(
		const std::vector<TerminalTabState>& saved, const std::vector<Core::ShellProfile>& shells,
		const std::vector<OpenTerminal>& open,
		const std::function<std::filesystem::path(const std::filesystem::path&)>& resolveDirectory,
		bool trusted);

	// A folder that never kept terminals opens one, as the terminal always did; one that kept an
	// empty list had them all closed on purpose. Either way, terminals already open - carried
	// over from the folder before - are enough. A folder in restricted mode opens none, for the
	// reason PlanTerminalRestore gives.
	bool OpensDefaultTerminal(const std::optional<std::vector<TerminalTabState>>& saved,
							  std::size_t openCount, bool trusted);

	// Whether a folder just opened has a terminal list of its own to keep: one it saved before,
	// even an empty one, or the default shell it has just opened. A folder opened while another
	// one's shells were still up has neither until the reader starts a shell in it, and until
	// then its list stays absent rather than empty - an empty list is "every terminal closed on
	// purpose", which would leave the folder opening with no terminal from then on although the
	// reader never closed one of its own.
	bool KeepsTerminals(const std::optional<std::vector<TerminalTabState>>& saved, bool openedDefault);

	// The saved list a folder in restricted mode starts none of (`trusted` false), which its
	// state goes on recording exactly as it was stored: nothing it names is open, and capturing
	// what is would write an empty list - "every terminal closed on purpose" - so the folder,
	// once trusted, would reopen with neither its terminals nor the default shell. Absent for a
	// trusted folder, and for one that saved no list. Dropped once the reader starts a shell by
	// hand in the folder: from then on its list is what is open, as in any other folder.
	std::optional<std::vector<TerminalTabState>> HeldBackTerminals(
		const std::optional<std::vector<TerminalTabState>>& saved, bool trusted);

	// What a folder's state records of its terminals: the held-back list while there is one
	// (HeldBackTerminals), otherwise absent while it keeps none, otherwise CaptureTerminals -
	// which may well be empty.
	std::optional<std::vector<TerminalTabState>> CaptureFolderTerminals(
		const std::vector<OpenTerminal>& open, bool keepsTerminals,
		const std::optional<std::vector<TerminalTabState>>& heldBack);

	// Where a terminal opened on an Explorer entry starts: a folder is itself, a file is the
	// folder it sits in.
	std::filesystem::path TerminalDirectoryFor(const std::filesystem::path& path, bool isDirectory);

	// Whether a terminal takes this frame's typed characters with these modifiers held. Only
	// Control without Alt is a chord rather than typing. Alt alone has to be typing too: GLFW
	// drops the fake Left Ctrl that AltGr sends on Windows, so the host sees AltGr - and with it
	// every Polish letter that needs it, ą ć ę ł ń ó ś ź ż - as Alt on its own, while a real
	// Alt+letter arrives as WM_SYSCHAR and never reaches the character queue at all.
	bool TerminalTakesTypedText(Core::KeyModifier held);

	// The modifiers a key press is encoded with. A press that typed a character sends neither
	// Control nor Alt: passing them through would make EncodeKey turn `ą` into ESC a on top of
	// itself. A press that typed nothing - Alt+B, Ctrl+Alt+Left - keeps them, and stays a chord.
	Core::KeyModifier TerminalKeyModifiers(Core::KeyModifier held, bool producedCharacter);
}
