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

		// Adds each path to the index as it stands on disk, which is what covers all three
		// cases git otherwise names separately: a new file, an edited one, and a deleted
		// one. Splitting them would push "this file is deleted, so a different call
		// applies" into every caller, which is knowledge the caller does not have and
		// should not acquire.
		//
		// A conflicted path staged this way stops being conflicted — that is how git
		// records a resolution.
		//
		// Paths are relative to the working directory, as FileStatus::Path is.
		Result<void> Stage(const std::vector<std::filesystem::path>& paths);

		// Puts each path back as HEAD has it, leaving the working tree alone. On an unborn
		// HEAD there is nothing to go back to, so the entry is removed from the index
		// instead — the same answer git gives, and not an error.
		Result<void> Unstage(const std::vector<std::filesystem::path>& paths);

		// Overwrites the working tree from the index. Irreversible: what it discards was
		// never in git, so there is nothing to recover it from.
		//
		// Untracked paths are not touched — a path git has never seen has nothing in the
		// index to be restored from. Deleting those is DeleteUntracked, deliberately a
		// separate call: one restores a state git knows, the other destroys a file git
		// does not, and a caller that means one rarely means both.
		Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths);

		// Deletes untracked files outright. A path that cannot be removed — a non-empty
		// directory, a file held open by another process — is reported rather than
		// skipped.
		Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths);

		// Writes the index as a commit on HEAD and answers with its id, so a caller has
		// something to show and a test something to assert beyond the absence of an error.
		//
		// Author and committer come from git's own config resolution; a repository with no
		// user.name or user.email fails here rather than inventing an identity for the
		// history.
		Result<Oid> Commit(const CommitRequest& request);

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