#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Branches/BranchRows.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using Graphite::BranchRow;
using Graphite::BranchRowKind;
using Graphite::BranchRowsInput;

namespace {

	constexpr long long kBaseSeconds = 1700000000;

	struct BranchSpec
	{
		std::string Name;
		Core::RefKind Kind = Core::RefKind::Local;
		std::optional<long long> TipSeconds;
		std::string AuthorEmail = "tip@example.com";
		bool IsHead = false;
		std::string Summary;
	};

	Core::Branch MakeBranch(const BranchSpec& spec, std::uint8_t tipByte)
	{
		Core::Branch branch;
		branch.Name = spec.Name;
		branch.FullName = (spec.Kind == Core::RefKind::Local ? "refs/heads/" : "refs/remotes/") + spec.Name;
		branch.Kind = spec.Kind;
		branch.IsHead = spec.IsHead;
		branch.Tip = OidWithFirstByte(tipByte);

		if (spec.TipSeconds.has_value())
		{
			Core::CommitSummary tip;
			tip.Id = branch.Tip;
			tip.Author = Core::Signature{.Name = spec.AuthorEmail.substr(0, spec.AuthorEmail.find('@')),
										 .Email = spec.AuthorEmail,
										 .When = SystemSecondsAt(*spec.TipSeconds)};
			tip.Committer = tip.Author;
			tip.Summary = spec.Summary;
			branch.TipCommit = tip;
		}

		return branch;
	}

	std::vector<Core::Branch> MakeBranches(const std::vector<BranchSpec>& specs)
	{
		std::vector<Core::Branch> branches;
		std::uint8_t tipByte = 1;

		for (const BranchSpec& spec : specs)
		{
			branches.push_back(MakeBranch(spec, tipByte));
			++tipByte;
		}

		return branches;
	}

	BranchRowsInput InputFor(const std::vector<Core::Branch>& branches)
	{
		BranchRowsInput input;
		input.Branches = &branches;
		return input;
	}

	std::vector<std::string> BranchNamesOf(const std::vector<BranchRow>& rows, const std::vector<Core::Branch>& branches)
	{
		std::vector<std::string> names;

		for (const BranchRow& row : rows)
		{
			if (row.Kind == BranchRowKind::Branch)
			{
				names.push_back(branches[row.BranchIndex].Name);
			}
		}

		return names;
	}

	std::vector<BranchRowKind> KindsOf(const std::vector<BranchRow>& rows)
	{
		std::vector<BranchRowKind> kinds;

		for (const BranchRow& row : rows)
		{
			kinds.push_back(row.Kind);
		}

		return kinds;
	}

	void AddOwnAuthors(Graphite::BranchAuthors& authors, const Core::Branch& branch, const std::vector<std::string>& emails)
	{
		Core::TipAuthors answer;
		answer.Tip = branch.Tip;
		answer.OwnCommits = emails.size();
		long long when = kBaseSeconds;

		for (const std::string& email : emails)
		{
			answer.Authors.push_back(Core::Signature{.Name = email.substr(0, email.find('@')), .Email = email,
													 .When = SystemSecondsAt(when)});
			--when;
		}

		authors.ByTip.emplace(branch.Tip, answer);
	}
}

TEST_CASE("Newest lists the latest tip first and breaks a tie by name")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "old", .TipSeconds = kBaseSeconds - 100},
		{.Name = "b-new", .TipSeconds = kBaseSeconds},
		{.Name = "a-new", .TipSeconds = kBaseSeconds},
	});

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(InputFor(branches)), branches) ==
		  std::vector<std::string>{"a-new", "b-new", "old"});
}

TEST_CASE("Oldest lists the earliest tip first and puts a tipless branch last")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "ghost"},
		{.Name = "new", .TipSeconds = kBaseSeconds},
		{.Name = "old", .TipSeconds = kBaseSeconds - 100},
	});

	BranchRowsInput input = InputFor(branches);
	input.Options.Sort = Graphite::BranchSort::Oldest;

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) == std::vector<std::string>{"old", "new", "ghost"});
}

TEST_CASE("Newest also puts a tipless branch last")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "ghost"},
		{.Name = "old", .TipSeconds = kBaseSeconds - 100},
	});

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(InputFor(branches)), branches) ==
		  std::vector<std::string>{"old", "ghost"});
}

TEST_CASE("Name lists branches by name")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "zeta", .TipSeconds = kBaseSeconds},
		{.Name = "alpha", .TipSeconds = kBaseSeconds - 100},
	});

	BranchRowsInput input = InputFor(branches);
	input.Options.Sort = Graphite::BranchSort::Name;

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) == std::vector<std::string>{"alpha", "zeta"});
}

TEST_CASE("The checked-out branch heads Local whatever the sort")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "newer", .TipSeconds = kBaseSeconds},
		{.Name = "main", .TipSeconds = kBaseSeconds - 100, .IsHead = true},
	});

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(InputFor(branches)), branches) ==
		  std::vector<std::string>{"main", "newer"});
}

TEST_CASE("Local comes before Remote and each heading counts its branches")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "origin/main", .Kind = Core::RefKind::Remote, .TipSeconds = kBaseSeconds},
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(InputFor(branches));

	CHECK(KindsOf(rows) == std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::Branch,
													   BranchRowKind::Section, BranchRowKind::Branch});
	CHECK(Graphite::SectionHeadingText(rows[0]) == "Local (1)");
	CHECK(Graphite::SectionHeadingText(rows[2]) == "Remote (1)");
}

TEST_CASE("Every word must match and the heading says how many of how many are left")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "PROJ-12-login", .TipSeconds = kBaseSeconds},
		{.Name = "PROJ-13-logout", .TipSeconds = kBaseSeconds},
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Filter.Text = "proj login";

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	CHECK(BranchNamesOf(rows, branches) == std::vector<std::string>{"PROJ-12-login"});
	CHECK(Graphite::SectionHeadingText(rows.front()) == "Local (1 of 3)");
}

TEST_CASE("Also commit message lets a word match the tip's message")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "feature", .TipSeconds = kBaseSeconds, .Summary = "Fix the login page"},
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Filter.Text = "login";
	input.Filter.AlsoMessage = true;

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) == std::vector<std::string>{"feature"});
}

TEST_CASE("Ticked authors pass a branch written by any of them")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "ann-and-bob", .TipSeconds = kBaseSeconds},
		{.Name = "carl", .TipSeconds = kBaseSeconds},
		{.Name = "merged-by-ann", .TipSeconds = kBaseSeconds - 1, .AuthorEmail = "ann@example.com"},
	});

	Graphite::BranchAuthors authors;
	AddOwnAuthors(authors, branches[0], {"ann@example.com", "bob@example.com"});
	AddOwnAuthors(authors, branches[1], {"carl@example.com"});

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Filter.Authors = {"bob@example.com", "ann@example.com"};

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) ==
		  std::vector<std::string>{"ann-and-bob", "merged-by-ann"});
}

TEST_CASE("Grouped by author a branch shared by two authors is under both and counted once")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "shared", .TipSeconds = kBaseSeconds},
	});

	Graphite::BranchAuthors authors;
	AddOwnAuthors(authors, branches[0], {"ann@example.com", "bob@example.com"});

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Options.Group = Graphite::BranchGrouping::Author;
	input.ExpandedGroups = {{.Section = Core::RefKind::Local, .Author = "ann@example.com"},
							{.Section = Core::RefKind::Local, .Author = "bob@example.com"}};

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	CHECK(KindsOf(rows) == std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::AuthorGroup,
													   BranchRowKind::Branch, BranchRowKind::AuthorGroup,
													   BranchRowKind::Branch, BranchRowKind::Section});
	CHECK(Graphite::SectionHeadingText(rows[0]) == "Local (1)");
	CHECK(rows[2].Depth == 1);
	CHECK(Graphite::AuthorGroupText(rows[1]) == "ann (1)");
}

TEST_CASE("Groups are collapsed until expanded with the user first and the unknown author last")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "by-zed", .TipSeconds = kBaseSeconds, .AuthorEmail = "zed@example.com"},
		{.Name = "by-amy", .TipSeconds = kBaseSeconds, .AuthorEmail = "amy@example.com"},
		{.Name = "by-me", .TipSeconds = kBaseSeconds, .AuthorEmail = "me@example.com"},
		{.Name = "ghost"},
	});

	Graphite::BranchAuthors authors;
	authors.UserEmail = "Me@Example.com";

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Options.Group = Graphite::BranchGrouping::Author;

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	std::vector<std::string> groupLabels;

	for (const BranchRow& row : rows)
	{
		if (row.Kind == BranchRowKind::AuthorGroup)
		{
			groupLabels.push_back(row.Label);
			CHECK(row.Collapsed);
		}
	}

	CHECK(groupLabels == std::vector<std::string>{"me (you)", "amy", "zed", "Unknown author"});
	CHECK(BranchNamesOf(rows, branches).empty());
}

TEST_CASE("The checked-out branch heads each of its author groups")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "newer", .TipSeconds = kBaseSeconds, .AuthorEmail = "ann@example.com"},
		{.Name = "main", .TipSeconds = kBaseSeconds - 100, .AuthorEmail = "ann@example.com", .IsHead = true},
	});

	Graphite::BranchAuthors authors;

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Options.Group = Graphite::BranchGrouping::Author;
	input.ExpandedGroups = {{.Section = Core::RefKind::Local, .Author = "ann@example.com"}};

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) == std::vector<std::string>{"main", "newer"});
}

TEST_CASE("Ticking authors while grouped shows only their groups")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "shared", .TipSeconds = kBaseSeconds},
	});

	Graphite::BranchAuthors authors;
	AddOwnAuthors(authors, branches[0], {"ann@example.com", "bob@example.com"});

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Options.Group = Graphite::BranchGrouping::Author;
	input.Filter.Authors = {"bob@example.com"};

	std::vector<std::string> groupKeys;

	for (const BranchRow& row : Graphite::BuildBranchRows(input))
	{
		if (row.Kind == BranchRowKind::AuthorGroup)
		{
			groupKeys.push_back(row.AuthorKey);
		}
	}

	CHECK(groupKeys == std::vector<std::string>{"bob@example.com"});
}

TEST_CASE("Two authors sharing a name stay apart by e-mail")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "one", .TipSeconds = kBaseSeconds, .AuthorEmail = "jan@work.example"},
		{.Name = "two", .TipSeconds = kBaseSeconds, .AuthorEmail = "jan@home.example"},
	});

	Graphite::BranchAuthors authors;

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Options.Group = Graphite::BranchGrouping::Author;

	std::vector<std::string> groupKeys;

	for (const BranchRow& row : Graphite::BuildBranchRows(input))
	{
		if (row.Kind == BranchRowKind::AuthorGroup)
		{
			groupKeys.push_back(row.AuthorKey);
		}
	}

	CHECK(groupKeys.size() == 2);
	CHECK(Graphite::ListAuthorChoices(input).size() == 2);
}

TEST_CASE("A view that needs authors shows a loading row until they land")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
		{.Name = "origin/main", .Kind = Core::RefKind::Remote, .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Options.Group = Graphite::BranchGrouping::Author;

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	CHECK(KindsOf(rows) == std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::Loading,
													   BranchRowKind::Section, BranchRowKind::Loading});
	CHECK(rows[1].Label == "Reading authors");
	CHECK(Graphite::SectionHeadingText(rows[0]) == "Local (1)");
}

TEST_CASE("A failed author read shows the failure in place of the branches")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Filter.Authors = {"ann@example.com"};
	input.AuthorsFailure = "the walk stopped";

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	REQUIRE(rows.size() >= 2);
	CHECK(rows[1].Kind == BranchRowKind::Failed);
	CHECK(rows[1].Label == "Cannot read authors: the walk stopped");
}

TEST_CASE("A sort and a text filter never wait for authors")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Filter.Text = "mai";

	CHECK(BranchNamesOf(Graphite::BuildBranchRows(input), branches) == std::vector<std::string>{"main"});
}

TEST_CASE("A collapsed section shows only its heading")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.CollapsedSections[Graphite::SectionIndexOf(Core::RefKind::Local)] = true;

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	CHECK(KindsOf(rows) == std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::Section});
	CHECK(rows.front().Collapsed);
}

TEST_CASE("A section whose branches the filter hides says so")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	BranchRowsInput input = InputFor(branches);
	input.Filter.Text = "nothing-like-it";

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(input);

	CHECK(KindsOf(rows) ==
		  std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::NoMatches, BranchRowKind::Section});
}

TEST_CASE("An empty section shows only its heading")
{
	const std::vector<Core::Branch> branches;

	const std::vector<BranchRow> rows = Graphite::BuildBranchRows(InputFor(branches));

	CHECK(KindsOf(rows) == std::vector<BranchRowKind>{BranchRowKind::Section, BranchRowKind::Section});
	CHECK(Graphite::SectionHeadingText(rows[0]) == "Local (0)");
	CHECK(Graphite::SectionHeadingText(rows[1]) == "Remote (0)");
}

TEST_CASE("Author choices count the branches the text leaves with the user first and zeros hidden unless ticked")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "login-by-amy", .TipSeconds = kBaseSeconds, .AuthorEmail = "amy@example.com"},
		{.Name = "login-by-me", .TipSeconds = kBaseSeconds, .AuthorEmail = "me@example.com"},
		{.Name = "other-by-zed", .TipSeconds = kBaseSeconds, .AuthorEmail = "zed@example.com"},
	});

	Graphite::BranchAuthors authors;
	authors.UserEmail = "me@example.com";

	BranchRowsInput input = InputFor(branches);
	input.Authors = &authors;
	input.Filter.Text = "login";
	input.Filter.Authors = {"bob@example.com"};

	const std::vector<Graphite::AuthorChoice> choices = Graphite::ListAuthorChoices(input);

	REQUIRE(choices.size() == 3);
	CHECK(choices[0].Key == "me@example.com");
	CHECK(choices[0].IsUser);
	CHECK(Graphite::AuthorChoiceText(choices[0]) == "me (you) (1)");
	CHECK(choices[1].Key == "amy@example.com");
	CHECK(choices[2].Key == "bob@example.com");
	CHECK(choices[2].Branches == 0);
	CHECK(choices[2].Ticked);
}

TEST_CASE("No author choices exist before the authors land")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "main", .TipSeconds = kBaseSeconds},
	});

	CHECK(Graphite::ListAuthorChoices(InputFor(branches)).empty());
}

TEST_CASE("A branch's tooltip names its message and authors and local time")
{
	const std::vector<Core::Branch> branches = MakeBranches({
		{.Name = "feature", .TipSeconds = kBaseSeconds, .Summary = "Fix the login page"},
	});

	const std::vector<Graphite::BranchAuthor> authors = {
		{.Key = "a@x", .Name = "Ann"}, {.Key = "b@x", .Name = "Bob"}, {.Key = "c@x", .Name = "Cid"},
		{.Key = "d@x", .Name = "Dan"}, {.Key = "e@x", .Name = "Eve"},
	};

	const std::string text =
		Graphite::BranchTooltipText(branches[0], "feature", authors, std::chrono::minutes{0});

	CHECK(text == "feature\nFix the login page\nAuthors: Ann, Bob, Cid +2\n2023-11-14 22:13");
}