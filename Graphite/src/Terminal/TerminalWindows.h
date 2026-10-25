#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Terminal/TerminalSession.h"

#include "Theme/EditorPalette.h"
#include "Terminal/CommandTerminal.h"
#include "Terminal/TerminalRules.h"
#include "Terminal/TerminalView.h"
#include "Persistence/PersistedState.h"
#include "UI/TabWindow.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class CommandRegistry;
	class EventBus;
	class TabCloseRequests;

}

namespace Graphite {

	class AppSettings;
	class Workspace;

	struct TerminalDockTargets
	{
		std::string BesideWindow;
		unsigned int CentreNode = 0;

		std::vector<std::string> RowWindows;

		unsigned int DockSpace = 0;

		float BottomRatio = Core::DockLayout{}.BottomRatio;

		bool LayoutRebuilt = false;
	};

	class TerminalWindows final : Core::NonCopyable
	{
	public:
		TerminalWindows(Workspace& workspace, const Core::Keymap& keys, const Core::CommandRegistry& commands,
						AppSettings& settings, Core::EventBus& events);

		const std::vector<Core::ShellProfile>& GetShells() const { return m_Shells; }

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		void Update();

		void Render(const TerminalDockTargets& targets, const EditorPalette& palette,
					Core::TabCloseRequests& closeRequests);

		bool OpenDefaultShell();

		bool OpenDefaultShellIn(const std::filesystem::path& directory);

		std::size_t GetDefaultShellIndex() const;

		// WARNING: Not for use from inside Render, whose loop over the open terminals a new one would invalidate.
		bool OpenShellIn(std::size_t shellIndex, const std::filesystem::path& directory);

		bool OpenStartupShell();

		// WARNING: Empty when the program did not start; the failure has been reported in a toast by then.
		Core::Ref<CommandTerminal> OpenCommand(const std::string& label, const std::filesystem::path& program,
											   std::vector<std::string> arguments,
											   const std::filesystem::path& workingDirectory);

		std::optional<std::vector<TerminalTabState>> CaptureState() const;

		void CarryOverOpenTerminals();

		void RestoreState(const std::optional<std::vector<TerminalTabState>>& saved, bool trusted);

		void RequestRedock();

		bool IsHidden() const { return m_Hidden; }

		void SetHidden(bool hidden);

		// WARNING: Called by each start a person asks for, never from OpenDefaultShell, OpenDefaultShellIn, OpenShellIn or StartShell: the startup shell and a folder's restore reach them too, and must leave hidden terminals hidden.
		void Reveal();

		std::size_t GetOpenCount() const { return m_Open.size(); }

		TerminalStripLook GetStripLook() const;

		void ToggleFromStrip();

	private:
		enum class EntryOrigin
		{
			Requested,
			Restored
		};

		struct Entry
		{
			Core::Scope<Core::TerminalSession> Session;
			Core::Scope<TerminalView> View;
			OpenTerminal Record;
			Core::Ref<CommandTerminal> Command;
			TabWindowState Window{.Focus = true};
			// WARNING: Acted on only after End, since destroying a session joins its reader thread.
			bool Closing = false;
			unsigned int DockNode = 0;
			TerminalGridSize LastMeasured;
		};

		struct PendingOpen
		{
			std::size_t Shell = 0;
			unsigned int DockNode = 0;
		};

		bool Start(const Core::ShellProfile& profile, const std::filesystem::path& workingDirectory,
				   OpenTerminal record, EntryOrigin origin);
		bool StartShell(std::size_t shellIndex, const std::filesystem::path& directory, unsigned int dockNode);
		void StartPendingOpens();
		void RecordAnnouncedTitles();
		bool RenderWindows(const TerminalDockTargets& targets, const EditorPalette& palette,
						   Core::TabCloseRequests& closeRequests);
		void BringForwardLastFocused();
		void RenderTabBarButtons(const TerminalDockTargets& targets);
		void DrawShellMenuItems(unsigned int dockNode);
		std::filesystem::path DefaultWorkingDirectory() const;
		Entry* FindEntry(const std::string& id);
		void PlaceNewWindow(const std::string& name, Entry& entry, const TerminalDockTargets& targets);
		void RemoveClosed();
		static bool TabCloses(const Core::Scope<Entry>& entry);
		static void NoteCommandExit(Entry& entry);
		static void ResizeTerminal(Entry& entry, Core::CursorPosition fitted);
		void TakeShowRequest(Entry& entry);
		void StoreRecords(const std::vector<OpenTerminal>& records);
		std::vector<OpenTerminal> Records() const;

		Workspace& m_Workspace;
		AppSettings& m_Settings;
		const Core::Keymap& m_Keys;
		const Core::CommandRegistry& m_Commands;
		Core::EventBus& m_Events;
		std::vector<Core::ShellProfile> m_Shells;

		// WARNING: Scoped so closing one never moves the others: a TerminalView holds its session by reference.
		std::vector<Core::Scope<Entry>> m_Open;

		std::vector<PendingOpen> m_PendingOpens;

		std::string m_LastFocused;

		bool m_KeepsTerminals = false;

		std::optional<std::vector<TerminalTabState>> m_HeldBack;

		bool m_RedockPending = false;

		bool m_Hidden = false;

		bool m_AnyDrawn = false;
	};
}
