#include <doctest/doctest.h>

#include "Git/Worktrees/WriteExpectation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	Core::Oid IdOf(std::uint8_t byte)
	{
		Core::Oid id;
		id.Bytes.fill(byte);
		return id;
	}

	Core::HeadState OnBranch(const std::string& name, std::uint8_t commit)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = name;
		head.Commit = IdOf(commit);
		return head;
	}

	Core::HeadState Detached(std::uint8_t commit)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Detached;
		head.Commit = IdOf(commit);
		return head;
	}

	WriteExpectation Expecting(const Core::HeadState& head)
	{
		WriteExpectation expected;
		expected.Kind = head.Kind;
		expected.BranchName = head.BranchName;
		expected.Head = head.Commit;
		expected.Folder = "wt";
		return expected;
	}

	Core::WorkingTreeStatus StatusWithStaged(const std::vector<std::string>& paths)
	{
		Core::WorkingTreeStatus status;

		for (const std::string& path : paths)
		{
			Core::FileStatus entry;
			entry.Path = path;
			entry.Index = Core::StatusFlags::Modified;
			status.Entries.push_back(entry);
		}

		return status;
	}
}

TEST_CASE("Write expectation: another branch refuses every write")
{
	const WriteExpectation expected = Expecting(OnBranch("main", 1));

	CHECK(CheckExpectation(expected, OnBranch("other", 1), nullptr, false) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(expected, OnBranch("other", 1), nullptr, true) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(expected, Detached(1), nullptr, false) == ExpectationBreak::OtherHead);

	const WriteExpectation onDetached = Expecting(Detached(1));
	CHECK(CheckExpectation(onDetached, OnBranch("main", 1), nullptr, false) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(onDetached, Detached(1), nullptr, false) == ExpectationBreak::None);
}

TEST_CASE("Write expectation: a moved HEAD refuses Amend")
{
	const WriteExpectation expected = Expecting(OnBranch("main", 1));

	CHECK(CheckExpectation(expected, OnBranch("main", 2), nullptr, true) == ExpectationBreak::NewCommitBeforeAmend);
	CHECK(CheckExpectation(expected, OnBranch("main", 1), nullptr, true) == ExpectationBreak::None);
}

TEST_CASE("Write expectation: a moved HEAD lets a plain commit through")
{
	WriteExpectation expected = Expecting(OnBranch("main", 1));
	expected.Staged = std::vector<std::filesystem::path>{"a.txt", "b.txt"};

	const Core::WorkingTreeStatus status = StatusWithStaged({"b.txt", "a.txt"});

	CHECK(CheckExpectation(expected, OnBranch("main", 2), &status, false) == ExpectationBreak::None);
}

TEST_CASE("Write expectation: a staged set that changed refuses a commit")
{
	WriteExpectation expected = Expecting(OnBranch("main", 1));
	expected.Staged = std::vector<std::filesystem::path>{"a.txt", "b.txt"};

	const Core::WorkingTreeStatus extra = StatusWithStaged({"a.txt", "b.txt", "c.txt"});
	const Core::WorkingTreeStatus missing = StatusWithStaged({"a.txt"});

	CHECK(CheckExpectation(expected, OnBranch("main", 1), &extra, false) == ExpectationBreak::StagedChanged);
	CHECK(CheckExpectation(expected, OnBranch("main", 1), &missing, false) == ExpectationBreak::StagedChanged);
	CHECK(CheckExpectation(expected, OnBranch("main", 1), &missing, true) == ExpectationBreak::StagedChanged);
}

TEST_CASE("Write expectation: stage and unstage check only the head")
{
	const WriteExpectation expected = Expecting(OnBranch("main", 1));
	const Core::WorkingTreeStatus status = StatusWithStaged({"x.txt"});

	CHECK(CheckExpectation(expected, OnBranch("main", 2), &status, false) == ExpectationBreak::None);
	CHECK(CheckExpectation(expected, OnBranch("other", 1), &status, false) == ExpectationBreak::OtherHead);
}

TEST_CASE("Write expectation: refusals read as plain sentences naming the folder")
{
	CHECK(RefusalMessage(ExpectationBreak::NewCommitBeforeAmend, ExpectedWrite::Commit, "eager-scribbling-frog") ==
		  "Not committed: eager-scribbling-frog got a new commit since you looked");
	CHECK(RefusalMessage(ExpectationBreak::OtherHead, ExpectedWrite::Commit, "wt") ==
		  "Not committed: wt has another branch or a detached HEAD checked out since you looked");
	CHECK(RefusalMessage(ExpectationBreak::StagedChanged, ExpectedWrite::Commit, "wt") ==
		  "Not committed: the staged files in wt changed since you looked");
	CHECK(RefusalMessage(ExpectationBreak::OtherHead, ExpectedWrite::Stage, "wt") ==
		  "Not staged: wt has another branch or a detached HEAD checked out since you looked");
	CHECK(RefusalMessage(ExpectationBreak::OtherHead, ExpectedWrite::Unstage, "wt") ==
		  "Not unstaged: wt has another branch or a detached HEAD checked out since you looked");
}

TEST_CASE("Write expectation: a busy worktree names its folder")
{
	CHECK(BusyMessage("wt") == "Git is busy in wt; try again");
}

TEST_CASE("Write expectation: only libgit2's index lock failure counts as busy")
{
	CHECK(IsIndexLocked(Core::Error{Core::ErrorCode::IoFailure, "failed to lock file '.git/worktrees/x/index.lock'"}));
	CHECK_FALSE(IsIndexLocked(Core::Error{Core::ErrorCode::IoFailure, "stage failed"}));
}

TEST_CASE("Write expectation: a detached HEAD that moved refuses every write")
{
	WriteExpectation expected = Expecting(Detached(1));
	expected.Staged = std::vector<std::filesystem::path>{};
	const Core::WorkingTreeStatus status = StatusWithStaged({});

	CHECK(CheckExpectation(expected, Detached(2), nullptr, false) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(expected, Detached(2), &status, false) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(expected, Detached(2), &status, true) == ExpectationBreak::OtherHead);
	CHECK(CheckExpectation(expected, Detached(1), &status, false) == ExpectationBreak::None);
}
