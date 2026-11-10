#include <doctest/doctest.h>

#include "Core/Remote/GitCodec.h"

#include <filesystem>
#include <string>

namespace {

	const Core::Oid kSomeCommit = *Core::Oid::FromHex("0123456789abcdef0123456789abcdef01234567");
	const Core::Oid kOtherCommit = *Core::Oid::FromHex("89abcdef0123456789abcdef0123456789abcdef");

	std::filesystem::path Utf8Path(const std::string& text)
	{
		return std::filesystem::path{std::u8string{text.begin(), text.end()}};
	}
}

TEST_CASE("A working tree's status round-trips with renames, flags and odd names")
{
	Core::WorkingTreeStatus status;
	status.Entries.push_back(Core::FileStatus{.Path = Utf8Path("dir/a b.txt"),
											  .RenamedFrom = Utf8Path("dir/za\xC5\xBC.txt"),
											  .Index = Core::StatusFlags::Renamed,
											  .WorkTree = Core::StatusFlags::Modified | Core::StatusFlags::Deleted,
											  .IsConflicted = true});
	status.Entries.push_back(Core::FileStatus{.Path = Utf8Path("-leading.txt"), .IsUntracked = true, .IsDirectory = true, .HoldsCheckout = true});

	const Core::Result<Core::WorkingTreeStatus> back = Core::WorkingTreeStatusFromJson(Core::ToJson(status));

	REQUIRE(back.HasValue());
	REQUIRE(back.GetValue().Entries.size() == 2);
	const Core::FileStatus& renamed = back.GetValue().Entries[0];
	CHECK(renamed.Path == Utf8Path("dir/a b.txt"));
	CHECK(renamed.RenamedFrom == std::optional{Utf8Path("dir/za\xC5\xBC.txt")});
	CHECK(renamed.Index == Core::StatusFlags::Renamed);
	CHECK(renamed.WorkTree == (Core::StatusFlags::Modified | Core::StatusFlags::Deleted));
	CHECK(renamed.IsConflicted);
	const Core::FileStatus& untracked = back.GetValue().Entries[1];
	CHECK(untracked.IsUntracked);
	CHECK(untracked.IsDirectory);
	CHECK(untracked.HoldsCheckout);
	CHECK_FALSE(untracked.RenamedFrom.has_value());
}

TEST_CASE("HEAD round-trips on a branch, detached and unborn")
{
	for (const Core::HeadState& head : {Core::HeadState{.Kind = Core::HeadKind::Branch, .BranchName = "main", .Commit = kSomeCommit},
										Core::HeadState{.Kind = Core::HeadKind::Detached, .Commit = kSomeCommit, .Operation = Core::RepositoryOperation::Rebase},
										Core::HeadState{.Kind = Core::HeadKind::Unborn, .BranchName = "main"}})
	{
		const Core::Result<Core::HeadState> back = Core::HeadStateFromJson(Core::ToJson(head));
		REQUIRE(back.HasValue());
		CHECK(back.GetValue() == head);
	}
}

TEST_CASE("A diff request round-trips with its revisions, file and whitespace")
{
	const Core::DiffRequest request{.Target = Core::DiffTarget::RevisionToRevision,
									.From = kSomeCommit,
									.To = kOtherCommit,
									.File = Utf8Path("src/a b.cpp"),
									.Whitespace = Core::WhitespaceInDiff::Ignored};

	const Core::Result<Core::DiffRequest> back = Core::DiffRequestFromJson(Core::ToJson(request));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue() == request);
}

TEST_CASE("A diff round-trips with its files, hunks and lines, binary files and renames included")
{
	Core::Diff diff;
	diff.Files.push_back(Core::FileDiff{.OldPath = Utf8Path("old name.txt"),
										.NewPath = Utf8Path("new name.txt"),
										.Change = Core::FileChange::Renamed,
										.OldId = kSomeCommit,
										.NewId = kOtherCommit,
										.Hunks = {Core::Hunk{.OldStart = 1,
															 .OldCount = 2,
															 .NewStart = 1,
															 .NewCount = 2,
															 .Header = "@@ -1,2 +1,2 @@",
															 .Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Removed, .OldLine = 1, .Text = "old\n"},
																	   Core::DiffLine{.Kind = Core::DiffLineKind::Added, .NewLine = 1, .Text = "new\n"}}}}});
	diff.Files.push_back(Core::FileDiff{.OldPath = Utf8Path("image.png"), .NewPath = Utf8Path("image.png"), .Change = Core::FileChange::Modified, .IsBinary = true});

	const Core::Result<Core::Diff> back = Core::DiffFromJson(Core::ToJson(diff));

	REQUIRE(back.HasValue());
	REQUIRE(back.GetValue().Files.size() == 2);
	const Core::FileDiff& renamed = back.GetValue().Files[0];
	CHECK(renamed.NewPath == Utf8Path("new name.txt"));
	CHECK(renamed.Change == Core::FileChange::Renamed);
	CHECK(renamed.OldId == kSomeCommit);
	REQUIRE(renamed.Hunks.size() == 1);
	CHECK(renamed.Hunks[0].Header == "@@ -1,2 +1,2 @@");
	REQUIRE(renamed.Hunks[0].Lines.size() == 2);
	CHECK(renamed.Hunks[0].Lines[1].Kind == Core::DiffLineKind::Added);
	CHECK(renamed.Hunks[0].Lines[1].Text == "new\n");
	CHECK(back.GetValue().Files[1].IsBinary);
}

TEST_CASE("A commit request round-trips")
{
	const Core::Result<Core::CommitRequest> back = Core::CommitRequestFromJson(Core::ToJson(Core::CommitRequest{.Message = "fix: x\n\nbody", .Amend = true}));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue().Message == "fix: x\n\nbody");
	CHECK(back.GetValue().Amend);
}

TEST_CASE("A field of the wrong type is an invalid argument that names the field")
{
	nlohmann::json head = Core::ToJson(Core::HeadState{.Kind = Core::HeadKind::Branch, .BranchName = "main"});
	head["branch"] = 7;

	const Core::Result<Core::HeadState> back = Core::HeadStateFromJson(head);

	REQUIRE_FALSE(back.HasValue());
	CHECK(back.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(back.GetError().Message.find("branch") != std::string::npos);
}
