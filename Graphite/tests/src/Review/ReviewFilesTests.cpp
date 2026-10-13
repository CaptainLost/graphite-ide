#include <doctest/doctest.h>

#include "Review/ReviewFiles.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::ListStep;
using Graphite::ReviewGroup;
using Graphite::ReviewGroups;
using Graphite::ReviewListRow;
using Graphite::ReviewSelection;

namespace {

	Core::Branch LocalBranch(const std::string& name, bool isHead)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Kind = Core::RefKind::Local;
		branch.IsHead = isHead;
		return branch;
	}

	Core::Branch RemoteBranch(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		branch.Kind = Core::RefKind::Remote;
		return branch;
	}

	Core::FileDiff FileAt(const std::filesystem::path& path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
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

	ReviewSelection CommitsSelection(const std::filesystem::path& path)
	{
		return ReviewSelection{.Group = ReviewGroup::Commits, .Path = path};
	}
}

TEST_CASE("Any branch but the checked-out one is offered for review against the checked-out branch")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", true), LocalBranch("side", false)};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review against main");
	CHECK(offer->Branch == "refs/heads/side");
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("The checked-out branch is offered for review against its upstream and not at all without one")
{
	Core::Branch main = LocalBranch("main", true);
	main.Upstream = "origin/main";
	const std::vector<Core::Branch> branches = {main, RemoteBranch("origin/main")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[0], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review against origin/main");
	CHECK(offer->Base == std::optional<std::string>{"refs/remotes/origin/main"});

	const std::vector<Core::Branch> withoutUpstream = {LocalBranch("main", true)};
	CHECK_FALSE(Graphite::OfferReview(withoutUpstream[0], withoutUpstream).has_value());
}

TEST_CASE("With HEAD detached a branch is offered with no base yet")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", false), LocalBranch("side", false)};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review\xE2\x80\xA6");
	CHECK_FALSE(offer->Base.has_value());
}

TEST_CASE("A remote branch is offered for review against the checked-out branch")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", true), RemoteBranch("origin/feature")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Branch == "refs/remotes/origin/feature");
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("Every branch but the reviewed one can be the base")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", true), LocalBranch("side", false),
												RemoteBranch("origin/main")};

	const std::vector<const Core::Branch*> choices = Graphite::BaseChoices(branches, "refs/heads/side");

	REQUIRE(choices.size() == 2);
	CHECK(choices[0]->Name == "main");
	CHECK(choices[1]->Name == "origin/main");
}

TEST_CASE("A full branch name is shortened to what the branch list shows")
{
	CHECK(Graphite::ShortBranchName("refs/heads/feature/x") == "feature/x");
	CHECK(Graphite::ShortBranchName("refs/remotes/origin/main") == "origin/main");
	CHECK(Graphite::ShortBranchName("HEAD") == "HEAD");
}

TEST_CASE("A file's added and removed lines are counted from its hunks")
{
	Core::FileDiff file = FileAt("a.cpp");
	Core::Hunk hunk;
	hunk.Lines = {Core::DiffLine{.Kind = Core::DiffLineKind::Context},
				  Core::DiffLine{.Kind = Core::DiffLineKind::Added},
				  Core::DiffLine{.Kind = Core::DiffLineKind::Added},
				  Core::DiffLine{.Kind = Core::DiffLineKind::Removed}};
	file.Hunks.push_back(hunk);

	const Graphite::LineCounts counts = Graphite::CountLines(file);

	CHECK(counts.Added == 2);
	CHECK(counts.Removed == 1);
}

TEST_CASE("The list shows each present group's heading and then its files by path")
{
	const Core::Diff committed = DiffOfFiles({"src/b.cpp", "src/a.cpp"});
	const Core::Diff uncommitted = DiffOfFiles({"z.cpp"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};

	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);

	REQUIRE(rows.size() == 5);
	CHECK(rows[0].IsHeading);
	CHECK(rows[0].FileCount == 2);
	CHECK(Graphite::FileOfRow(groups, rows[1]).NewPath == std::filesystem::path{"src/a.cpp"});
	CHECK(Graphite::FileOfRow(groups, rows[2]).NewPath == std::filesystem::path{"src/b.cpp"});
	CHECK(rows[3].IsHeading);
	CHECK(rows[3].Group == ReviewGroup::Uncommitted);
	CHECK(Graphite::FileOfRow(groups, rows[4]).NewPath == std::filesystem::path{"z.cpp"});
}

TEST_CASE("A folded group shows only its heading and an absent one shows nothing")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	ReviewGroups groups{.Commits = &committed};
	groups.Collapsed[Graphite::IndexOf(ReviewGroup::Commits)] = true;

	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);

	REQUIRE(rows.size() == 1);
	CHECK(rows[0].IsHeading);
}

TEST_CASE("A selection is kept by path across a refresh")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	const ReviewGroups groups{.Commits = &committed};
	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);

	const std::optional<ReviewSelection> followed =
		Graphite::FollowSelection(CommitsSelection("b.cpp"), {"a.cpp", "b.cpp"}, groups, rows);

	CHECK(followed == std::optional<ReviewSelection>{CommitsSelection("b.cpp")});
}

TEST_CASE("A selected file that is gone gives way to the file that took its place")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "c.cpp"});
	const ReviewGroups groups{.Commits = &committed};
	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);

	const std::optional<ReviewSelection> followed =
		Graphite::FollowSelection(CommitsSelection("b.cpp"), {"a.cpp", "b.cpp", "c.cpp"}, groups, rows);

	CHECK(followed == std::optional<ReviewSelection>{CommitsSelection("c.cpp")});
}

TEST_CASE("With nothing selected the first file listed is")
{
	const Core::Diff committed = DiffOfFiles({"b.cpp", "a.cpp"});
	const ReviewGroups groups{.Commits = &committed};
	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);

	const std::optional<ReviewSelection> followed = Graphite::FollowSelection(std::nullopt, {}, groups, rows);

	CHECK(followed == std::optional<ReviewSelection>{CommitsSelection("a.cpp")});
}

TEST_CASE("A selection in a group still loading is kept")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed};
	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);
	const ReviewSelection waiting{.Group = ReviewGroup::Uncommitted, .Path = "z.cpp"};

	const std::optional<ReviewSelection> followed = Graphite::FollowSelection(waiting, {}, groups, rows);

	CHECK(followed == std::optional<ReviewSelection>{waiting});
}

TEST_CASE("The arrow keys step through files across groups and stop at either end")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	const Core::Diff uncommitted = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};
	const std::vector<ReviewListRow> rows = Graphite::BuildReviewRows(groups);
	const ReviewSelection lastFile{.Group = ReviewGroup::Uncommitted, .Path = "a.cpp"};

	CHECK(Graphite::NeighbourSelection(CommitsSelection("b.cpp"), groups, rows, ListStep::Next) ==
		  std::optional<ReviewSelection>{lastFile});
	CHECK(Graphite::NeighbourSelection(lastFile, groups, rows, ListStep::Next) ==
		  std::optional<ReviewSelection>{lastFile});
	CHECK(Graphite::NeighbourSelection(CommitsSelection("a.cpp"), groups, rows, ListStep::Previous) ==
		  std::optional<ReviewSelection>{CommitsSelection("a.cpp")});
}

TEST_CASE("The viewed count covers every file of every present group")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	const Core::Diff uncommitted = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};
	Graphite::ViewedMarks marks;
	marks.Mark("refs/heads/side", ReviewGroup::Commits, committed.Files[0]);

	const Graphite::ViewedCount count = Graphite::CountViewed(groups, marks, "refs/heads/side");

	CHECK(count.Viewed == 1);
	CHECK(count.Total == 3);
}

TEST_CASE("The range summary names the merge base and counts the commits")
{
	Graphite::ReviewRange range;
	range.Ends.MergeBase.Bytes[0] = 0x77;
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
		  "main no longer exists. Pick a base.");
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
