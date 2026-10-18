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

	enum class BranchSort
	{
		Newest,
		Oldest,
		Name
	};

	enum class BranchGrouping
	{
		None,
		Author
	};

	struct BranchListState
	{
		BranchSort Sort = BranchSort::Newest;
		BranchGrouping Group = BranchGrouping::None;

		friend bool operator==(const BranchListState&, const BranchListState&) = default;
	};

	struct WorkspaceState
	{
		std::vector<TabState> Tabs;
		std::optional<std::filesystem::path> Active;
		std::vector<SessionTabState> Sessions;
		std::vector<DiffTabState> Diffs;
		std::vector<ReviewTabState> Reviews;

		std::optional<std::vector<TerminalTabState>> Terminals;
		BranchListState BranchList;

		friend bool operator==(const WorkspaceState&, const WorkspaceState&) = default;
	};
}
