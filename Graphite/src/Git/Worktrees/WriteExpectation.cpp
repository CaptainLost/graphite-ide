#include "Git/Worktrees/WriteExpectation.h"

#include <algorithm>

namespace Graphite {

	namespace {

		std::string Reason(ExpectationBreak broken, std::string_view folder)
		{
			switch (broken)
			{
			case ExpectationBreak::OtherHead:
				return std::string{folder} + " has another branch or a detached HEAD checked out since you looked";
			case ExpectationBreak::NewCommitBeforeAmend:
				return std::string{folder} + " got a new commit since you looked";
			case ExpectationBreak::StagedChanged:
				return "the staged files in " + std::string{folder} + " changed since you looked";
			case ExpectationBreak::None:
				break;
			}

			return {};
		}

		const char* Prefix(ExpectedWrite write)
		{
			switch (write)
			{
			case ExpectedWrite::Stage:
				return "Not staged: ";
			case ExpectedWrite::Unstage:
				return "Not unstaged: ";
			case ExpectedWrite::Commit:
				break;
			}

			return "Not committed: ";
		}
	}

	std::vector<std::filesystem::path> StagedPaths(const Core::WorkingTreeStatus& status)
	{
		std::vector<std::filesystem::path> staged;

		for (const Core::FileStatus& entry : status.Entries)
		{
			if (entry.Index != Core::StatusFlags::None)
			{
				staged.push_back(entry.Path);
			}
		}

		std::sort(staged.begin(), staged.end());
		return staged;
	}

	ExpectationBreak CheckExpectation(const WriteExpectation& expected, const Core::HeadState& now,
									  const Core::WorkingTreeStatus* statusNow, bool amend)
	{
		// A detached HEAD has no branch to commit on top of, so a different commit is a different place.
		const bool detachedMoved = expected.Kind == Core::HeadKind::Detached && now.Commit != expected.Head;

		if (now.Kind != expected.Kind || now.BranchName != expected.BranchName || detachedMoved)
		{
			return ExpectationBreak::OtherHead;
		}

		if (amend && now.Commit != expected.Head)
		{
			return ExpectationBreak::NewCommitBeforeAmend;
		}

		if (expected.Staged.has_value() && statusNow != nullptr && StagedPaths(*statusNow) != *expected.Staged)
		{
			return ExpectationBreak::StagedChanged;
		}

		return ExpectationBreak::None;
	}

	std::string RefusalMessage(ExpectationBreak broken, ExpectedWrite write, std::string_view folder)
	{
		return Prefix(write) + Reason(broken, folder);
	}

	bool IsIndexLocked(const Core::Error& error)
	{
		// libgit2 words a held lock file as "the index is locked; this might be due to a concurrent or crashed process" and
		// does not name the file, so a user file called index.lock cannot be mistaken for it.
		return error.Message.find("the index is locked") != std::string::npos;
	}

	std::string BusyMessage(std::string_view folder)
	{
		return "Git is busy in " + std::string{folder} + "; try again";
	}
}
