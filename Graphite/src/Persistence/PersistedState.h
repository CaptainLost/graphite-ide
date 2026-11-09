#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"
#include "Core/Platform/WindowPlacement.h"
#include "Core/Text/Caret.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct GlobalState
	{
		std::optional<Core::WindowPlacement> Window;

		std::map<std::string, bool> PanelVisibility;

		bool TerminalsHidden = false;

		friend bool operator==(const GlobalState&, const GlobalState&) = default;
	};

	struct TabState
	{
		std::filesystem::path Path;
		bool Preview = false;

		std::size_t Line = 0;
		std::size_t Column = 0;

		std::size_t Top = 0;

		Core::ColumnUnit ColumnIn = Core::ColumnUnit::Byte;

		friend bool operator==(const TabState&, const TabState&) = default;
	};

	enum class SessionTab
	{
		Conversation,
		Cost,
		Requests
	};

	struct SessionTabState
	{
		std::string Id;
		std::string Title;
		SessionTab Tab = SessionTab::Conversation;

		friend bool operator==(const SessionTabState&, const SessionTabState&) = default;
	};

	struct DiffTabState
	{
		std::optional<Core::Oid> Commit;
		Core::DiffTarget Target = Core::DiffTarget::CommitToParent;

		std::filesystem::path Path;

		friend bool operator==(const DiffTabState&, const DiffTabState&) = default;
	};

	struct TerminalTabState
	{
		std::string Id;
		std::string ProfileName;
		std::filesystem::path WorkingDirectory;

		std::string Title;

		friend bool operator==(const TerminalTabState&, const TerminalTabState&) = default;
	};

	struct ReviewTabState
	{
		std::string Branch;

		std::filesystem::path Selected;
		bool SelectedIsUncommitted = false;

		friend bool operator==(const ReviewTabState&, const ReviewTabState&) = default;
	};

	// What the Git window shows beside its sidebar: the history of one reference, or of a worktree without a branch.
	struct GitWindowState
	{
		std::string Ref; // its full name; empty for HEAD's history

		// The path of the detached worktree whose HEAD's history is shown; empty for none. A worktree on a branch is
		// stored as its branch, in Ref.
		std::string Worktree;

		friend bool operator==(const GitWindowState&, const GitWindowState&) = default;
	};

	struct WorkspaceState
	{
		std::vector<TabState> Tabs;
		std::optional<std::filesystem::path> Active;
		std::vector<SessionTabState> Sessions;
		std::vector<DiffTabState> Diffs;
		std::vector<ReviewTabState> Reviews;

		std::optional<std::vector<TerminalTabState>> Terminals;
		GitWindowState GitWindow;

		friend bool operator==(const WorkspaceState&, const WorkspaceState&) = default;
	};
}
