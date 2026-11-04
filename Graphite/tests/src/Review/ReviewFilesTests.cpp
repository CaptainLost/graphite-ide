#include <doctest/doctest.h>

#include "Git/Lines/LineReadFixtures.h"
#include "Review/ReviewFiles.h"
#include "Review/ReviewSessions.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::ListStep;
using Graphite::ReviewGroup;
using Graphite::ReviewGroups;

namespace {

	constexpr std::uint8_t kFirstIdByte = 0x77;

	Core::FileDiff FileAt(const std::filesystem::path& path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}

	Core::FileDiff ChangedFileAt(const std::filesystem::path& path)
	{
		Core::FileDiff file = FileAt(path);
		file.OldId.Bytes[0] = 1;
		file.NewId.Bytes[0] = 2;
		return file;
	}

	Core::Diff DiffOfFiles(const std::vector<std::filesystem::path>& paths)
	{
		Core::Diff diff;

		for (const std::filesystem::path& path : paths)
		{
			diff.Files.push_back(FileAt(path));
		}

		return diff;
	}
}

TEST_CASE("Git window: a branch is reviewed against its line")
{
	Graphite::Fixtures::LineReadBuilder builder;
	const std::size_t main = builder.AddLocal("main");
	const std::size_t side = builder.AddLocal("side");
	builder.MakeBase(main);
	builder.Compare(side, main);
	const Graphite::LineRead read = builder.Build();
	const Graphite::BranchLines lines(read);
	const std::vector<Core::Branch>& branches = *read.Branches;

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[side], branches, &lines);

	REQUIRE(offer.has_value());
	CHECK(offer->Branch == "refs/heads/side");
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("Git window: a base is reviewed against its upstream")
{
	Graphite::Fixtures::LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddLocal("main");
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	const std::size_t alone = builder.AddLocal("alone");
	builder.MakeBase(main);
	builder.MakeBase(alone);
	builder.Track(main, originMain);
	const Graphite::LineRead read = builder.Build();
	const Graphite::BranchLines lines(read);
	const std::vector<Core::Branch>& branches = *read.Branches;

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[main], branches, &lines);

	REQUIRE(offer.has_value());
	CHECK(offer->Base == std::optional<std::string>{"refs/remotes/origin/main"});
	CHECK_FALSE(Graphite::OfferReview(branches[alone], branches, &lines).has_value());
}

TEST_CASE("Git window: a branch compared with nothing has no review base")
{
	Graphite::Fixtures::LineReadBuilder builder;
	const std::size_t main = builder.AddLocal("main");
	const std::size_t orphan = builder.AddLocal("orphan");
	builder.MakeBase(main);
	const Graphite::LineRead read = builder.Build();
	const Graphite::BranchLines lines(read);
	const std::vector<Core::Branch>& branches = *read.Branches;

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[orphan], branches, &lines);

	REQUIRE(offer.has_value());
	CHECK_FALSE(offer->Base.has_value());
}

TEST_CASE("Git window: without a line read no review is offered")
{
	Graphite::Fixtures::LineReadBuilder builder;
	builder.AddLocal("side");
	const Graphite::LineRead read = builder.Build();

	CHECK_FALSE(Graphite::OfferReview((*read.Branches)[0], *read.Branches, nullptr).has_value());
}

TEST_CASE("The viewed count covers every file of every present group")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	const Core::Diff uncommitted = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};
	Graphite::ReviewSessions sessions;
	sessions.Start("refs/heads/side", "refs/heads/main");
	sessions.Mark("refs/heads/side", ReviewGroup::Commits, committed.Files[0]);

	const Graphite::ViewedCount count = Graphite::CountViewed(
		groups, [&sessions](Graphite::ReviewGroup group, const Core::FileDiff& file) { return sessions.StateOf("refs/heads/side", group, file); });

	CHECK(count.Viewed == 1);
	CHECK(count.Total == 3);
}

TEST_CASE("The range summary names the merge base and counts the commits")
{
	Graphite::ReviewRange range;
	range.Ends.MergeBase.Bytes[0] = kFirstIdByte;
	range.Commits.resize(1);

	CHECK(Graphite::RangeSummary(range) == "merge-base 7700000 \xC2\xB7 1 commit");

	range.Commits.resize(3);
	CHECK(Graphite::RangeSummary(range) == "merge-base 7700000 \xC2\xB7 3 commits");

	range.CommitsComplete = false;
	CHECK(Graphite::RangeSummary(range) == "merge-base 7700000 \xC2\xB7 3+ commits");
}

TEST_CASE("Each range problem is said in words that name the branches")
{
	CHECK(Graphite::RangeProblemMessage(Graphite::RangeProblem::BranchMissing, "refs/heads/side", "refs/heads/main") ==
		  "side no longer exists.");
	CHECK(Graphite::RangeProblemMessage(Graphite::RangeProblem::BaseMissing, "refs/heads/side", "refs/heads/main") ==
		  "main no longer exists.");
	CHECK(Graphite::RangeProblemMessage(Graphite::RangeProblem::NoCommonAncestor, "refs/heads/side",
										"refs/heads/main") == "side has no common ancestor with main.");
	CHECK(Graphite::AlreadyMergedMessage("refs/heads/side", "refs/heads/main") ==
		  "Every commit of side is already in main.");
}

TEST_CASE("A row's label splits the directory from the name and says where a rename came from")
{
	Core::FileDiff renamed = FileAt("src/old.cpp");
	renamed.Change = Core::FileChange::Renamed;
	renamed.NewPath = "src/ui/new.cpp";

	const Graphite::PathLabel label = Graphite::LabelOf(renamed);

	CHECK(label.Origin == "src/old.cpp \xE2\x86\x92 ");
	CHECK(label.Directory == "src/ui/");
	CHECK(label.Name == "new.cpp");

	const Graphite::PathLabel topLevel = Graphite::LabelOf(FileAt("README.md"));
	CHECK(topLevel.Origin.empty());
	CHECK(topLevel.Directory.empty());
	CHECK(topLevel.Name == "README.md");
}

TEST_CASE("A commit's diff is the range from its first parent to it")
{
	Core::CommitSummary commit;
	commit.Id.Bytes[0] = 9;
	Core::Oid firstParent;
	firstParent.Bytes[0] = 8;
	Core::Oid secondParent;
	secondParent.Bytes[0] = 7;
	commit.Parents = {firstParent, secondParent};

	const Graphite::RangeEnds ends = Graphite::EndsOfCommit(commit);

	CHECK(ends.MergeBase == firstParent);
	CHECK(ends.Tip == commit.Id);
}

TEST_CASE("A change summary counts the files and their lines")
{
	Core::Diff diff = DiffOfFiles({"a.cpp", "b.cpp"});
	Core::Hunk hunk;
	hunk.Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Added},
				  Core::DiffLine{.Kind = Core::DiffLineKind::Removed},
				  Core::DiffLine{.Kind = Core::DiffLineKind::Added}};
	diff.Files[0].Hunks.push_back(hunk);

	CHECK(Graphite::ChangeSummary(diff) == "2 files +2 -1");
	CHECK(Graphite::ChangeSummary(DiffOfFiles({"a.cpp"})) == "1 file +0 -0");
}

TEST_CASE("A group heading says how many files are left on screen out of how many")
{
	CHECK(Graphite::GroupHeadingText(ReviewGroup::Commits, 40, 40) == "Files (40)");
	CHECK(Graphite::GroupHeadingText(ReviewGroup::Commits, 12, 40) == "Files (12 of 40)");
	CHECK(Graphite::GroupHeadingText(ReviewGroup::Uncommitted, 0, 3) == "Uncommitted in worktree (0 of 3)");
}

TEST_CASE("A group still loading is headed by its name alone")
{
	CHECK(std::string{Graphite::GroupName(ReviewGroup::Uncommitted)} == "Uncommitted in worktree");
	CHECK(std::string{Graphite::GroupName(ReviewGroup::Commits)} == "Files");
}

TEST_CASE("The review says it updates its changes while it shows ones made for other settings or an older range")
{
	CHECK_FALSE(Graphite::IsUpdatingChanges(Graphite::ReviewRefresh{}));
	CHECK(Graphite::IsUpdatingChanges(Graphite::ReviewRefresh{.ShowsEarlierChanges = true}));
	CHECK(Graphite::IsUpdatingChanges(Graphite::ReviewRefresh{.RangeLoading = true}));
}

TEST_CASE("Uncommitted changes read again update the review only once some are on screen")
{
	const Graphite::ReviewRefresh firstLoad{.UncommittedLoading = true, .UncommittedShown = false};
	const Graphite::ReviewRefresh refresh{.UncommittedLoading = true, .UncommittedShown = true};

	CHECK_FALSE(Graphite::IsUpdatingChanges(firstLoad));
	CHECK(Graphite::IsUpdatingChanges(refresh));
}

TEST_CASE("Uncommitted changes are awaited as a loading group on their first read and on a retry, not on a read in the background")
{
	const Graphite::ReviewRefresh firstRead{.UncommittedLoading = true};
	const Graphite::ReviewRefresh retry{.UncommittedLoading = true, .UncommittedFailed = true, .UncommittedRetrying = true};
	const Graphite::ReviewRefresh backgroundReadAfterFailure{.UncommittedLoading = true, .UncommittedFailed = true};
	const Graphite::ReviewRefresh refreshOfShownChanges{.UncommittedLoading = true, .UncommittedShown = true};

	CHECK(Graphite::IsUncommittedAwaited(firstRead));
	CHECK(Graphite::IsUncommittedAwaited(retry));
	CHECK_FALSE(Graphite::IsUncommittedAwaited(backgroundReadAfterFailure));
	CHECK_FALSE(Graphite::IsUncommittedAwaited(refreshOfShownChanges));
	CHECK_FALSE(Graphite::IsUncommittedAwaited(Graphite::ReviewRefresh{}));
}

TEST_CASE("A failure to read uncommitted changes stays on screen through reads in the background and gives way only to a retry")
{
	const Graphite::ReviewRefresh failed{.UncommittedFailed = true};
	const Graphite::ReviewRefresh readAgain{.UncommittedLoading = true, .UncommittedFailed = true};
	const Graphite::ReviewRefresh retried{.UncommittedLoading = true, .UncommittedFailed = true, .UncommittedRetrying = true};

	CHECK(Graphite::ShowsUncommittedFailure(failed));
	CHECK(Graphite::ShowsUncommittedFailure(readAgain));
	CHECK_FALSE(Graphite::ShowsUncommittedFailure(retried));
	CHECK_FALSE(Graphite::ShowsUncommittedFailure(Graphite::ReviewRefresh{}));
}

TEST_CASE("The diff pane without a file says there is nothing to review only once the uncommitted changes are in")
{
	using Graphite::DiffPaneWithoutFile;

	CHECK(Graphite::DiffPaneWithoutFileFor(0, true) == DiffPaneWithoutFile::LoadingUncommitted);
	CHECK(Graphite::DiffPaneWithoutFileFor(0, false) == DiffPaneWithoutFile::NothingToReview);
	CHECK(Graphite::DiffPaneWithoutFileFor(3, true) == DiffPaneWithoutFile::SelectAFile);
	CHECK(Graphite::DiffPaneWithoutFileFor(3, false) == DiffPaneWithoutFile::SelectAFile);
}

TEST_CASE("The commits tab counts the range's commits and says when the list was cut")
{
	Graphite::ReviewRange range;
	range.Commits.resize(3);

	CHECK(Graphite::CommitsTabLabel(range) == "Commits (3)");

	range.CommitsComplete = false;
	CHECK(Graphite::CommitsTabLabel(range) == "Commits (3+)");
}

TEST_CASE("A commit's line names its hash and author and then what it changed")
{
	Core::CommitSummary commit;
	commit.Id.Bytes[0] = kFirstIdByte;
	commit.Author.Name = "Ada";

	CHECK(Graphite::CommitLine(commit, nullptr) == "7700000 \xC2\xB7 Ada");

	const Core::Diff diff = DiffOfFiles({"a.cpp"});
	CHECK(Graphite::CommitLine(commit, &diff) == "7700000 \xC2\xB7 Ada \xC2\xB7 1 file +0 -0");
}

TEST_CASE("A picked commit is kept while the range lists it and let go once it does not")
{
	std::vector<Core::CommitSummary> commits(2);
	commits[0].Id.Bytes[0] = 1;
	commits[1].Id.Bytes[0] = 2;
	const std::optional<Core::Oid> picked = commits[1].Id;

	CHECK(Graphite::FollowCommit(picked, commits) == picked);

	commits.pop_back();
	CHECK_FALSE(Graphite::FollowCommit(picked, commits).has_value());
}

TEST_CASE("The arrow keys step through the commits and stop at either end")
{
	std::vector<Core::CommitSummary> commits(3);
	commits[0].Id.Bytes[0] = 1;
	commits[1].Id.Bytes[0] = 2;
	commits[2].Id.Bytes[0] = 3;
	const std::optional<Core::Oid> nonePicked;
	const std::vector<Core::CommitSummary> noCommits;

	CHECK(Graphite::NeighbourCommit(nonePicked, commits, ListStep::Next) == commits[0].Id);
	CHECK(Graphite::NeighbourCommit(commits[0].Id, commits, ListStep::Next) == commits[1].Id);
	CHECK(Graphite::NeighbourCommit(commits[2].Id, commits, ListStep::Next) == commits[2].Id);
	CHECK(Graphite::NeighbourCommit(commits[0].Id, commits, ListStep::Previous) == commits[0].Id);
	CHECK_FALSE(Graphite::NeighbourCommit(nonePicked, noCommits, ListStep::Next).has_value());
}

TEST_CASE("Only a modified text file left with no lines once white space is ignored changed only in white space")
{
	const Core::FileDiff file = ChangedFileAt("a.cpp");

	CHECK(Graphite::OnlyWhitespaceChanged(file, Core::WhitespaceInDiff::Ignored));
	CHECK_FALSE(Graphite::OnlyWhitespaceChanged(file, Core::WhitespaceInDiff::Shown));

	Core::FileDiff binary = ChangedFileAt("a.png");
	binary.IsBinary = true;
	CHECK_FALSE(Graphite::OnlyWhitespaceChanged(binary, Core::WhitespaceInDiff::Ignored));

	Core::FileDiff renamed = ChangedFileAt("b.cpp");
	renamed.Change = Core::FileChange::Renamed;
	CHECK_FALSE(Graphite::OnlyWhitespaceChanged(renamed, Core::WhitespaceInDiff::Ignored));
}

TEST_CASE("A modified file with the same blob on both sides changed its mode and not its white space")
{
	Core::FileDiff modeOnly = FileAt("run.sh");
	modeOnly.OldId.Bytes[0] = 7;
	modeOnly.NewId = modeOnly.OldId;

	CHECK_FALSE(Graphite::OnlyWhitespaceChanged(modeOnly, Core::WhitespaceInDiff::Ignored));
}

TEST_CASE("A root commit has no parent so its range starts at a zero id")
{
	Core::CommitSummary root;
	root.Id.Bytes[0] = 9;

	const Graphite::RangeEnds ends = Graphite::EndsOfCommit(root);

	CHECK(ends.MergeBase == Core::Oid{});
	CHECK(ends.Tip == root.Id);
}

TEST_CASE("Each file's line counts come in the diff's own order and add up to the change summary")
{
	Core::Diff diff = DiffOfFiles({"a.cpp", "b.cpp"});
	Core::Hunk addedOnly;
	addedOnly.Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Added}};
	Core::Hunk removedTwice;
	removedTwice.Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Removed},
						  Core::DiffLine{.Kind = Core::DiffLineKind::Removed}};
	diff.Files[0].Hunks.push_back(addedOnly);
	diff.Files[1].Hunks.push_back(removedTwice);

	const std::vector<Core::LineCounts> counts = Graphite::LineCountsOfEach(diff);

	REQUIRE(counts.size() == 2);
	CHECK(counts[0].Added == 1);
	CHECK(counts[0].Removed == 0);
	CHECK(counts[1].Added == 0);
	CHECK(counts[1].Removed == 2);
	CHECK(Graphite::SumOf(counts).Added == 1);
	CHECK(Graphite::SumOf(counts).Removed == 2);
}

TEST_CASE("A selected file is found in its own group and is null when the group is not loaded or lacks the file")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed};

	const Graphite::ReviewSelection present{.Group = ReviewGroup::Commits, .Path = "a.cpp"};
	const Graphite::ReviewSelection gone{.Group = ReviewGroup::Commits, .Path = "b.cpp"};
	const Graphite::ReviewSelection notLoaded{.Group = ReviewGroup::Uncommitted, .Path = "a.cpp"};

	CHECK(Graphite::SelectedFile(groups, present) == &committed.Files[0]);
	CHECK(Graphite::SelectedFile(groups, gone) == nullptr);
	CHECK(Graphite::SelectedFile(groups, notLoaded) == nullptr);
}
