#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"

#include <filesystem>
#include <optional>
#include <vector>

struct git_repository;

namespace Core {

	class Repository
	{
	public:
		static Result<Repository> Open(const std::filesystem::path& anyPathInside);

		~Repository();

		Repository(Repository&& other) noexcept;
		Repository& operator=(Repository&& other) noexcept;

		Repository(const Repository&) = delete;
		Repository& operator=(const Repository&) = delete;

		bool IsIgnored(const std::filesystem::path& path) const;

		// Newest first, in topological then date order — the order a history is read in.
		// A repository whose HEAD is unborn answers with an empty, complete page, not an
		// error.
		Result<LogPage> QueryLog(const LogRequest& request) const;

		// Renames are detected rather than reported as a delete and an add. A binary file
		// comes back flagged and with no hunks.
		Result<Diff> QueryDiff(const DiffRequest& request) const;

		// Ignored files are left out: the panel lists what can be acted on, and a build
		// directory would bury everything else.
		Result<WorkingTreeStatus> QueryStatus() const;

		// Local branches first, then remote-tracking ones, each group in name order.
		Result<std::vector<Branch>> QueryBranches() const;

		// The file's bytes as they stood in one commit. Bytes rather than lines, and no
		// encoding is assumed: the caller knows whether it asked about text.
		//
		// NotFound when that commit's tree has no such path — which is the ordinary answer
		// for the older side of an added file, not a failure worth reporting to anyone.
		Result<std::string> ReadBlob(const std::filesystem::path& path, const Oid& revision) const;

		// The commit this one's diff is taken against. Empty for a root commit, which has
		// nothing before it — not an error, and the caller's answer is the same as for a
		// file that did not exist yet.
		Result<std::optional<Oid>> FirstParentOf(const Oid& commit) const;

		const std::filesystem::path& GetWorkingDirectory() const
		{
			return m_WorkingDirectory;
		}

	private:
		Repository() = default;

		git_repository* m_Handle = nullptr;
		std::filesystem::path m_WorkingDirectory;
	};
}