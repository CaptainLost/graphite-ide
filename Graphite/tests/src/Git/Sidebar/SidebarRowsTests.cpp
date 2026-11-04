#include <doctest/doctest.h>

#include "Git/Sidebar/SidebarRows.h"

#include "Git/Lines/LineReadFixtures.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Graphite;
using Graphite::Fixtures::BranchSpec;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	struct Options
	{
		std::string Search;
		std::map<std::string, bool> OpenByUser;
		std::set<std::string> ShowingMerged;
		const std::vector<Core::StashEntry>* Stashes = nullptr;
	};

	std::vector<SidebarRow> RowsOf(const LineRead& read, const Options& options = {})
	{
		const SidebarInput input{.Read = read,
								 .Stashes = options.Stashes,
								 .Authors = nullptr,
								 .Search = options.Search,
								 .OpenByUser = options.OpenByUser,
								 .ShowingMerged = options.ShowingMerged,
								 .Now = LineReadBuilder::Now()};
		return BuildSidebarRows(input);
	}

	// One short line per row, indented by depth: "# yours 3" a section, "> label 2" a group (closed groups end with
	// " closed"), "main" a branch or base, "~ text" a link, "stash 0" a stash.
	std::vector<std::string> Describe(const LineRead& read, const std::vector<SidebarRow>& rows)
	{
		std::vector<std::string> lines;

		for (const SidebarRow& row : rows)
		{
			std::string text(row.Depth * 2, ' ');

			switch (row.Kind)
			{
			case SidebarRowKind::Section:
				text += "# " + row.Key + " " + std::to_string(row.Count);

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

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 4", "checked-out", "newer", "older", "unread", "# stashes 0"});
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
										"# remote/origin 0", "# stashes 0"});
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
										"origin/other", "# stashes 0"});
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
										"# remote/fork 1", "fork/x", "# stashes 0"});
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

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 0", "# remote/origin 1", "origin/live", kOneMergedHidden, "# stashes 0"});

	Options showing;
	showing.ShowingMerged = {"origin"};
	const std::vector<SidebarRow> rows = RowsOf(read, showing);

	CHECK(Describe(read, rows) ==
		  Lines{"# yours 0", "# remote/origin 2", "origin/merged", "origin/live", "~ Hide merged", "# stashes 0"});
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
							 "> Last 90 days 7 closed", "> Older than 90 days 6 closed", "# stashes 0"});
	CHECK(rows[2].Key == "remote/origin/0");
	CHECK(rows[6].Key == "remote/origin/1");
	CHECK(rows[7].Key == "remote/origin/2");
	CHECK(rows[2].Remote == "origin");

	Options opened;
	opened.OpenByUser = {{"remote/origin/2", true}, {"remote/origin/0", false}};
	const Lines reopened = Describe(read, RowsOf(read, opened));

	CHECK(reopened[2] == "> Active this week 3 closed");
	CHECK(reopened[5] == "  origin/old0");
	CHECK(reopened[reopened.size() - 2] == "  origin/unread");
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

	CHECK(rows.size() == 1 + 1 + 15 + 1);
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
												"~ Why these, and how to change them", "# stashes 0"});

	Options opened;
	opened.OpenByUser = {{"bases/quiet", true}};
	const std::vector<SidebarRow> rows = RowsOf(read, opened);

	CHECK(Describe(read, rows) == Lines{"# yours 0", "# bases 3", "first", "last", "> Quiet for 90 days 1", "  quiet",
										"~ Why these, and how to change them", "# stashes 0"});
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

	CHECK(Describe(read, RowsOf(read)) == Lines{"# yours 0", "# stashes 0"});
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

	CHECK(Describe(read, RowsOf(read, closed)) == Lines{"# yours 1 closed", "# remote/origin 1 closed", "# stashes 0"});
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

	// yours + 1000, bases + base + link, remote + 3 groups + 1667 open ones, stashes.
	CHECK(rows.size() == 1 + 1000 + 3 + 1 + 3 + 1667 + 1);
}
