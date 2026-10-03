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

		// The last title the shell announced, kept so the tab reads the same after a restart until
		// the new shell announces its own.
		std::string Title;

		// Opened while no folder was open, so it was never any folder's: the next folder opened
		// adopts it. A terminal that belonged to a folder is carried over instead.
		bool Unowned = false;
	};

	std::vector<TerminalTabState> CaptureTerminals(const std::vector<OpenTerminal>& open);

	// Whether a folder just opened takes over a terminal that is open: a shell, not a command's
	// window, that no folder has had.
	bool AdoptsTerminal(const OpenTerminal& terminal);

	// The title a tab shows: what the shell announces now, else what it announced last time.
	std::string TerminalLabelTitle(const std::string& live, const std::string& remembered);

	// Whether a measured grid is worth telling the shell about. A window on its first frame, or
	// one squeezed to nothing, measures a sliver; resizing the pseudo-console to it makes the
	// console host reflow what it has printed - the banner - out of existence.
	bool TerminalSizeIsPlausible(int columns, int rows);

	struct TerminalToStart
	{
		std::string Id;

		// Index into the discovered shells.
		std::size_t Shell = 0;
		std::filesystem::path WorkingDirectory;
		std::string Title;
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

	// Whether a folder just opened has a terminal list of its own to keep: one it saved before,
	// even an empty one, or the default shell it has just opened. A folder opened while another
	// one's shells were still up has neither until the reader starts a shell in it, and until
	// then its list stays absent rather than empty - an empty list is "every terminal closed on
	// purpose", which would leave the folder opening with no terminal from then on although the
	// reader never closed one of its own.
	bool KeepsTerminals(const std::optional<std::vector<TerminalTabState>>& saved, bool openedDefault);

	// What a folder's state records of its terminals: absent while it keeps none, otherwise
	// CaptureTerminals - which may well be empty.
	std::optional<std::vector<TerminalTabState>> CaptureFolderTerminals(const std::vector<OpenTerminal>& open,
																		bool keepsTerminals);

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

	// Where a scrollbar's thumb sits within its track, both measured from the top.
	struct TerminalScrollbarThumb
	{
		float Top = 0.0f;
		float Height = 0.0f;
	};

	// The thumb for a view `rows` tall that is `offset` lines above the tail of `scrollback`
	// lines of history. The tail - offset 0 - is the bottom of the track and the oldest line
	// the top. The thumb is as tall as the visible share of everything there is, never shorter
	// than `minHeight` so it stays something to grab, and fills the track when there is no
	// history to scroll through.
	TerminalScrollbarThumb ComputeTerminalScrollbarThumb(int offset, int rows, int scrollback,
															 float trackHeight, float minHeight);

	// The inverse: the offset a thumb dragged to `thumbTop` stands for, clamped to the history.
	int TerminalScrollOffsetForThumbTop(float thumbTop, int scrollback, float trackHeight,
										float thumbHeight);

	// What the view itself does with a key, before the IDE's bindings or the shell see it.
	enum class TerminalShortcut
	{
		// Not one of the view's gestures: the IDE's bindings, then the shell, decide.
		None,

		Copy,
		Paste,

		// Ctrl+C with nothing selected: the interrupt the shell is owed.
		Interrupt,

		// Consumed and answered with nothing. Ctrl+Shift+C with no selection lands here: it is
		// the copy chord, and a copy of nothing must never fall through to become a ^C.
		Swallow
	};

	// Ctrl+C copies a selection and interrupts without one; Ctrl+Shift+C only ever copies.
	// Ctrl+V and Ctrl+Shift+V both paste, so the plain chord of every other application works
	// here as well as the one a terminal needs for itself. Alt or Super in the mix makes it
	// somebody else's chord.
	TerminalShortcut ClassifyTerminalShortcut(Core::KeyCode key, Core::KeyModifier held,
											   bool hasSelection);
}
