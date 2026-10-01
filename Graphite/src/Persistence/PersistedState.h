#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"
#include "Core/Platform/WindowPlacement.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What outlives the application whichever folder is open.
	struct GlobalState
	{
		std::optional<Core::WindowPlacement> Window;

		// Only panels whose visibility differs from their default, keyed by panel id. A panel
		// added in a later version then starts the way its author meant, rather than the way a
		// list written before it existed happens to say.
		std::map<std::string, bool> PanelVisibility;

		friend bool operator==(const GlobalState&, const GlobalState&) = default;
	};

	// One open file document, as the next launch reopens it.
	struct TabState
	{
		std::filesystem::path Path;
		bool Preview = false;

		// The caret, as a line and a byte offset into that line. A line survives the file being
		// edited elsewhere far better than a document offset does.
		std::size_t Line = 0;
		std::size_t Column = 0;

		// The document line at the top of the view.
		std::size_t Top = 0;

		friend bool operator==(const TabState&, const TabState&) = default;
	};

	// The three views a session's window holds. Here rather than beside that window, so the
	// state can name one without reaching for the code that draws it.
	enum class SessionTab
	{
		Conversation,
		Cost,
		Requests
	};

	// One open session window. The title is what the tab said when it was closed, shown
	// until the session is found again and its own title replaces it.
	struct SessionTabState
	{
		std::string Id;
		std::string Title;
		SessionTab Tab = SessionTab::Conversation;

		friend bool operator==(const SessionTabState&, const SessionTabState&) = default;
	};

	// One open diff window: a file in one commit against its parent, or a file in the working
	// tree against the index or HEAD. The same three things the window is keyed by, kept apart
	// from the window's own type so this stays plain data.
	struct DiffTabState
	{
		// Absent for the working tree.
		std::optional<Core::Oid> Commit;
		Core::DiffTarget Target = Core::DiffTarget::CommitToParent;

		// Relative to the repository's working directory, as git reports it.
		std::filesystem::path Path;

		friend bool operator==(const DiffTabState&, const DiffTabState&) = default;
	};

	// One open terminal window, as the next launch starts it again: a fresh process of the same
	// shell, in the same directory, under the same window identity - which is what imgui.ini
	// keeps the window's place by. The id is a Uuid rather than a counter so two folders'
	// terminals never share a remembered place.
	struct TerminalTabState
	{
		std::string Id;
		std::string ProfileName;
		std::filesystem::path WorkingDirectory;

		friend bool operator==(const TerminalTabState&, const TerminalTabState&) = default;
	};

	struct WorkspaceState
	{
		std::vector<TabState> Tabs;
		std::optional<std::filesystem::path> Active;
		std::vector<SessionTabState> Sessions;
		std::vector<DiffTabState> Diffs;

		// Absent when this folder's state was written before terminals were kept, or never
		// written: the folder then opens one default terminal. Present and empty when every
		// terminal was closed, and then none opens.
		std::optional<std::vector<TerminalTabState>> Terminals;

		friend bool operator==(const WorkspaceState&, const WorkspaceState&) = default;
	};
}
