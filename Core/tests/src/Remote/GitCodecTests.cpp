#include <doctest/doctest.h>

#include "Core/Remote/GitCodec.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

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

namespace {

	Core::CommitSummary SampleCommitSummary()
	{
		return Core::CommitSummary{
			.Id = kSomeCommit,
			.Parents = {kOtherCommit, kSomeCommit},
			.Author = {.Name = "Anna", .Email = "anna@example.com", .When = std::chrono::sys_seconds{std::chrono::seconds{1700000000}}},
			.Committer = {.Name = "Bob", .Email = "bob@example.com", .When = std::chrono::sys_seconds{std::chrono::seconds{1700000500}}},
			.Summary = "fix: x",
			.Body = "the body\nof it",
		};
	}
}

TEST_CASE("A branch list round-trips with upstreams, distances and the tip's commit")
{
	const std::vector<Core::Branch> branches{
		Core::Branch{.Name = "main",
					 .FullName = "refs/heads/main",
					 .Kind = Core::RefKind::Local,
					 .Tip = kSomeCommit,
					 .IsHead = true,
					 .Upstream = "origin/main",
					 .Ahead = 2,
					 .Behind = 3,
					 .DistanceKnown = true,
					 .TipCommit = SampleCommitSummary()},
		Core::Branch{.Name = "origin/gone",
					 .FullName = "refs/remotes/origin/gone",
					 .Kind = Core::RefKind::Remote,
					 .Tip = kOtherCommit,
					 .IsRemoteDefault = true,
					 .MissingUpstream = "origin/x"},
	};

	const Core::Result<std::vector<Core::Branch>> back = Core::BranchesFromJson(Core::ToJsonList(branches));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue() == branches);
}

TEST_CASE("A branch list with a mistyped tip is refused, naming the field")
{
	nlohmann::json listed = Core::ToJsonList(std::vector<Core::Branch>{Core::Branch{.Name = "main", .FullName = "refs/heads/main", .Tip = kSomeCommit}});
	listed[0]["tip"] = "zz";

	const Core::Result<std::vector<Core::Branch>> back = Core::BranchesFromJson(listed);

	REQUIRE_FALSE(back.HasValue());
	CHECK(back.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(back.GetError().Message.find("tip") != std::string::npos);
}

TEST_CASE("Tags and remotes round-trip")
{
	const std::vector<Core::Tag> tags{
		Core::Tag{.Name = "v1",
				  .FullName = "refs/tags/v1",
				  .Target = kSomeCommit,
				  .When = std::chrono::sys_seconds{std::chrono::seconds{42}},
				  .Annotated = true,
				  .Subject = "first"},
		Core::Tag{.Name = "light", .FullName = "refs/tags/light", .Target = kOtherCommit},
	};
	const std::vector<Core::Remote> remotes{Core::Remote{.Name = "origin", .Url = "git@example.com:a/b.git"}, Core::Remote{.Name = "bare"}};

	const Core::Result<std::vector<Core::Tag>> tagsBack = Core::TagsFromJson(Core::ToJsonList(tags));
	const Core::Result<std::vector<Core::Remote>> remotesBack = Core::RemotesFromJson(Core::ToJsonList(remotes));

	REQUIRE(tagsBack.HasValue());
	CHECK(tagsBack.GetValue() == tags);
	REQUIRE(remotesBack.HasValue());
	CHECK(remotesBack.GetValue() == remotes);
}

TEST_CASE("A log request round-trips with every field it can carry")
{
	const Core::LogRequest request{
		.Ref = "refs/heads/main",
		.From = kSomeCommit,
		.AlsoFrom = {kOtherCommit},
		.FirstParentOnly = true,
		.Exclude = kOtherCommit,
		.Stash = kSomeCommit,
		.File = Utf8Path("dir/a b.txt"),
		.Lines = Core::OneBasedLineRange{.First = 3, .Last = 7},
		.Filter = {.Text = "kept width"},
		.FollowRenames = false,
		.MaxWalk = 123,
		.MaxCount = 45,
	};

	const Core::Result<Core::LogRequest> back = Core::LogRequestFromJson(Core::ToJson(request));

	REQUIRE(back.HasValue());
	const Core::LogRequest& got = back.GetValue();
	CHECK(got.Ref == request.Ref);
	CHECK(got.From == request.From);
	CHECK(got.AlsoFrom == request.AlsoFrom);
	CHECK(got.FirstParentOnly);
	CHECK(got.Exclude == request.Exclude);
	CHECK(got.Stash == request.Stash);
	CHECK(got.File == request.File);
	REQUIRE(got.Lines.has_value());
	CHECK(got.Lines->First == 3);
	CHECK(got.Lines->Last == 7);
	CHECK(got.Filter.Text == "kept width");
	CHECK_FALSE(got.FollowRenames);
	CHECK(got.MaxWalk == 123);
	CHECK(got.MaxCount == 45);
}

TEST_CASE("A plain log request round-trips without the fields it leaves out")
{
	const Core::Result<Core::LogRequest> back = Core::LogRequestFromJson(Core::ToJson(Core::LogRequest{}));

	REQUIRE(back.HasValue());
	CHECK_FALSE(back.GetValue().Ref.has_value());
	CHECK_FALSE(back.GetValue().From.has_value());
	CHECK_FALSE(back.GetValue().File.has_value());
	CHECK_FALSE(back.GetValue().Lines.has_value());
	CHECK(back.GetValue().FollowRenames);
	CHECK(back.GetValue().MaxCount == Core::kLogPageSize);
}

TEST_CASE("A log page round-trips with its commits, unpushed set and completeness")
{
	Core::LogPage page;
	page.Commits = {SampleCommitSummary()};
	page.Complete = false;
	page.Walked = 812;
	page.Unpushed = {kSomeCommit, kOtherCommit};
	page.UpstreamKnown = true;

	const Core::Result<Core::LogPage> back = Core::LogPageFromJson(Core::ToJson(page));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue().Commits == page.Commits);
	CHECK_FALSE(back.GetValue().Complete);
	CHECK(back.GetValue().Walked == 812);
	CHECK(back.GetValue().Unpushed == page.Unpushed);
	CHECK(back.GetValue().UpstreamKnown);
}

TEST_CASE("An own-authors request and its answer round-trip")
{
	const Core::OwnAuthorsRequest request{.Tips = {kSomeCommit, kSomeCommit}, .Hidden = {kOtherCommit}, .MaxCommitsPerTip = 9};
	const std::vector<Core::TipAuthors> answer{Core::TipAuthors{
		.Tip = kSomeCommit,
		.Authors = {Core::Signature{.Name = "Anna", .Email = "anna@example.com", .When = std::chrono::sys_seconds{std::chrono::seconds{5}}}},
		.OwnCommits = 4,
		.Truncated = true}};

	const Core::Result<Core::OwnAuthorsRequest> requestBack = Core::OwnAuthorsRequestFromJson(Core::ToJson(request));
	const Core::Result<std::vector<Core::TipAuthors>> answerBack = Core::TipAuthorsFromJson(Core::ToJsonList(answer));

	REQUIRE(requestBack.HasValue());
	CHECK(requestBack.GetValue() == request);
	REQUIRE(answerBack.HasValue());
	REQUIRE(answerBack.GetValue().size() == 1);
	CHECK(answerBack.GetValue()[0].Tip == kSomeCommit);
	CHECK(answerBack.GetValue()[0].Authors == answer[0].Authors);
	CHECK(answerBack.GetValue()[0].OwnCommits == 4);
	CHECK(answerBack.GetValue()[0].Truncated);
}

TEST_CASE("Stashes round-trip")
{
	const std::vector<Core::StashEntry> stashes{Core::StashEntry{.Index = 1, .Message = "On main: x", .Id = kSomeCommit, .SecondsSinceEpoch = 77}};

	const Core::Result<std::vector<Core::StashEntry>> back = Core::StashesFromJson(Core::ToJsonList(stashes));

	REQUIRE(back.HasValue());
	REQUIRE(back.GetValue().size() == 1);
	CHECK(back.GetValue()[0].Index == 1);
	CHECK(back.GetValue()[0].Message == "On main: x");
	CHECK(back.GetValue()[0].Id == kSomeCommit);
	CHECK(back.GetValue()[0].SecondsSinceEpoch == 77);
}

TEST_CASE("A checkout's target, plan and outcome round-trip, a half-done outcome keeping both its paths and its failure")
{
	const Core::CheckoutTarget target{.Kind = Core::CheckoutKind::Commit, .Name = "x", .Commit = kOtherCommit};
	const Core::CheckoutPlan plan{.Overwritten = {Utf8Path("a b.txt")}, .Updated = {Utf8Path("dir/c.txt"), Utf8Path("d.txt")}};
	const Core::CheckoutOutcome outcome{.Changed = {Utf8Path("dir/c.txt")}, .Failure = Core::Error{Core::ErrorCode::IoFailure, "disk full"}};

	const Core::Result<Core::CheckoutTarget> targetBack = Core::CheckoutTargetFromJson(Core::ToJson(target));
	const Core::Result<Core::CheckoutPlan> planBack = Core::CheckoutPlanFromJson(Core::ToJson(plan));
	const Core::Result<Core::CheckoutOutcome> outcomeBack = Core::CheckoutOutcomeFromJson(Core::ToJson(outcome));

	REQUIRE(targetBack.HasValue());
	CHECK(targetBack.GetValue() == target);
	REQUIRE(planBack.HasValue());
	CHECK(planBack.GetValue().Overwritten == plan.Overwritten);
	CHECK(planBack.GetValue().Updated == plan.Updated);
	REQUIRE(outcomeBack.HasValue());
	CHECK(outcomeBack.GetValue().Changed == outcome.Changed);
	REQUIRE(outcomeBack.GetValue().Failure.has_value());
	CHECK(outcomeBack.GetValue().Failure->Code == Core::ErrorCode::IoFailure);
	CHECK(outcomeBack.GetValue().Failure->Message == "disk full");

	const Core::Result<Core::CheckoutOutcome> cleanBack = Core::CheckoutOutcomeFromJson(Core::ToJson(Core::CheckoutOutcome{}));
	REQUIRE(cleanBack.HasValue());
	CHECK_FALSE(cleanBack.GetValue().Failure.has_value());
}

TEST_CASE("A blame round-trips with its hunks and the commits they name")
{
	Core::Blame blame;
	blame.At = kSomeCommit;
	blame.File = Utf8Path("dir/a b.txt");
	blame.LineCount = 5;
	blame.Hunks = {Core::BlameHunk{.FirstLine = 1, .LineCount = 2, .Commit = kSomeCommit, .OriginalPath = Utf8Path("old/a.txt"), .OriginalFirstLine = 3},
				   Core::BlameHunk{.FirstLine = 3, .LineCount = 3, .Commit = kOtherCommit, .OriginalPath = Utf8Path("dir/a b.txt"), .OriginalFirstLine = 1}};
	blame.Commits[kSomeCommit] = SampleCommitSummary();
	Core::CommitSummary other = SampleCommitSummary();
	other.Id = kOtherCommit;
	blame.Commits[kOtherCommit] = other;

	const Core::Result<Core::Blame> back = Core::BlameFromJson(Core::ToJson(blame));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue().At == blame.At);
	CHECK(back.GetValue().File == blame.File);
	CHECK(back.GetValue().LineCount == 5);
	REQUIRE(back.GetValue().Hunks.size() == 2);
	CHECK(back.GetValue().Hunks[0].OriginalPath == Utf8Path("old/a.txt"));
	CHECK(back.GetValue().Hunks[0].OriginalFirstLine == 3);
	CHECK(back.GetValue().Hunks[1].Commit == kOtherCommit);
	CHECK(back.GetValue().Commits == blame.Commits);
}

TEST_CASE("A line history and its request round-trip")
{
	const Core::LineHistoryRequest request{.From = kSomeCommit, .File = Utf8Path("a.txt"), .Line = 12, .MaxWalk = 99};
	Core::LineHistory history;
	history.Complete = false;
	history.Changes.push_back(Core::LineChange{
		.Commit = SampleCommitSummary(),
		.Path = Utf8Path("a.txt"),
		.Line = 12,
		.ParentLine = 10,
		.Hunks = {Core::Hunk{.OldStart = 9, .OldCount = 2, .NewStart = 11, .NewCount = 3, .Header = "@@ -9,2 +11,3 @@",
							 .Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Added, .OldLine = -1, .NewLine = 12, .Text = "x"}}}}});
	history.Changes.push_back(Core::LineChange{.Commit = SampleCommitSummary(), .Path = Utf8Path("b.txt"), .Line = 4});

	const Core::Result<Core::LineHistoryRequest> requestBack = Core::LineHistoryRequestFromJson(Core::ToJson(request));
	const Core::Result<Core::LineHistory> historyBack = Core::LineHistoryFromJson(Core::ToJson(history));

	REQUIRE(requestBack.HasValue());
	CHECK(requestBack.GetValue() == request);
	REQUIRE(historyBack.HasValue());
	CHECK_FALSE(historyBack.GetValue().Complete);
	REQUIRE(historyBack.GetValue().Changes.size() == 2);
	const Core::LineChange& first = historyBack.GetValue().Changes[0];
	CHECK(first.ParentLine == std::optional<std::size_t>{10});
	REQUIRE(first.Hunks.size() == 1);
	CHECK(first.Hunks[0].Header == "@@ -9,2 +11,3 @@");
	REQUIRE(first.Hunks[0].Lines.size() == 1);
	CHECK(first.Hunks[0].Lines[0].NewLine == 12);
	CHECK_FALSE(historyBack.GetValue().Changes[1].ParentLine.has_value());
	CHECK(historyBack.GetValue().Changes[1].Path == Utf8Path("b.txt"));
}

TEST_CASE("A worktree list round-trips with each worktree's HEAD and lock")
{
	const std::vector<Core::WorktreeEntry> worktrees{
		Core::WorktreeEntry{.Path = Utf8Path("/home/me/p"), .Name = "p", .IsMain = true,
							.Head = Core::HeadState{.Kind = Core::HeadKind::Branch, .BranchName = "main", .Commit = kSomeCommit}},
		Core::WorktreeEntry{.Path = Utf8Path("/home/me/wt"), .Name = "wt", .Head = Core::HeadState{.Kind = Core::HeadKind::Detached, .Commit = kOtherCommit},
							.Locked = true, .LockReason = "on a stick", .FolderMissing = true},
	};

	const Core::Result<std::vector<Core::WorktreeEntry>> back = Core::WorktreesFromJson(Core::ToJsonList(worktrees));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue() == worktrees);
}

TEST_CASE("A content verdict and a patch twin search round-trip")
{
	const Core::ContentVerdict verdict{.Outcome = Core::ContentOutcome::Merged,
									   .Kind = Core::ContentKind::Rebase,
									   .Landing = kOtherCommit,
									   .Twins = {Core::PatchTwin{.Own = kSomeCommit, .Twin = kOtherCommit}}};
	const Core::PatchTwinSearch search{.Twin = kSomeCommit, .MoreToRead = true};

	const Core::Result<Core::ContentVerdict> verdictBack = Core::ContentVerdictFromJson(Core::ToJson(verdict));
	const Core::Result<Core::PatchTwinSearch> searchBack = Core::PatchTwinSearchFromJson(Core::ToJson(search));
	const Core::Result<Core::ContentVerdict> unknownBack = Core::ContentVerdictFromJson(Core::ToJson(Core::ContentVerdict{.Outcome = Core::ContentOutcome::Unknown}));

	REQUIRE(verdictBack.HasValue());
	CHECK(verdictBack.GetValue() == verdict);
	REQUIRE(searchBack.HasValue());
	CHECK(searchBack.GetValue() == search);
	REQUIRE(unknownBack.HasValue());
	CHECK(unknownBack.GetValue().Outcome == Core::ContentOutcome::Unknown);
	CHECK_FALSE(unknownBack.GetValue().Landing.has_value());
}
