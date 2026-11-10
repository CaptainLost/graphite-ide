#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/WslSharePath.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	class WslAgents;

	// A repository inside WSL asked through its distro's agent (stage 3a): the same answers as
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

	private:
		struct Answer
		{
			nlohmann::json Result;
			std::string Payload;
		};

		Core::Result<Answer> Ask(const std::string& operation, nlohmann::json arguments, std::chrono::seconds timeout);
		Core::Result<void> AskForPaths(const std::string& operation, const std::vector<std::filesystem::path>& paths);

		WslAgents& m_Agents;
		Core::WslSharePath m_Root;
		Reach m_Reach;
	};
}
