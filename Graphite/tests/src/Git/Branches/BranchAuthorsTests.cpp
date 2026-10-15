#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Branches/BranchAuthors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

	constexpr std::uint8_t kMainTipByte = 1;
	constexpr std::uint8_t kOriginMainTipByte = 2;
	constexpr std::uint8_t kFeatureTipByte = 3;
	constexpr std::uint8_t kUnreadableTipByte = 4;
	constexpr long long kTipSeconds = 1700000000;

	Core::Signature Person(const std::string& name, const std::string& email, long long seconds)
	{
		return Core::Signature{.Name = name, .Email = email, .When = SystemSecondsAt(seconds)};
	}

	Core::Branch BranchWithTip(const std::string& name, Core::RefKind kind, std::uint8_t tipByte)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = (kind == Core::RefKind::Local ? "refs/heads/" : "refs/remotes/") + name;
		branch.Kind = kind;
		branch.Tip = OidWithFirstByte(tipByte);

		Core::CommitSummary tip;
		tip.Id = branch.Tip;
		tip.Author = Person("Tip Author", "Tip@Example.com", kTipSeconds);
		tip.Committer = tip.Author;
		branch.TipCommit = tip;

		return branch;
	}

	std::vector<std::string> KeysOf(const std::vector<Graphite::BranchAuthor>& authors)
	{
		std::vector<std::string> keys;

		for (const Graphite::BranchAuthor& author : authors)
		{
			keys.push_back(author.Key);
		}

		return keys;
	}
}

TEST_CASE("An author's key is the e-mail lower-cased without surrounding spaces")
{
	CHECK(Graphite::AuthorKeyOf("  Ann@Example.COM ") == "ann@example.com");
}

TEST_CASE("The authors request lists every tip once in a fixed order and hides the trunk")
{
	Core::Branch main = BranchWithTip("main", Core::RefKind::Local, kMainTipByte);
	main.IsHead = true;
	main.Upstream = "origin/main";

	const std::vector<Core::Branch> branches = {
		BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte),
		main,
		BranchWithTip("origin/main", Core::RefKind::Remote, kOriginMainTipByte),
		BranchWithTip("origin/feature", Core::RefKind::Remote, kFeatureTipByte),
	};

	const Core::OwnAuthorsRequest request = Graphite::OwnAuthorsRequestFor(branches);

	CHECK(request.Tips == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte),
												 OidWithFirstByte(kFeatureTipByte)});
	CHECK(request.Hidden == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("A branch whose tip cannot be read is left out of the authors request")
{
	Core::Branch main = BranchWithTip("main", Core::RefKind::Local, kMainTipByte);
	main.IsHead = true;

	Core::Branch unreadable = BranchWithTip("unreadable", Core::RefKind::Local, kUnreadableTipByte);
	unreadable.TipCommit.reset();

	const std::vector<Core::Branch> branches = {
		BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte),
		main,
		unreadable,
	};

	const Core::OwnAuthorsRequest request = Graphite::OwnAuthorsRequestFor(branches);

	CHECK(request.Tips == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kFeatureTipByte)});
	CHECK(request.Hidden == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte)});
}

TEST_CASE("A trunk branch whose tip cannot be read is not hidden from the authors request")
{
	Core::Branch main = BranchWithTip("main", Core::RefKind::Local, kMainTipByte);
	main.IsHead = true;
	main.Upstream = "origin/main";
	main.TipCommit.reset();

	const std::vector<Core::Branch> branches = {
		BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte),
		main,
		BranchWithTip("origin/main", Core::RefKind::Remote, kOriginMainTipByte),
	};

	const Core::OwnAuthorsRequest request = Graphite::OwnAuthorsRequestFor(branches);

	CHECK(request.Tips ==
		  std::vector<Core::Oid>{OidWithFirstByte(kOriginMainTipByte), OidWithFirstByte(kFeatureTipByte)});
	CHECK(request.Hidden == std::vector<Core::Oid>{OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("A branch with own commits is written by their authors")
{
	const Core::Branch feature = BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte);

	Graphite::BranchAuthors authors;
	authors.ByTip.emplace(feature.Tip, Core::TipAuthors{.Tip = feature.Tip,
														.Authors = {Person("Bob", "bob@example.com", kTipSeconds),
																	Person("Ann", "Ann@Example.com", kTipSeconds - 1)},
														.OwnCommits = 2});

	const std::vector<Graphite::BranchAuthor> found = Graphite::AuthorsOfBranch(feature, authors);

	CHECK(KeysOf(found) == std::vector<std::string>{"bob@example.com", "ann@example.com"});
	CHECK(found[1].Name == "Ann");
}

TEST_CASE("A branch with no own commits is written by its tip's author")
{
	const Core::Branch feature = BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte);

	Graphite::BranchAuthors authors;
	authors.ByTip.emplace(feature.Tip, Core::TipAuthors{.Tip = feature.Tip});

	const std::vector<Graphite::BranchAuthor> found = Graphite::AuthorsOfBranch(feature, authors);

	CHECK(KeysOf(found) == std::vector<std::string>{"tip@example.com"});
	CHECK(found.front().Name == "Tip Author");
}

TEST_CASE("A branch missing from the answer is written by its tip's author")
{
	const Core::Branch feature = BranchWithTip("feature", Core::RefKind::Local, kFeatureTipByte);

	CHECK(KeysOf(Graphite::AuthorsOfBranch(feature, Graphite::BranchAuthors{})) ==
		  std::vector<std::string>{"tip@example.com"});
}

TEST_CASE("A branch with no readable tip and no own commits is written by the unknown author")
{
	Core::Branch ghost = BranchWithTip("ghost", Core::RefKind::Local, kFeatureTipByte);
	ghost.TipCommit.reset();

	const std::vector<Graphite::BranchAuthor> found = Graphite::AuthorsOfBranch(ghost, Graphite::BranchAuthors{});

	REQUIRE(found.size() == 1);
	CHECK(found.front().Key.empty());
	CHECK(found.front().Name == Graphite::kUnknownAuthorName);
}

TEST_CASE("The user's key is recognised whatever the case of the configured e-mail")
{
	Graphite::BranchAuthors authors;
	authors.UserEmail = "Ann@Example.com";

	CHECK(Graphite::IsUserKey("ann@example.com", authors));
	CHECK_FALSE(Graphite::IsUserKey("bob@example.com", authors));
	CHECK_FALSE(Graphite::IsUserKey("", Graphite::BranchAuthors{}));
}