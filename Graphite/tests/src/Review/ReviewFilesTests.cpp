#include <doctest/doctest.h>

#include "Review/ReviewFiles.h"
#include "Review/ReviewSessions.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::ListStep;
using Graphite::ReviewGroup;
using Graphite::ReviewGroups;

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

	Core::Branch RemoteDefault(const std::string& name)
	{
		Core::Branch branch = RemoteBranch(name);
		branch.IsRemoteDefault = true;
		return branch;
	}

	Core::Branch Tracking(Core::Branch branch, const std::string& upstream)
	{
		branch.Upstream = upstream;
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

	// A file whose content changed: its old and new blobs differ.
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
	CHECK(offer->Label.empty());
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

TEST_CASE("With a remote default every other branch is offered for review against the local branch tracking it")
{
	const std::vector<Core::Branch> branches = {Tracking(LocalBranch("main", false), "origin/main"),
												LocalBranch("refactor", true), LocalBranch("side", false),
												RemoteBranch("origin/feature"), RemoteDefault("origin/main")};

	for (const Core::Branch& reviewed : {branches[1], branches[2], branches[3]})
	{
		CAPTURE(reviewed.Name);

		const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(reviewed, branches);

		REQUIRE(offer.has_value());
		CHECK(offer->Label == "Review against main");
		CHECK(offer->Branch == reviewed.FullName);
		CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
	}
}

TEST_CASE("The local branch tracking the remote default is offered for review against its upstream")
{
	const std::vector<Core::Branch> branches = {Tracking(LocalBranch("main", false), "origin/main"),
												LocalBranch("refactor", true), RemoteDefault("origin/main")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[0], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review against origin/main");
	CHECK(offer->Base == std::optional<std::string>{"refs/remotes/origin/main"});
}

TEST_CASE("With HEAD detached a remote default still gives a branch its base")
{
	const std::vector<Core::Branch> branches = {Tracking(LocalBranch("main", false), "origin/main"),
												LocalBranch("side", false), RemoteDefault("origin/main")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review against main");
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("With no local branch tracking the remote default the remote branch is the base")
{
	const std::vector<Core::Branch> branches = {LocalBranch("side", true), LocalBranch("feature", false),
												RemoteDefault("origin/main")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Label == "Review against origin/main");
	CHECK(offer->Base == std::optional<std::string>{"refs/remotes/origin/main"});

	CHECK_FALSE(Graphite::OfferReview(branches[2], branches).has_value());
}

TEST_CASE("Of several local branches tracking the remote default the one named like it is the base")
{
	const std::vector<Core::Branch> branches = {Tracking(LocalBranch("alpha", false), "origin/main"),
												Tracking(LocalBranch("main", false), "origin/main"),
												LocalBranch("feature", true), RemoteDefault("origin/main")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[2], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("The default of a remote other than origin is not a base")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", true), LocalBranch("side", false),
												RemoteDefault("upstream/develop")};

	const std::optional<Graphite::ReviewOffer> offer = Graphite::OfferReview(branches[1], branches);

	REQUIRE(offer.has_value());
	CHECK(offer->Base == std::optional<std::string>{"refs/heads/main"});
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

TEST_CASE("The viewed count covers every file of every present group")
{
	const Core::Diff committed = DiffOfFiles({"a.cpp", "b.cpp"});
	const Core::Diff uncommitted = DiffOfFiles({"a.cpp"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};
	Graphite::ReviewSessions sessions;
	sessions.Start("refs/heads/side", "refs/heads/main");
	sessions.Mark("refs/heads/side", ReviewGroup::Commits, committed.Files[0]);

	const Graphite::ViewedCount count = Graphite::CountViewed(
		groups, [&sessions](Graphite::ReviewGroup group, const Core::FileDiff& file)
		{ return sessions.StateOf("refs/heads/side", group, file); });

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
	commit.Id.Bytes[0] = 0x77;
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
