#include <doctest/doctest.h>

#include "Git/Branches/BranchListFilter.h"

#include <string>
#include <vector>

namespace {

	constexpr bool kAlsoMessage = true;
	constexpr bool kNameOnly = false;

	const std::vector<Graphite::BranchAuthor> kNoAuthors;

	const std::vector<Graphite::BranchAuthor> kJan = {{.Key = "jk@example.com", .Name = "Jan Kowalski"}};

	Core::Branch BranchNamed(const std::string& name, const std::string& summary, const std::string& body)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;

		Core::CommitSummary tip;
		tip.Summary = summary;
		tip.Body = body;
		branch.TipCommit = tip;

		return branch;
	}
}

TEST_CASE("The filter text splits into lower-cased words")
{
	CHECK(Graphite::LoweredWordsOf("  PROJ   Login ") == std::vector<std::string>{"proj", "login"});
	CHECK(Graphite::LoweredWordsOf("   ").empty());
}

TEST_CASE("No words match every branch")
{
	CHECK(Graphite::MatchesWords(BranchNamed("main", "", ""), kNoAuthors, {}, kNameOnly));
}

TEST_CASE("A word is looked for in the short name and not in the full ref name")
{
	const Core::Branch main = BranchNamed("main", "", "");

	CHECK(Graphite::MatchesWords(main, kNoAuthors, {"mai"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(main, kNoAuthors, {"heads"}, kNameOnly));
}

TEST_CASE("Every word must be in the name whatever its case")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12-Login", "", "");

	CHECK(Graphite::MatchesWords(feature, kNoAuthors, {"proj", "login"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(feature, kNoAuthors, {"proj", "logout"}, kNameOnly));
}

TEST_CASE("With also message a word may be in the tip's summary or body instead")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "Fix the Login page", "Hotfix for the release\n");

	CHECK(Graphite::MatchesWords(feature, kNoAuthors, {"proj", "login"}, kAlsoMessage));
	CHECK(Graphite::MatchesWords(feature, kNoAuthors, {"hotfix"}, kAlsoMessage));
	CHECK_FALSE(Graphite::MatchesWords(feature, kNoAuthors, {"login"}, kNameOnly));
}

TEST_CASE("Also message on a branch without a readable tip looks only at the name")
{
	Core::Branch ghost = BranchNamed("ghost", "", "");
	ghost.TipCommit.reset();

	CHECK(Graphite::MatchesWords(ghost, kNoAuthors, {"gho"}, kAlsoMessage));
	CHECK_FALSE(Graphite::MatchesWords(ghost, kNoAuthors, {"fix"}, kAlsoMessage));
}

TEST_CASE("A word may be in an author's name instead of the branch name")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "", "");

	CHECK(Graphite::MatchesWords(feature, kJan, {"kowal"}, kNameOnly));
	CHECK(Graphite::MatchesWords(feature, kJan, {"proj", "jan"}, kNameOnly));
}

TEST_CASE("A word may be in an author's e-mail instead of the branch name")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "", "");

	CHECK(Graphite::MatchesWords(feature, kJan, {"jk@"}, kNameOnly));
	CHECK(Graphite::MatchesWords(feature, kJan, {"example.com"}, kNameOnly));
}

TEST_CASE("A word in neither the name nor an author fails the branch")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "Fix the login page", "");

	CHECK_FALSE(Graphite::MatchesWords(feature, kJan, {"login"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(feature, kJan, {"proj", "anna"}, kNameOnly));
}

TEST_CASE("With also message and authors a word may be in any of the three")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "Fix the login page", "");

	CHECK(Graphite::MatchesWords(feature, kJan, {"proj", "login", "kowalski"}, kAlsoMessage));
	CHECK_FALSE(Graphite::MatchesWords(feature, kJan, {"proj", "login", "kowalski"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(feature, kJan, {"logout"}, kAlsoMessage));
}

TEST_CASE("An author holds a word in the name or the e-mail")
{
	const Graphite::BranchAuthor jan = kJan.front();

	CHECK(Graphite::AuthorHoldsAnyWord(jan, {"zzz", "kowal"}));
	CHECK(Graphite::AuthorHoldsAnyWord(jan, {"jk@"}));
	CHECK_FALSE(Graphite::AuthorHoldsAnyWord(jan, {"anna"}));
	CHECK_FALSE(Graphite::AuthorHoldsAnyWord(jan, {}));
}

TEST_CASE("No word matches the unknown author's stand-in name")
{
	Core::Branch ghost = BranchNamed("ghost", "", "");
	ghost.TipCommit.reset();

	const std::vector<Graphite::BranchAuthor> unknown = {{.Key = "", .Name = "Unknown author"}};

	CHECK_FALSE(Graphite::MatchesWords(ghost, unknown, {"unknown"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(ghost, unknown, {"auth"}, kAlsoMessage));
	CHECK_FALSE(Graphite::AuthorHoldsAnyWord(unknown.front(), {"unknown"}));
}

TEST_CASE("The word to mark is the first one the name holds")
{
	const std::vector<std::string> words = {"fix", "proj"};

	CHECK(Graphite::FirstWordInName("feature/PROJ-12", words) == "proj");
	CHECK(Graphite::FirstWordInName("main", words).empty());
}
