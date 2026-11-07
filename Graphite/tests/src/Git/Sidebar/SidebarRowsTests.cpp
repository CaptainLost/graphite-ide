#include <doctest/doctest.h>

#include "Git/Sidebar/SidebarRows.h"
#include "Git/Sidebar/SidebarText.h"

#include "Git/Lines/LineReadFixtures.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Graphite;
using Graphite::Fixtures::BranchSpec;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	const std::vector<Core::StashEntry> kNoStashes;

	struct Options
	{
		std::string Search;
		std::map<std::string, bool> OpenByUser;
		std::set<std::string> ShowingMerged;
		const std::vector<Core::StashEntry>* Stashes = &kNoStashes;
		const BranchAuthors* Authors = nullptr;
		std::string ShownRef;
	};

	std::vector<SidebarRow> RowsOf(const LineRead& read, const Options& options = {})
	{
		const SidebarInput input{.Read = read,
								 .Stashes = options.Stashes,
								 .Authors = options.Authors,
								 .Search = options.Search,
								 .OpenByUser = options.OpenByUser,
								 .ShowingMerged = options.ShowingMerged,
								 .ShownRef = options.ShownRef};
		return BuildSidebarRows(input);
	}

	// One short line per row, indented by depth: "# yours 3" a section ("# stashes" with no count while unread), "> label
	// 2" a group (closed groups end with " closed"), "main" a branch or base, "~ text" a link, "stash 0" a stash, "? text"
	// a notice that waits and "! text" one that does not.
	std::vector<std::string> Describe(const LineRead& read, const std::vector<SidebarRow>& rows)
	{
		std::vector<std::string> lines;

		for (const SidebarRow& row : rows)
		{
			std::string text(row.Depth * 2, ' ');

			switch (row.Kind)
			{
			case SidebarRowKind::Section:
				text += "# " + row.Key + (row.Counted ? " " + std::to_string(row.Count) : std::string{});

				if (row.Total.has_value())
				{
					text += "/" + std::to_string(*row.Total);
				}

				if (!row.Open)
				{
					text += " closed";
				}
				break;
			case SidebarRowKind::Group:
				text += "> " + row.Label + " " + std::to_string(row.Count) + (row.Open ? "" : " closed");
				break;
			case SidebarRowKind::Branch:
			case SidebarRowKind::Base:
				text += (*read.Branches)[*row.Ref].Name;
				break;
			case SidebarRowKind::Stash:
				text += "stash " + std::to_string(*row.Stash);
				break;
			case SidebarRowKind::Link:
				text += "~ " + row.Label;
				break;
			case SidebarRowKind::Notice:
				text += (row.Waiting ? "? " : "! ") + row.Label;
				break;
			}

			lines.push_back(text);
		}

		return lines;
	}

	using Lines = std::vector<std::string>;

	const char* const kInYours = "~ 1 more you have locally \xC2\xB7 in Yours";
	const char* const kOneMergedHidden = "~ 1 merged hidden \xC2\xB7 Show";
}

TEST_CASE("Git window: yours lists the checked-out branch first then the others newest first")
{
	LineReadBuilder builder;
	builder.AddLocal("older", {.AgeDays = 10});
	builder.AddLocal("checked-out", {.Head = true, .AgeDays = 50});
	builder.AddLocal("unread", {.AgeDays = std::nullopt});
	builder.AddLocal("newer", {.AgeDays = 1});
	const LineRead read = builder.Build();

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 4", "checked-out", "newer", "older", "unread", "# stashes 0", "! No stashes"});
}

TEST_CASE("Git window: a base's local copy is in the base's row and not in yours")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddLocal("main");
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	builder.AddLocal("feature");
	builder.MakeBase(originMain);
	builder.SameRow(main, originMain);
	const LineRead read = builder.Build();

	const std::vector<SidebarRow> rows = RowsOf(read);

	CHECK(Describe(read, rows) == Lines{"# yours 1", "feature", "# bases 1", "origin/main", "~ Why these, and how to change them",
										"# remote/origin 0", "# stashes 0", "! No stashes"});
	CHECK(rows[3].Kind == SidebarRowKind::Base);
	CHECK(rows[3].LocalCopy == std::optional<std::size_t>{main});
}

TEST_CASE("Git window: a checked-out local copy of a base stays in the base's row")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddLocal("main", {.Head = true});
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	builder.AddLocal("feature", {.AgeDays = 1});
	builder.MakeBase(originMain);
	builder.SameRow(main, originMain);
	const LineRead read = builder.Build();

	const std::vector<SidebarRow> rows = RowsOf(read);

	CHECK(Describe(read, rows)[1] == "feature");
	CHECK(rows[3].LocalCopy == std::optional<std::size_t>{main});
}

TEST_CASE("Git window: a local base is one row with the remote branch it tracks")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddLocal("main");
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	builder.AddRemoteBranch("origin", "other");
	builder.MakeBase(main);
	builder.Track(main, originMain);
	const LineRead read = builder.Build();

	const std::vector<SidebarRow> rows = RowsOf(read);

	CHECK(Describe(read, rows) == Lines{"# yours 0", "# bases 1", "main", "~ Why these, and how to change them", "# remote/origin 1",
										"origin/other", "# stashes 0", "! No stashes"});
	CHECK_FALSE(rows[2].LocalCopy.has_value());
}

TEST_CASE("Git window: a remote section lists only what is not on this computer and says how many are in yours")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");
	const std::size_t mine = builder.AddLocal("mine", {.AgeDays = 3});
	const std::size_t originMine = builder.AddRemoteBranch("origin", "mine");
	builder.SameRow(mine, originMine);
	builder.AddRemoteBranch("origin", "theirs-old", {.AgeDays = 20});
	builder.AddRemoteBranch("origin", "theirs-new", {.AgeDays = 2});
	builder.AddRemoteBranch("fork", "x");
	const LineRead read = builder.Build();

	const std::vector<SidebarRow> rows = RowsOf(read);

	CHECK(Describe(read, rows) == Lines{"# yours 1", "mine", "# remote/origin 2", "origin/theirs-new", "origin/theirs-old", kInYours,
										"# remote/fork 1", "fork/x", "# stashes 0", "! No stashes"});
	CHECK(rows[2].Label == "origin");
	CHECK(rows[2].Note == "not on this computer");
	CHECK(rows[2].Remote == "origin");
	CHECK(rows[5].Link == std::optional<SidebarLink>{SidebarLink::ToYours});
}

TEST_CASE("Git window: merged branches of a remote hide behind a show row until shown")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "live", {.AgeDays = 2});
	builder.AddRemoteBranch("origin", "merged", {.AgeDays = 1, .State = Core::BranchState::MergedByHistory});
	const LineRead read = builder.Build();

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 0", "# remote/origin 1", "origin/live", kOneMergedHidden, "# stashes 0", "! No stashes"});

	Options showing;
	showing.ShowingMerged = {"origin"};
	const std::vector<SidebarRow> rows = RowsOf(read, showing);

	CHECK(Describe(read, rows) ==
		  Lines{"# yours 0", "# remote/origin 2", "origin/merged", "origin/live", "~ Hide merged", "# stashes 0", "! No stashes"});
	CHECK(rows[4].Link == std::optional<SidebarLink>{SidebarLink::HideMerged});
}

TEST_CASE("Git window: more than fifteen remote branches are grouped by activity with the first group open")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	for (int index = 0; index < 3; ++index)
	{
		builder.AddRemoteBranch("origin", "week" + std::to_string(index), {.AgeDays = index + 1});
	}

	for (int index = 0; index < 7; ++index)
	{
		builder.AddRemoteBranch("origin", "season" + std::to_string(index), {.AgeDays = 30 + index});
	}

	for (int index = 0; index < 5; ++index)
	{
		builder.AddRemoteBranch("origin", "old" + std::to_string(index), {.AgeDays = 200 + index});
	}

	builder.AddRemoteBranch("origin", "unread", {.AgeDays = std::nullopt});
	const LineRead read = builder.Build();

	const std::vector<SidebarRow> rows = RowsOf(read);
	const Lines described = Describe(read, rows);

	CHECK(described == Lines{"# yours 0", "# remote/origin 16", "> Active this week 3", "  origin/week0", "  origin/week1", "  origin/week2",
							 "> Last 90 days 7 closed", "> Older than 90 days 6 closed", "# stashes 0", "! No stashes"});
	CHECK(rows[2].Key == "remote/origin/0");
	CHECK(rows[6].Key == "remote/origin/1");
	CHECK(rows[7].Key == "remote/origin/2");
	CHECK(rows[2].Remote == "origin");

	Options opened;
	opened.OpenByUser = {{"remote/origin/2", true}, {"remote/origin/0", false}};
	const Lines reopened = Describe(read, RowsOf(read, opened));

	CHECK(reopened[2] == "> Active this week 3 closed");
	CHECK(reopened[5] == "  origin/old0");
	CHECK(reopened[reopened.size() - 3] == "  origin/unread");
}

TEST_CASE("Git window: the remote section's links count with a thousands comma")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "live", {.AgeDays = 2});

	for (int index = 0; index < 1'200; ++index)
	{
		builder.AddRemoteBranch("origin", "merged" + std::to_string(index), {.State = Core::BranchState::MergedByHistory});
	}

	for (int index = 0; index < 1'050; ++index)
	{
		const std::size_t local = builder.AddLocal("mine" + std::to_string(index));
		const std::size_t remote = builder.AddRemoteBranch("origin", "mine" + std::to_string(index));
		builder.SameRow(local, remote);
	}

	const LineRead read = builder.Build();
	const Lines described = Describe(read, RowsOf(read));

	CHECK(std::ranges::find(described, "~ 1,050 more you have locally \xC2\xB7 in Yours") != described.end());
	CHECK(std::ranges::find(described, "~ 1,200 merged hidden \xC2\xB7 Show") != described.end());
}

TEST_CASE("Git window: the activity groups are told against the read's time and not the clock")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	for (int index = 0; index < 16; ++index)
	{
		builder.AddRemoteBranch("origin", "b" + std::to_string(index), {.AgeDays = 6});
	}

	LineRead read = builder.Build();
	CHECK(Describe(read, RowsOf(read))[2] == "> Active this week 16");

	// The same branches read two days later have left the week.
	constexpr std::int64_t kTwoDays = 2 * 24 * 60 * 60;
	read.Now += kTwoDays;
	CHECK(Describe(read, RowsOf(read))[2] == "> Last 90 days 16");
}

TEST_CASE("Git window: a search matches the authors in hand when it began until it ends")
{
	const Core::Ref<const BranchAuthors> before = Core::CreateRef<const BranchAuthors>();
	const Core::Ref<const BranchAuthors> landed = Core::CreateRef<const BranchAuthors>();
	const Core::Ref<const BranchAuthors> later = Core::CreateRef<const BranchAuthors>();

	SearchedAuthors authors;

	CHECK(authors.Follow(before, "") == before);
	CHECK(authors.Follow(before, "zed") == before);
	CHECK(authors.Follow(landed, "zed") == before);
	CHECK(authors.Follow(landed, "zed fix") == before);

	// Words that match nothing are no search: the newest authors count again.
	CHECK(authors.Follow(landed, "  ") == landed);
	CHECK(authors.Follow(later, "") == later);
}

TEST_CASE("Git window: fifteen remote branches stay flat")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	for (int index = 0; index < 15; ++index)
	{
		builder.AddRemoteBranch("origin", "b" + std::to_string(index), {.AgeDays = index + 1});
	}

	const LineRead read = builder.Build();
	const std::vector<SidebarRow> rows = RowsOf(read);

	CHECK(rows.size() == 1 + 1 + 15 + 2);
	CHECK(rows[2].Kind == SidebarRowKind::Branch);
	CHECK(rows[2].Depth == 0);
}

TEST_CASE("Git window: quiet bases fold under their own group after the active ones")
{
	LineReadBuilder builder;
	const std::size_t first = builder.AddLocal("first");
	const std::size_t quiet = builder.AddLocal("quiet");
	const std::size_t last = builder.AddLocal("last");
	builder.MakeBase(first);
	builder.MakeBase(quiet, false);
	builder.MakeBase(last);
	const LineRead read = builder.Build();

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 0", "# bases 3", "first", "last", "> Quiet for 90 days 1 closed",
												"~ Why these, and how to change them", "# stashes 0", "! No stashes"});

	Options opened;
	opened.OpenByUser = {{"bases/quiet", true}};
	const std::vector<SidebarRow> rows = RowsOf(read, opened);

	CHECK(Describe(read, rows) == Lines{"# yours 0", "# bases 3", "first", "last", "> Quiet for 90 days 1", "  quiet",
										"~ Why these, and how to change them", "# stashes 0", "! No stashes"});
	CHECK(rows[2].Link == std::nullopt);
	CHECK(rows[4].Key == "bases/quiet");
}

TEST_CASE("Git window: search matches every word against names and authors and counts n of total")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddLocal("login-fix", {.AgeDays = 1, .AuthorName = "Bob Builder", .AuthorEmail = "bob@example.com"});
	builder.AddLocal("login-page", {.AgeDays = 2});
	builder.AddLocal("other", {.AgeDays = 3, .AuthorName = "Bob Builder", .AuthorEmail = "bob@example.com"});
	builder.AddRemoteBranch("origin", "login-remote", {.AuthorName = "Bob Builder", .AuthorEmail = "bob@example.com"});
	builder.AddRemoteBranch("origin", "nothing");
	const LineRead read = builder.Build();

	Options search;
	search.Search = "  LOGIN bob ";

	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# yours 1/3", "login-fix", "# remote/origin 1/2", "origin/login-remote"});

	search.Search = "ann@example";
	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# yours 1/3", "login-page", "# remote/origin 1/2", "origin/nothing"});

	search.Search = "zzz";
	CHECK(RowsOf(read, search).empty());
}

TEST_CASE("Git window: search matches stashes on their message")
{
	LineReadBuilder builder;
	builder.AddLocal("a");
	const LineRead read = builder.Build();

	const std::vector<Core::StashEntry> stashes = {{.Index = 0, .Message = "On main: Parser WIP"}, {.Index = 1, .Message = "On a: docs"}};
	Options options;
	options.Stashes = &stashes;

	CHECK(Describe(read, RowsOf(read, options)) == Lines{"# yours 1", "a", "# stashes 2", "stash 0", "stash 1"});

	options.Search = "parser wip";
	CHECK(Describe(read, RowsOf(read, options)) == Lines{"# stashes 1/2", "stash 0"});
}

TEST_CASE("Git window: while the lines are read only the stashes are listed")
{
	const LineRead noLines = LineReadBuilder{}.Build();
	const std::vector<Core::StashEntry> stashes = {{.Index = 0, .Message = "On main: Parser WIP"}, {.Index = 1, .Message = "On a: docs"}};
	const std::map<std::string, bool> noneClosed;

	CHECK(Describe(noLines, BuildStashRows(&stashes, nullptr, {}, noneClosed)) == Lines{"# stashes 2", "stash 0", "stash 1"});
	CHECK(Describe(noLines, BuildStashRows(&stashes, nullptr, "docs", noneClosed)) == Lines{"# stashes 1/2", "stash 1"});

	const std::map<std::string, bool> closed = {{"stashes", false}};
	CHECK(Describe(noLines, BuildStashRows(&stashes, nullptr, {}, closed)) == Lines{"# stashes 2 closed"});
}

TEST_CASE("Git window: the stashes section says when its list is read, failed or empty")
{
	const LineRead noLines = LineReadBuilder{}.Build();
	const std::map<std::string, bool> noneClosed;
	const std::vector<Core::StashEntry> stashes = {{.Index = 0, .Message = "On main: Parser WIP"}};
	const std::vector<Core::StashEntry> none;
	const Core::Error failure{Core::ErrorCode::IoFailure, "refs/stash is locked"};

	CHECK(Describe(noLines, BuildStashRows(nullptr, nullptr, {}, noneClosed)) == Lines{"# stashes", "? Loading stashes"});
	CHECK(Describe(noLines, BuildStashRows(nullptr, &failure, {}, noneClosed)) == Lines{"# stashes", "! refs/stash is locked"});
	CHECK(Describe(noLines, BuildStashRows(&stashes, &failure, {}, noneClosed)) ==
		  Lines{"# stashes 1", "! refs/stash is locked", "stash 0"});
	CHECK(Describe(noLines, BuildStashRows(&none, nullptr, {}, noneClosed)) == Lines{"# stashes 0", "! No stashes"});

	// A closed section says nothing, and a search with no stash to match leaves the section out.
	const std::map<std::string, bool> closed = {{"stashes", false}};
	CHECK(Describe(noLines, BuildStashRows(nullptr, nullptr, {}, closed)) == Lines{"# stashes closed"});
	CHECK(BuildStashRows(nullptr, nullptr, "parser", noneClosed).empty());
}

TEST_CASE("Git window: search finds merged branches that are hidden")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "feature-live");
	builder.AddRemoteBranch("origin", "feature-merged", {.AgeDays = 1, .State = Core::BranchState::MergedByHistory});
	const LineRead read = builder.Build();

	Options search;
	search.Search = "merged";

	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# remote/origin 1/2", "origin/feature-merged"});
}

TEST_CASE("Git window: a repository without remotes has no remote sections")
{
	LineReadBuilder builder;
	builder.AddLocal("main");
	const LineRead read = builder.Build();

	for (const SidebarRow& row : RowsOf(read))
	{
		CHECK(row.Section != SidebarSection::Remote);
	}
}

TEST_CASE("Git window: an unborn repository still lists the yours and stashes sections")
{
	const LineRead read = LineReadBuilder{}.Build();

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 0", "# stashes 0", "! No stashes"});
}

TEST_CASE("Git window: a section the user closed lists no rows")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddLocal("a");
	builder.AddRemoteBranch("origin", "b");
	const LineRead read = builder.Build();

	Options closed;
	closed.OpenByUser = {{"yours", false}, {"remote/origin", false}};

	CHECK(Describe(read, RowsOf(read, closed)) == Lines{"# yours 1 closed", "# remote/origin 1 closed", "# stashes 0", "! No stashes"});
}

TEST_CASE("Git window: six thousand branches build into rows")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(originMain);

	for (int index = 0; index < 1000; ++index)
	{
		builder.AddLocal("local" + std::to_string(index), {.AgeDays = index % 400});
	}

	constexpr int kRemoteBranches = 5000;

	for (int index = 0; index < kRemoteBranches; ++index)
	{
		const int age = index % 3 == 0 ? 1 : (index % 3 == 1 ? 30 : 200);
		builder.AddRemoteBranch("origin", "remote" + std::to_string(index), {.AgeDays = age});
	}

	const LineRead read = builder.Build();

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const std::vector<SidebarRow> rows = RowsOf(read);
	const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - start;

	std::printf("six thousand branches: %zu rows in %.2f ms\n", rows.size(), took.count());

	// yours + 1000, bases + base + link, remote + 3 groups + 1667 open ones, stashes + "No stashes".
	CHECK(rows.size() == 1 + 1000 + 3 + 1 + 3 + 1667 + 2);
}

TEST_CASE("Git window: search finds a branch through the authors read for its tip")
{
	LineReadBuilder builder;
	const std::size_t alpha = builder.AddLocal("alpha", {.AgeDays = 1});
	builder.AddLocal("beta", {.AgeDays = 2});
	const LineRead read = builder.Build();

	BranchAuthors authors;
	Core::TipAuthors answer;
	answer.Tip = (*read.Branches)[alpha].Tip;
	answer.Authors = {Core::Signature{.Name = "Zed Zealot", .Email = "zed@example.com", .When = {}}};
	answer.OwnCommits = 1;
	authors.ByTip[answer.Tip] = answer;

	Options search;
	search.Search = "zealot";

	CHECK(Describe(read, RowsOf(read, search)).empty());

	search.Authors = &authors;
	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# yours 1/2", "alpha"});

	search.Search = "zed@example";
	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# yours 1/2", "alpha"});
}

TEST_CASE("Git window: search with authors read but none for a tip falls back to the tip's author")
{
	LineReadBuilder builder;
	builder.AddLocal("alpha", {.AgeDays = 1, .AuthorName = "Ann Author", .AuthorEmail = "ann@example.com"});
	builder.AddLocal("beta", {.AgeDays = 2, .AuthorName = "Bob Builder", .AuthorEmail = "bob@example.com"});
	const LineRead read = builder.Build();

	const BranchAuthors authors;
	Options search;
	search.Authors = &authors;

	search.Search = "builder";
	CHECK(Describe(read, RowsOf(read, search)) == Lines{"# yours 1/2", "beta"});

	search.Search = "zealot";
	CHECK(RowsOf(read, search).empty());
}

TEST_CASE("Git window: a failed stashes read leaves the count's place empty and the section says what failed")
{
	const Core::Error error{.Message = "cannot read stashes"};
	const std::vector<SidebarRow> rows = BuildStashRows(nullptr, &error, "", {});

	REQUIRE(rows.size() == 2);
	CHECK(rows[0].Kind == SidebarRowKind::Section);
	CHECK(rows[0].CountFailed);
	CHECK(SectionCountText(rows[0]).empty());
	CHECK(rows[1].Label == "cannot read stashes");
}

namespace {

	// origin/main is a base and main its local copy, one row with it; the copy is built from `copy`.
	struct BaseWithCopy
	{
		LineRead Read;
		std::size_t Main = 0;
		std::size_t OriginMain = 0;
	};

	BaseWithCopy MakeBaseWithCopy(const BranchSpec& copy)
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		BaseWithCopy scene;
		scene.Main = builder.AddLocal("main", copy);
		scene.OriginMain = builder.AddRemoteBranch("origin", "main");
		builder.MakeBase(scene.OriginMain);
		builder.SameRow(scene.Main, scene.OriginMain);
		builder.Compare(scene.Main, scene.OriginMain);
		scene.Read = builder.Build();

		return scene;
	}

	SidebarRow BaseRowOf(const BaseWithCopy& scene)
	{
		for (const SidebarRow& row : RowsOf(scene.Read))
		{
			if (row.Kind == SidebarRowKind::Base)
			{
				return row;
			}
		}

		return {};
	}
}

TEST_CASE("Git window: a base row opens its local copy while the copy is checked out")
{
	const BaseWithCopy scene = MakeBaseWithCopy({.Head = true, .State = Core::BranchState::NoOwnCommits});
	const BranchLines lines(scene.Read);

	CHECK(RefOpenedBy(BaseRowOf(scene), scene.Read, lines) == std::optional<std::size_t>{scene.Main});
}

TEST_CASE("Git window: a base row opens its local copy when the copy has commits of its own")
{
	const BaseWithCopy scene =
		MakeBaseWithCopy({.State = Core::BranchState::NotMerged, .Ahead = 2, .DistanceKnown = true});
	const BranchLines lines(scene.Read);

	CHECK(RefOpenedBy(BaseRowOf(scene), scene.Read, lines) == std::optional<std::size_t>{scene.Main});
}

TEST_CASE("Git window: a base row opens the base when its local copy is behind with no commits of its own")
{
	const BaseWithCopy scene =
		MakeBaseWithCopy({.State = Core::BranchState::NoOwnCommits, .Behind = 3, .DistanceKnown = true});
	const BranchLines lines(scene.Read);

	CHECK(RefOpenedBy(BaseRowOf(scene), scene.Read, lines) == std::optional<std::size_t>{scene.OriginMain});
}

TEST_CASE("Git window: a base row without a local copy opens the base")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(originMain);
	const LineRead read = builder.Build();
	const BranchLines lines(read);

	SidebarRow baseRow;

	for (const SidebarRow& row : RowsOf(read))
	{
		if (row.Kind == SidebarRowKind::Base)
		{
			baseRow = row;
		}
	}

	CHECK(RefOpenedBy(baseRow, read, lines) == std::optional<std::size_t>{originMain});
	CHECK(RefOfRowMenu(baseRow) == std::optional<std::size_t>{originMain});
}

TEST_CASE("Git window: a base row's menu is its local copy's whatever the copy holds")
{
	const BaseWithCopy behind = MakeBaseWithCopy({.State = Core::BranchState::NoOwnCommits});
	const BaseWithCopy ahead = MakeBaseWithCopy({.State = Core::BranchState::NotMerged, .Ahead = 2});

	CHECK(RefOfRowMenu(BaseRowOf(behind)) == std::optional<std::size_t>{behind.Main});
	CHECK(RefOfRowMenu(BaseRowOf(ahead)) == std::optional<std::size_t>{ahead.Main});
}

TEST_CASE("Git window: a base row's Compare with acts on the ref its click opens")
{
	const BaseWithCopy checkedOut = MakeBaseWithCopy({.Head = true, .State = Core::BranchState::NoOwnCommits});
	const BaseWithCopy ahead =
		MakeBaseWithCopy({.State = Core::BranchState::NotMerged, .Ahead = 2, .DistanceKnown = true});
	const BaseWithCopy behind =
		MakeBaseWithCopy({.State = Core::BranchState::NoOwnCommits, .Behind = 3, .DistanceKnown = true});
	const BranchLines checkedOutLines(checkedOut.Read);
	const BranchLines aheadLines(ahead.Read);
	const BranchLines behindLines(behind.Read);

	const RowMenuRefs onCheckedOut = RowMenuRefsOf(BaseRowOf(checkedOut), checkedOut.Read, checkedOutLines);
	CHECK(onCheckedOut.Compared == std::optional<std::size_t>{checkedOut.Main});
	CHECK(onCheckedOut.Menu == std::optional<std::size_t>{checkedOut.Main});
	CHECK(onCheckedOut.Lines == std::optional<std::size_t>{checkedOut.OriginMain});

	const RowMenuRefs onAhead = RowMenuRefsOf(BaseRowOf(ahead), ahead.Read, aheadLines);
	CHECK(onAhead.Compared == std::optional<std::size_t>{ahead.Main});
	CHECK(onAhead.Lines == std::optional<std::size_t>{ahead.OriginMain});

	// The copy behind with nothing of its own opens the base, and so is the base compared.
	const RowMenuRefs onBehind = RowMenuRefsOf(BaseRowOf(behind), behind.Read, behindLines);
	CHECK(onBehind.Compared == std::optional<std::size_t>{behind.OriginMain});
	CHECK(onBehind.Menu == std::optional<std::size_t>{behind.Main});
	CHECK(onBehind.Lines == std::optional<std::size_t>{behind.OriginMain});
}

TEST_CASE("Git window: a branch row opens and gives the menu of its own ref and a section row neither")
{
	LineReadBuilder builder;
	const std::size_t feature = builder.AddLocal("feature");
	const LineRead read = builder.Build();
	const BranchLines lines(read);
	const std::vector<SidebarRow> rows = RowsOf(read);

	REQUIRE(rows.size() >= 2);
	CHECK(RefOpenedBy(rows[1], read, lines) == std::optional<std::size_t>{feature});
	CHECK(RefOfRowMenu(rows[1]) == std::optional<std::size_t>{feature});
	CHECK(RowMenuRefsOf(rows[1], read, lines).Lines == std::optional<std::size_t>{feature});
	CHECK(RowMenuRefsOf(rows[1], read, lines).Compared == std::optional<std::size_t>{feature});
	CHECK_FALSE(RefOpenedBy(rows[0], read, lines).has_value());
	CHECK_FALSE(RefOfRowMenu(rows[0]).has_value());
	CHECK_FALSE(RowMenuRefsOf(rows[0], read, lines).Lines.has_value());
}

namespace {

	// Seventeen remote branches, grouped by activity: three this week, seven this season, six old, and old-merged, an old
	// one merged by history.
	LineRead ReadOfGroupedRemote()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		for (int index = 0; index < 3; ++index)
		{
			builder.AddRemoteBranch("origin", "week" + std::to_string(index), {.AgeDays = index + 1});
		}

		for (int index = 0; index < 7; ++index)
		{
			builder.AddRemoteBranch("origin", "season" + std::to_string(index), {.AgeDays = 30 + index});
		}

		for (int index = 0; index < 6; ++index)
		{
			builder.AddRemoteBranch("origin", "old" + std::to_string(index), {.AgeDays = 200 + index});
		}

		builder.AddRemoteBranch("origin", "old-merged", {.AgeDays = 300, .State = Core::BranchState::MergedByHistory});
		return builder.Build();
	}
}

TEST_CASE("Git window: the shown branch stays listed after the search is cleared")
{
	const LineRead read = ReadOfGroupedRemote();

	Options searching;
	searching.Search = "old3";
	searching.ShownRef = "refs/remotes/origin/old3";
	CHECK(Describe(read, RowsOf(read, searching)) == Lines{"# remote/origin 1/17", "origin/old3"});

	Options cleared;
	cleared.ShownRef = "refs/remotes/origin/old3";
	const Lines described = Describe(read, RowsOf(read, cleared));

	CHECK(described == Lines{"# yours 0", "# remote/origin 16", "> Active this week 3", "  origin/week0", "  origin/week1",
							 "  origin/week2", "> Last 90 days 7 closed", "> Older than 90 days 6", "  origin/old0", "  origin/old1",
							 "  origin/old2", "  origin/old3", "  origin/old4", "  origin/old5", kOneMergedHidden, "# stashes 0",
							 "! No stashes"});

	// A group the user closed stays closed.
	cleared.OpenByUser = {{"remote/origin/2", false}};
	CHECK(Describe(read, RowsOf(read, cleared))[7] == "> Older than 90 days 6 closed");
}

TEST_CASE("Git window: the shown branch stays listed when it becomes merged")
{
	const LineRead read = ReadOfGroupedRemote();

	Options shown;
	shown.ShownRef = "refs/remotes/origin/old-merged";
	const Lines described = Describe(read, RowsOf(read, shown));

	CHECK(described == Lines{"# yours 0", "# remote/origin 17", "> Active this week 3", "  origin/week0", "  origin/week1",
							 "  origin/week2", "> Last 90 days 7 closed", "> Older than 90 days 7", "  origin/old0", "  origin/old1",
							 "  origin/old2", "  origin/old3", "  origin/old4", "  origin/old5", "  origin/old-merged", "# stashes 0",
							 "! No stashes"});
}

TEST_CASE("Git window: the quiet bases open when the shown branch is one of them or its local copy")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t active = builder.AddRemoteBranch("origin", "main");
	const std::size_t quiet = builder.AddRemoteBranch("origin", "release");
	const std::size_t copy = builder.AddLocal("release", {.State = Core::BranchState::NotMerged});
	builder.MakeBase(active);
	builder.MakeBase(quiet, false);
	builder.SameRow(copy, quiet);
	const LineRead read = builder.Build();

	for (const std::string shownRef : {"refs/remotes/origin/release", "refs/heads/release"})
	{
		Options shown;
		shown.ShownRef = shownRef;

		CHECK(Describe(read, RowsOf(read, shown)) == Lines{"# yours 0", "# bases 2", "origin/main", "> Quiet for 90 days 1", "  origin/release",
															"~ Why these, and how to change them", "# remote/origin 0", "# stashes 0",
															"! No stashes"});
	}
}

TEST_CASE("Git window: a row found merged by content keeps its place until the rows are rebuilt")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "live", {.AgeDays = 2});
	const std::size_t squashed = builder.AddRemoteBranch("origin", "squashed", {.AgeDays = 1});
	const LineRead read = builder.Build();

	bool found = false;
	int asked = 0;
	const std::function<bool(std::size_t)> mergedByContent = [&](std::size_t branch) {
		++asked;
		return found && branch == squashed;
	};
	const auto build = [&]() {
		const SidebarInput input{.Read = read,
								 .Stashes = &kNoStashes,
								 .Authors = nullptr,
								 .Search = {},
								 .OpenByUser = {},
								 .ShowingMerged = {},
								 .MergedByContent = &mergedByContent};
		return BuildSidebarRows(input);
	};

	const std::vector<SidebarRow> before = build();
	const int askedAtBuild = asked;
	CHECK(Describe(read, before) == Lines{"# yours 0", "# remote/origin 2", "origin/squashed", "origin/live", "# stashes 0", "! No stashes"});

	found = true;

	// The rows stand: nothing between two builds asks the callback, so nothing can move.
	CHECK(asked == askedAtBuild);
	CHECK(Describe(read, before) == Lines{"# yours 0", "# remote/origin 2", "origin/squashed", "origin/live", "# stashes 0", "! No stashes"});

	// The next build hides it, and the closing row counts it.
	const std::vector<SidebarRow> after = build();
	CHECK(Describe(read, after) == Lines{"# yours 0", "# remote/origin 1", "origin/live", kOneMergedHidden, "# stashes 0", "! No stashes"});

	// Shown, it is listed again.
	const SidebarInput showing{.Read = read,
							   .Stashes = &kNoStashes,
							   .Authors = nullptr,
							   .Search = {},
							   .OpenByUser = {},
							   .ShowingMerged = {"origin"},
							   .MergedByContent = &mergedByContent};
	CHECK(Describe(read, BuildSidebarRows(showing)) ==
		  Lines{"# yours 0", "# remote/origin 2", "origin/squashed", "origin/live", "~ Hide merged", "# stashes 0", "! No stashes"});
}

namespace {

	// origin/live, and origin/squashed, which a check finds merged by content once `Found` is set.
	struct FoundLater
	{
		Core::Ref<const LineRead> Read;
		std::size_t Squashed = 0;
		bool Found = false;

		bool IsMergedNow(std::size_t branch) const { return Found && branch == Squashed; }
	};

	FoundLater SquashedFoundLater()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");
		builder.AddRemoteBranch("origin", "live", {.AgeDays = 2});
		const std::size_t squashed = builder.AddRemoteBranch("origin", "squashed", {.AgeDays = 1});

		return FoundLater{.Read = Core::CreateRef<const LineRead>(builder.Build()), .Squashed = squashed};
	}

	// The rows as the window builds them from `inputs`: the branches hidden by content are the snapshot's, which follows
	// the inputs first and asks the live verdicts only at the moments it is taken.
	std::vector<SidebarRow> RowsFromSnapshot(const SidebarInputs& inputs, MergedByContentSnapshot& snapshot, const FoundLater& found,
											 const std::set<std::string>& showingMerged = {})
	{
		const std::function<bool(std::size_t)> mergedNow = [&found](std::size_t branch) { return found.IsMergedNow(branch); };
		snapshot.Follow(inputs, mergedNow);

		const std::function<bool(std::size_t)> hiddenByContent = [&snapshot](std::size_t branch) { return snapshot.IsMerged(branch); };
		const SidebarInput input{.Read = *inputs.Read,
								 .Stashes = &kNoStashes,
								 .Authors = inputs.Authors.get(),
								 .Search = inputs.Search,
								 .OpenByUser = {},
								 .ShowingMerged = showingMerged,
								 .ShownRef = inputs.ShownRef,
								 .MergedByContent = &hiddenByContent};
		return BuildSidebarRows(input);
	}

	const Lines kSquashedListed{"# yours 0", "# remote/origin 2", "origin/squashed", "origin/live", "# stashes 0", "! No stashes"};
	const Lines kSquashedHidden{"# yours 0", "# remote/origin 1", "origin/live", kOneMergedHidden, "# stashes 0", "! No stashes"};
}

TEST_CASE("Git window: a verdict found since the last snapshot hides no row when the rows are built for another reason")
{
	FoundLater found = SquashedFoundLater();
	MergedByContentSnapshot snapshot;
	SidebarInputs inputs{.Read = found.Read};

	REQUIRE(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found)) == kSquashedListed);

	found.Found = true;

	SUBCASE("another branch selected")
	{
		inputs.ShownRef = "refs/remotes/origin/live";
	}

	SUBCASE("a group opened or closed")
	{
		++inputs.ChoicesVersion;
	}

	SUBCASE("the authors read landing")
	{
		inputs.Authors = Core::CreateRef<const BranchAuthors>();
	}

	CHECK(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found)) == kSquashedListed);
}

TEST_CASE("Git window: a verdict found since the last snapshot hides its row at a new read, a search, Show or Hide merged and an opening")
{
	FoundLater found = SquashedFoundLater();
	MergedByContentSnapshot snapshot;
	SidebarInputs inputs{.Read = found.Read};

	REQUIRE(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found)) == kSquashedListed);

	found.Found = true;

	SUBCASE("a new read of the same branches")
	{
		inputs.Read = Core::CreateRef<const LineRead>(*found.Read);
	}

	SUBCASE("a search typed and cleared")
	{
		inputs.Search = "squashed";
		CHECK(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found)) == Lines{"# remote/origin 1/2", "origin/squashed"});
		inputs.Search.clear();
	}

	SUBCASE("Show merged and then Hide merged")
	{
		++inputs.MergedChoicesVersion;
		CHECK(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found, {"origin"})) ==
			  Lines{"# yours 0", "# remote/origin 2", "origin/squashed", "origin/live", "~ Hide merged", "# stashes 0", "! No stashes"});
		++inputs.MergedChoicesVersion;
	}

	SUBCASE("the window opened again")
	{
		++inputs.Openings;
	}

	CHECK(Describe(*found.Read, RowsFromSnapshot(inputs, snapshot, found)) == kSquashedHidden);
}

TEST_CASE("Git window: a local branch merged by content stays listed in Yours with its mark")
{
	LineReadBuilder builder;
	const std::size_t mine = builder.AddLocal("mine");
	const LineRead read = builder.Build();

	const std::function<bool(std::size_t)> everythingMerged = [](std::size_t) { return true; };
	const SidebarInput input{.Read = read,
							 .Stashes = &kNoStashes,
							 .Authors = nullptr,
							 .Search = {},
							 .OpenByUser = {},
							 .ShowingMerged = {},
							 .MergedByContent = &everythingMerged};

	CHECK(Describe(read, BuildSidebarRows(input)) == Lines{"# yours 1", "mine", "# stashes 0", "! No stashes"});
	CHECK(mine == 0);
}

TEST_CASE("Git window: a callback for content verdicts is asked only while the rows are built")
{
	// What a build reads of the content checks it reads through this callback, which only a build calls; between two builds
	// a landed verdict asks nothing. (That GitView's SidebarInputs hold no content result is not reachable from here.)
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "topic");
	const LineRead read = builder.Build();

	bool landed = false;
	int asked = 0;
	const std::function<bool(std::size_t)> mergedByContent = [&](std::size_t) {
		++asked;
		return landed;
	};
	const SidebarInput input{.Read = read,
							 .Stashes = &kNoStashes,
							 .Authors = nullptr,
							 .Search = {},
							 .OpenByUser = {},
							 .ShowingMerged = {},
							 .MergedByContent = &mergedByContent};

	const std::vector<SidebarRow> rows = BuildSidebarRows(input);
	const int askedByTheBuild = asked;
	CHECK(askedByTheBuild > 0);

	landed = true;

	CHECK(asked == askedByTheBuild);
}

TEST_CASE("Git window: a search lists a branch merged by content as it lists one merged by history")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemoteBranch("origin", "live", {.AgeDays = 2});
	const std::size_t squashed = builder.AddRemoteBranch("origin", "squashed", {.AgeDays = 1});
	const LineRead read = builder.Build();

	const std::function<bool(std::size_t)> mergedByContent = [&](std::size_t branch) { return branch == squashed; };
	const SidebarInput input{.Read = read,
							 .Stashes = &kNoStashes,
							 .Authors = nullptr,
							 .Search = "squashed",
							 .OpenByUser = {},
							 .ShowingMerged = {},
							 .MergedByContent = &mergedByContent};

	CHECK(Describe(read, BuildSidebarRows(input)) == Lines{"# remote/origin 1/2", "origin/squashed"});
}
