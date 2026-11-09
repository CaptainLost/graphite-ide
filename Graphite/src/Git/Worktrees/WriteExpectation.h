#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Status.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// What the user saw of a worktree when they asked for a write into it. Another process (an agent) may stage, commit or
	// switch branches in that worktree at any moment, so the write checks this against the worktree first and refuses when
	// it no longer holds, rather than record work the user never looked at.
	struct WriteExpectation
	{
		Core::HeadKind Kind = Core::HeadKind::Branch;

		// As Core::HeadState::BranchName.
		std::string BranchName;

		std::optional<Core::Oid> Head;

		// Sorted. Set for a commit and an amend, which record exactly what is staged.
		std::optional<std::vector<std::filesystem::path>> Staged;

		// The worktree's folder name, for messages.
		std::string Folder;

		// The worktree's folder, which the write's port must open as its working directory; empty checks nothing.
		std::filesystem::path Worktree;

		friend bool operator==(const WriteExpectation&, const WriteExpectation&) = default;
	};

	enum class ExpectationBreak
	{
		None,
		OtherFolder,
		OtherHead,
		NewCommitBeforeAmend,
		StagedChanged
	};

	// statusNow may be null when the caller did not read it; the staged set is then not checked.
	ExpectationBreak CheckExpectation(const WriteExpectation& expected, const Core::HeadState& now,
									  const Core::WorkingTreeStatus* statusNow, bool amend);

	// The paths with something staged in the index, sorted, as WriteExpectation::Staged holds them.
	std::vector<std::filesystem::path> StagedPaths(const Core::WorkingTreeStatus& status);

	// Declared apart from GitModel::WriteKind: GitModel.h includes this header.
	enum class ExpectedWrite
	{
		Stage,
		Unstage,
		Commit
	};

	// "Not committed: ..." for a commit or an amend, "Not staged: ..." or "Not unstaged: ..." otherwise.
	std::string RefusalMessage(ExpectationBreak broken, ExpectedWrite write, std::string_view folder);

	// libgit2's failure to take the index lock, which another git process holds while it writes.
	bool IsIndexLocked(const Core::Error& error);

	std::string BusyMessage(std::string_view folder);
}
