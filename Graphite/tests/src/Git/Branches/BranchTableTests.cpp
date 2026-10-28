#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Branches/BranchTable.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr long long kNowSeconds = 1700000000;
	constexpr long long kDay = 24 * 60 * 60;

	std::uint8_t g_NextTip = 1;

	Core::Branch WithTip(Core::Branch branch, long long daysOld, const std::string& email)
	{
		branch.Tip.Bytes[0] = g_NextTip++;

		Core::CommitSummary tip;
		tip.Id = branch.Tip;
		tip.Summary = "Subject of " + branch.Name;
		tip.Author = Core::Signature{.Name = email, .Email = email, .When = SystemSecondsAt(kNowSeconds - daysOld * kDay)};
		tip.Committer = tip.Author;
		branch.TipCommit = tip;

		return branch;
	}

	Core::Branch Local(const std::string& name, long long daysOld, const std::string& upstream = {},
					   const std::string& email = "ann@example.com")
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;

		if (!upstream.empty())
		{
			branch.Upstream = upstream;
		}

		return WithTip(branch, daysOld, email);
	}

	Core::Branch Remote(const std::string& name, long long daysOld, const std::string& email = "ann@example.com")
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		branch.Kind = Core::RefKind::Remote;

		return WithTip(branch, daysOld, email);
	}

	Graphite::BranchTableInput InputOver(const std::vector<Core::Branch>& branches)
	{
		Graphite::BranchTableInput input;
		input.Branches = &branches;
		input.Now = SystemSecondsAt(kNowSeconds);
		return input;
	}

	std::vector<std::string> NamesOf(const std::vector<Graphite::BranchTableRow>& rows)
	{
		std::vector<std::string> names;

		for (const Graphite::BranchTableRow& row : rows)
		{
			names.push_back(row.Name);
		}

		return names;
	}
}

TEST_CASE("A local branch and its upstream are one row")
{
	const std::vector<Core::Branch> branches = {Local("feat", 1, "origin/feat"), Remote("origin/feat", 2)};

	const std::vector<Graphite::BranchTableRow> rows = Graphite::BuildBranchTable(InputOver(branches));

	REQUIRE(rows.size() == 1);
	CHECK(rows[0].Name == "feat");
	CHECK(rows[0].Local == std::optional<std::size_t>{0});
	CHECK(rows[0].Remote == std::optional<std::size_t>{1});
	CHECK(rows[0].TipTime == SystemSecondsAt(kNowSeconds - kDay));
}

TEST_CASE("A remote branch no local branch tracks is a row of its own and the only kind in Remote only")
{
	const std::vector<Core::Branch> branches = {Local("feat", 1, "origin/feat"), Remote("origin/feat", 2),
												Remote("origin/other", 3), Local("loose", 4)};

	Graphite::BranchTableInput input = InputOver(branches);
	input.Scope = Graphite::BranchScope::RemoteOnly;

	const std::vector<Graphite::BranchTableRow> rows = Graphite::BuildBranchTable(input);

	REQUIRE(rows.size() == 1);
	CHECK(rows[0].Name == "origin/other");
	CHECK_FALSE(rows[0].Local.has_value());
}

TEST_CASE("Local lists every local branch, tracked or not")
{
	const std::vector<Core::Branch> branches = {Local("feat", 1, "origin/feat"), Remote("origin/feat", 2),
												Remote("origin/other", 3), Local("loose", 4)};

	Graphite::BranchTableInput input = InputOver(branches);
	input.Scope = Graphite::BranchScope::Local;
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"feat", "loose"});
}

TEST_CASE("Mine keeps the rows with a commit of the user's")
{
	const std::vector<Core::Branch> branches = {Local("mine", 1, {}, "me@example.com"),
												Local("theirs", 1, {}, "you@example.com")};

	Graphite::BranchAuthors authors;
	authors.UserEmail = "me@example.com";

	Graphite::BranchTableInput input = InputOver(branches);
	input.Authors = &authors;
	input.Scope = Graphite::BranchScope::Mine;

	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"mine"});
}

TEST_CASE("Stale keeps the rows whose newest tip is three months old or more")
{
	const std::vector<Core::Branch> branches = {Local("edge", 89), Local("old", 90), Local("older", 200),
												Local("tracked", 300, "origin/tracked"), Remote("origin/tracked", 1)};

	Graphite::BranchTableInput input = InputOver(branches);
	input.Scope = Graphite::BranchScope::Stale;
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"old", "older"});
}

TEST_CASE("The filter matches a word of the name or of an author")
{
	const std::vector<Core::Branch> branches = {Local("login-fix", 1, {}, "ann@example.com"),
												Local("search", 2, {}, "bob@example.com"),
												Local("other", 3, {}, "carol@example.com")};

	Graphite::BranchTableInput input = InputOver(branches);
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	input.Filter = "login";
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"login-fix"});

	input.Filter = "bob";
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"search"});

	input.Filter = "bob search";
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"search"});

	input.Filter = "bob login";
	CHECK(Graphite::BuildBranchTable(input).empty());
}

TEST_CASE("Rows sort newest first by default and by name when asked, either way")
{
	const std::vector<Core::Branch> branches = {Local("b", 5), Local("c", 1), Local("a", 9)};

	Graphite::BranchTableInput input = InputOver(branches);

	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"c", "b", "a"});

	input.Ascending = true;
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"a", "b", "c"});

	input.SortBy = Graphite::BranchColumn::Name;
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"a", "b", "c"});

	input.Ascending = false;
	CHECK(NamesOf(Graphite::BuildBranchTable(input)) == std::vector<std::string>{"c", "b", "a"});
}

TEST_CASE("A local branch shows its distance to the trunk once the facts answered it")
{
	const std::vector<Core::Branch> branches = {Local("feat", 1), Local("unanswered", 2), Remote("origin/feat", 3)};

	const Core::PatchMatch match{.Ahead = 3, .Behind = 2, .OwnChanges = 3, .InBase = 1, .Complete = true};

	Graphite::WorkFacts facts;
	facts.Branches.push_back(Graphite::BranchFacts{.FullName = "refs/heads/feat", .Match = match});

	Graphite::BranchTableInput input = InputOver(branches);
	input.Facts = &facts;
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	const std::vector<Graphite::BranchTableRow> rows = Graphite::BuildBranchTable(input);

	REQUIRE(rows.size() == 3);
	CHECK(rows[0].Name == "feat");
	CHECK(rows[0].Trunk == std::optional<Core::PatchMatch>{match});
	CHECK_FALSE(rows[1].Trunk.has_value());
	CHECK_FALSE(rows[2].Trunk.has_value());
}

TEST_CASE("Scope counts apply the filter")
{
	const std::vector<Core::Branch> branches = {Local("fix-a", 1, "origin/fix-a"), Remote("origin/fix-a", 2),
												Remote("origin/fix-b", 3), Local("fix-old", 100), Local("feature", 1)};

	Graphite::BranchTableInput input = InputOver(branches);

	const std::array<std::size_t, 5> all = Graphite::CountBranchScopes(input);
	CHECK(all == std::array<std::size_t, 5>{4, 3, 1, 0, 1});

	input.Filter = "fix";
	input.Scope = Graphite::BranchScope::Stale;
	CHECK(Graphite::CountBranchScopes(input) == std::array<std::size_t, 5>{3, 2, 1, 0, 1});
}

TEST_CASE("HEAD's branch is marked")
{
	Core::Branch head = Local("main", 1);
	head.IsHead = true;

	const std::vector<Core::Branch> branches = {head, Local("side", 2)};

	Graphite::BranchTableInput input = InputOver(branches);
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	const std::vector<Graphite::BranchTableRow> rows = Graphite::BuildBranchTable(input);

	REQUIRE(rows.size() == 2);
	CHECK(rows[0].IsHead);
	CHECK_FALSE(rows[1].IsHead);
}

TEST_CASE("Where says local, origin, both, and worktree")
{
	const std::vector<Core::Branch> branches = {Local("both", 1, "origin/both"), Remote("origin/both", 2),
												Local("solo", 3), Remote("upstream/far", 4)};

	Graphite::BranchTableInput input = InputOver(branches);
	input.HeldByWorktree = {"refs/heads/solo"};
	input.SortBy = Graphite::BranchColumn::Name;
	input.Ascending = true;

	const std::vector<Graphite::BranchTableRow> rows = Graphite::BuildBranchTable(input);

	REQUIRE(rows.size() == 3);
	CHECK(Graphite::WhereText(rows[0]) == "local \xC2\xB7 origin");
	CHECK(Graphite::WhereText(rows[1]) == "local \xC2\xB7 worktree");
	CHECK(rows[1].InWorktree);
	CHECK(Graphite::WhereText(rows[2]) == "upstream");
}

TEST_CASE("A branch's tooltip names its message and authors and local time")
{
	Core::Branch branch = Local("feature", 0);
	branch.TipCommit->Summary = "Fix the login page";

	const std::vector<Graphite::BranchAuthor> authors = {
		{.Key = "a@x", .Name = "Ann"}, {.Key = "b@x", .Name = "Bob"}, {.Key = "c@x", .Name = "Cid"},
		{.Key = "d@x", .Name = "Dan"}, {.Key = "e@x", .Name = "Eve"},
	};

	const std::string text = Graphite::BranchTooltipText(branch, "feature", authors, nullptr);

	CHECK(text == "feature\nFix the login page\nAuthors: Ann, Bob, Cid +2\n2023-11-14 22:13");
}
