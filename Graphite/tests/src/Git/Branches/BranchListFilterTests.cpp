#include <doctest/doctest.h>

#include "Git/Branches/BranchListFilter.h"

#include <string>
#include <vector>

namespace {

	constexpr bool kAlsoMessage = true;
	constexpr bool kNameOnly = false;

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
	CHECK(Graphite::MatchesWords(BranchNamed("main", "", ""), {}, kNameOnly));
}

TEST_CASE("A word is looked for in the short name and not in the full ref name")
{
	const Core::Branch main = BranchNamed("main", "", "");

	CHECK(Graphite::MatchesWords(main, {"mai"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(main, {"heads"}, kNameOnly));
}

TEST_CASE("Every word must be in the name whatever its case")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12-Login", "", "");

	CHECK(Graphite::MatchesWords(feature, {"proj", "login"}, kNameOnly));
	CHECK_FALSE(Graphite::MatchesWords(feature, {"proj", "logout"}, kNameOnly));
}

TEST_CASE("With also message a word may be in the tip's summary or body instead")
{
	const Core::Branch feature = BranchNamed("feature/PROJ-12", "Fix the Login page", "Hotfix for the release\n");

	CHECK(Graphite::MatchesWords(feature, {"proj", "login"}, kAlsoMessage));
	CHECK(Graphite::MatchesWords(feature, {"hotfix"}, kAlsoMessage));
	CHECK_FALSE(Graphite::MatchesWords(feature, {"login"}, kNameOnly));
}

TEST_CASE("Also message on a branch without a readable tip looks only at the name")
{
	Core::Branch ghost = BranchNamed("ghost", "", "");
	ghost.TipCommit.reset();

	CHECK(Graphite::MatchesWords(ghost, {"gho"}, kAlsoMessage));
	CHECK_FALSE(Graphite::MatchesWords(ghost, {"fix"}, kAlsoMessage));
}

TEST_CASE("The word to mark is the first one the name holds")
{
	const std::vector<std::string> words = {"fix", "proj"};

	CHECK(Graphite::FirstWordInName("feature/PROJ-12", words) == "proj");
	CHECK(Graphite::FirstWordInName("main", words).empty());
}
