#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Oid.h"
#include "Core/Git/OwnAuthors.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"
#include "Core/Git/Tag.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Graphite {

	class WslAgents;

	// A repository inside WSL asked through its distro's agent (stages 3a, 3b): the same answers as
	// Core::Repository gives the host's port, with absolute paths in the share spelling the host
	// names them by and relative ones as git spells them.
	class WslGit
	{
	public:
		enum class Reach
		{
			SearchUpward,
			Exactly
		};

		WslGit(WslAgents& agents, Core::WslSharePath root, Reach reach);

		Core::Result<Core::WorkingTreeStatus> QueryStatus();
		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(const std::filesystem::path& directory);
		Core::Result<std::filesystem::path> GetWorkingDirectory();
		Core::Result<Core::HeadState> QueryHead();
		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request);

		enum class Side
		{
			Head,
			Index,
			WorkTree
		};

		// Each read's bytes, or empty where git has no such file on that side.
		Core::Result<std::vector<std::optional<std::string>>> ReadSides(const std::vector<std::pair<Side, std::filesystem::path>>& reads);

		struct Bases
		{
			bool Trackable = false;
			std::optional<std::string> Head;
			std::optional<std::string> Index;
		};

		// absolutePath in the share spelling.
		Core::Result<Bases> ReadBases(const std::filesystem::path& absolutePath);

		Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths);
		Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths);
		Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths);
		Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths);
		Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles);
		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request);
		Core::Result<void> Init();

		Core::Result<std::vector<Core::Branch>> QueryBranches();
		Core::Result<std::vector<Core::Tag>> QueryTags();
		Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest& request);
		Core::Result<std::optional<std::string>> ReadUserEmail();
		Core::Result<std::vector<Core::Remote>> QueryRemotes();
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request);

		// The tip ref (HEAD without one) names, and its count of commits unless it is knownTip,
		// whose count the caller has.
		Core::Result<std::pair<Core::Oid, std::optional<std::size_t>>> CountCommits(const std::optional<std::string>& ref, Core::FollowedParents parents,
																				  const std::optional<Core::Oid>& knownTip);

		// A commit's file, new side then old side, each empty where git has none (ReadFileTexts' rule).
		Core::Result<std::pair<std::optional<std::string>, std::optional<std::string>>> ReadCommitSides(const Core::Oid& commit,
																										 const std::filesystem::path& oldPath,
																										 const std::filesystem::path& newPath);

		Core::Result<void> CreateBranch(const std::string& name, const Core::Oid& from);
		Core::Result<void> RenameBranch(const std::string& from, const std::string& to);
		Core::Result<std::size_t> CountUnmergedCommits(const std::string& name);
		Core::Result<void> DeleteBranch(const std::string& name);

		Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles);
		Core::Result<std::vector<Core::StashEntry>> QueryStashes();
		Core::Result<void> StashApply(const Core::Oid& id);
		Core::Result<void> StashPop(const Core::Oid& id);
		Core::Result<void> StashDrop(const Core::Oid& id);

		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target);

		// A request that failed before the agent answered is a Failure with nothing changed.
		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target);

		// The directory the repository's worktrees share, in the share spelling.
		Core::Result<std::filesystem::path> GetCommonDirectory();

		// The lines graph's reads (stage 3b-lines).
		Core::Result<std::vector<Core::GraphCommit>> ReadGraphCommits(const Core::GraphReadRequest& request);
		Core::Result<std::vector<bool>> HoldsCommits(const std::vector<Core::Oid>& commits);
		Core::Result<std::unordered_set<Core::Oid>> ShallowBoundary();

		// What the Explorer needs once per folder, in the share spelling. The Explorer's questions
		// go to the agent's quick lane and wait at most startWait for an agent being started.
		struct IgnoreSetup
		{
			std::filesystem::path WorkingDirectory;
			std::vector<std::filesystem::path> RuleFiles;
		};

		Core::Result<IgnoreSetup> QueryIgnoreSetup(std::chrono::milliseconds startWait);

		// Whether git ignores each entry of folder (a share path), in the entries' order.
		Core::Result<std::vector<bool>> QueryIgnored(const std::filesystem::path& folder, const std::vector<Core::DirectoryEntry>& entries,
													 std::chrono::milliseconds startWait);

	private:
		struct Answer
		{
			nlohmann::json Result;
			std::string Payload;
		};

		Core::Result<Answer> Ask(const std::string& operation, nlohmann::json arguments, std::chrono::milliseconds timeout,
								 std::chrono::milliseconds startWait);
		Core::Result<Answer> Ask(const std::string& operation, nlohmann::json arguments);
		Core::Result<void> AskForDone(const std::string& operation, nlohmann::json arguments);
		Core::Result<nlohmann::json> AskForPayload(const std::string& operation, nlohmann::json arguments);
		std::filesystem::path SharePathOf(const std::string& linuxPath) const;
		Core::Result<void> AskForPaths(const std::string& operation, const std::vector<std::filesystem::path>& paths);

		WslAgents& m_Agents;
		Core::WslSharePath m_Root;
		Reach m_Reach;

		// The root's common directory, once the agent named it: it never changes for a root.
		std::mutex m_CommonMutex;
		std::optional<std::filesystem::path> m_CommonDirectory;
	};
}
