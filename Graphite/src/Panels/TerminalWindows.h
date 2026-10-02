#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Term/TerminalSession.h"

#include "Editor/EditorPalette.h"
#include "Panels/TerminalRules.h"
#include "Panels/TerminalView.h"
#include "Persistence/PersistedState.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class CommandRegistry;
	class TabCloseRequests;

}

namespace Graphite {

	class Workspace;

	// Where a terminal with nowhere better to go is docked: beside this window (the console
	// panel's, so the bottom row), else into a bottom row split off the central node again,
	// else into the central node itself.
	struct TerminalDockTargets
	{
		std::string BesideWindow;
		unsigned int CentreNode = 0;

		// The dockspace's root, which a re-created bottom row is finished against; 0 when there
		// is no dockspace to split.
		unsigned int DockSpace = 0;

		// The share of the central node a re-created bottom row takes: the layout's own, so the
		// row comes back the height a layout reset would give it.
		float BottomRatio = Core::DockLayout{}.BottomRatio;
	};

	// The shells running in this window, each a window of its own that can be dragged and docked
	// anywhere - the DiffWindows pattern (ADR 0494) rather than a panel, because there can be
	// any number of them. This owns the sessions rather than the views, because a session
	// outlives the frames its window is drawn in: a build in a tab behind another keeps making
	// progress, which is why every session is pumped and not just the visible ones.
	class TerminalWindows final : Core::NonCopyable
	{
	public:
		TerminalWindows(Workspace& workspace, const Core::Keymap& keys);

		// "New Terminal" for the default shell, bound to Ctrl+Shift+` by default, and one command
		// per discovered shell in the terminal.shells group. No menu of the menu bar lists them:
		// the tab bars of the terminals' own nodes do, so the palette and the key are what is
		// left when no such node is on screen.
		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		// Drains every session. Once per frame, before Render.
		void Update();

		void Render(const TerminalDockTargets& targets, const EditorPalette& palette,
					Core::TabCloseRequests& closeRequests);

		// The first discovered shell is the most capable, so it is the default. False when no
		// shell was found or it could not be started, which is logged.
		bool OpenDefaultShell();

		// Starts discovered shell `shell` in `directory`, or where a new terminal starts by
		// default when `directory` is empty, placed by the usual rule. False when there is no
		// such shell or it could not be started, which is logged. Not for use from inside
		// Render, whose loop over the open terminals a new one would invalidate.
		bool OpenShellIn(std::size_t shell, const std::filesystem::path& directory);

		// Opens a window running `program` rather than a discovered shell. It behaves as any
		// other: pumped, resized and closed the same way, closing when the program exits, but it
		// is never kept for the next launch. False when the program could not be started.
		bool OpenCommand(const std::string& label, const std::filesystem::path& program,
						 std::vector<std::string> arguments, const std::filesystem::path& workingDirectory);

		// The shells, as the next launch of this folder starts them again.
		std::vector<TerminalTabState> CaptureState() const;

		// Marks every open terminal as carried over from the folder about to be replaced or
		// closed: they stay open, but CaptureState leaves them out, as CarryOverOpenSessions does.
		void CarryOverOpenTerminals();

		// Starts what CaptureState returned last time for the folder being opened, each window
		// going back to the place imgui.ini remembers. Absent state opens one default shell when
		// nothing is open (OpensDefaultTerminal).
		void RestoreState(const std::optional<std::vector<TerminalTabState>>& saved);

	private:
		struct Entry
		{
			Core::Scope<Core::TerminalSession> Session;
			Core::Scope<TerminalView> View;
			OpenTerminal Record;

			// Not yet drawn: its first frame decides where it docks.
			bool Fresh = true;

			// Started by RestoreState and not yet drawn: goes back to where imgui.ini remembers
			// it, when it remembers anywhere it can still go.
			bool Restored = false;

			bool Focus = true;

			// Set by the window's ×, middle click, the tab menu or the view's own Close; acted on
			// after End, since destroying a session joins its reader thread.
			bool Closing = false;

			// The node whose tab bar or tab menu asked for this terminal, 0 for none: its first
			// frame docks it there, ahead of the last-focused rule, while that node still exists.
			unsigned int DockNode = 0;
		};

		// A terminal asked for from inside Render - a tab bar's buttons or a tab's menu - and
		// started once nothing is iterating m_Open any more.
		struct PendingOpen
		{
			std::size_t Shell = 0;
			unsigned int DockNode = 0;
		};

		bool Start(const Core::ShellProfile& profile, const std::filesystem::path& workingDirectory,
				   OpenTerminal record, bool restored);
		bool StartShell(std::size_t shell, const std::filesystem::path& directory, unsigned int dockNode);
		void StartPendingOpens();
		void RenderTabBarButtons(const TerminalDockTargets& targets);
		void DrawShellMenuItems(unsigned int dockNode);
		std::filesystem::path DefaultWorkingDirectory() const;
		Entry* FindEntry(const std::string& id);
		void PlaceNewWindow(const std::string& name, Entry& entry, const TerminalDockTargets& targets);
		void RemoveClosed();
		std::vector<OpenTerminal> Records() const;

		Workspace& m_Workspace;
		const Core::Keymap& m_Keys;
		std::vector<Core::ShellProfile> m_Shells;

		// Scoped, so closing one never moves the others: a TerminalView holds its session by
		// reference.
		std::vector<Core::Scope<Entry>> m_Open;

		std::vector<PendingOpen> m_PendingOpens;

		// The terminal whose window had focus last: a new one docks beside it.
		std::string m_LastFocused;
	};
}
