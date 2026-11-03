#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>
#include <vector>

using Core::OwnAuthorsRequest;
using Core::Repository;
using Core::TipAuthors;

namespace {

	Core::Oid TipOf(const Repository& repository, const std::string& fullName)
	{
		const Core::Result<Core::Oid> tip = repository.ResolveReference(fullName);
		REQUIRE(tip.HasValue());
		return tip.GetValue();
	}

	std::vector<std::string> EmailsOf(const TipAuthors& answer)
	{
		std::vector<std::string> emails;

		for (const Core::Signature& author : answer.Authors)
		{
			emails.push_back(author.Email);
		}

		return emails;
	}

	std::vector<TipAuthors> AnswersTo(const Repository& repository, const OwnAuthorsRequest& request)
	{
		const Core::Result<std::vector<TipAuthors>> answers = repository.QueryOwnAuthors(request);
		REQUIRE(answers.HasValue());
		return answers.GetValue();
	}

	TipAuthors OnlyAnswerTo(const Repository& repository, const OwnAuthorsRequest& request)
	{
		const std::vector<TipAuthors> answers = AnswersTo(repository, request);
		REQUIRE(answers.size() == 1);
		return answers.front();
	}

	// master holds one commit by the default author; feature, cut from it, one by Ann and one by Bob.
	void BuildFeatureByAnnAndBob(GitFixture& fixture)
	{
		fixture.Write("a.txt", "trunk\n");
		fixture.Commit("trunk");
		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Write("b.txt", "one\n");
		fixture.CommitAs("first", "Ann", "ann@example.com");
		fixture.Write("b.txt", "two\n");
		fixture.CommitAs("second", "Bob", "bob@example.com");
	}
}

TEST_CASE("A branch's own commits name each of their authors newest first")
{
	GitFixture fixture;
	BuildFeatureByAnnAndBob(fixture);

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const TipAuthors answer =
		OnlyAnswerTo(repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/feature")},
												  .Hidden = {TipOf(repository, "refs/heads/master")}});

	CHECK(answer.OwnCommits == 2);
	CHECK_FALSE(answer.Truncated);
	CHECK(EmailsOf(answer) == std::vector<std::string>{"bob@example.com", "ann@example.com"});
}

TEST_CASE("An author is told apart by e-mail whatever its case and keeps the name of their newest commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "trunk\n");
	fixture.Commit("trunk");
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("b.txt", "one\n");
	fixture.CommitAs("first", "Ann", "ann@example.com");
	fixture.Write("b.txt", "two\n");
	fixture.CommitAs("second", "Ann Nowak", "ANN@example.com");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const TipAuthors answer =
		OnlyAnswerTo(repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/feature")},
												  .Hidden = {TipOf(repository, "refs/heads/master")}});

	REQUIRE(answer.Authors.size() == 1);
	CHECK(answer.Authors.front().Name == "Ann Nowak");
	CHECK(answer.OwnCommits == 2);
}

TEST_CASE("A merged branch has no own commits")
{
	GitFixture fixture;
	BuildFeatureByAnnAndBob(fixture);
	fixture.Checkout("master");
	fixture.MergeCommit("merge feature", "feature");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const TipAuthors answer =
		OnlyAnswerTo(repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/feature")},
												  .Hidden = {TipOf(repository, "refs/heads/master")}});

	CHECK(answer.OwnCommits == 0);
	CHECK(answer.Authors.empty());
}

TEST_CASE("Hiding the trunk's upstream keeps commits pulled from it out of a branch cut after them")
{
	GitFixture fixture;
	fixture.Write("a.txt", "trunk\n");
	fixture.Commit("trunk");
	fixture.Branch("pulled");
	fixture.Checkout("pulled");
	fixture.Write("a.txt", "pulled\n");
	const std::string pulledCommit = fixture.CommitAs("trunk work", "Tess", "tess@example.com");
	fixture.PushTo("master", pulledCommit);
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("b.txt", "feature\n");
	fixture.CommitAs("feature work", "Ann", "ann@example.com");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const TipAuthors answer = OnlyAnswerTo(
		repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/feature")},
									  .Hidden = {TipOf(repository, "refs/heads/master"),
												 TipOf(repository, "refs/remotes/origin/master")}});

	CHECK(answer.OwnCommits == 1);
	CHECK(EmailsOf(answer) == std::vector<std::string>{"ann@example.com"});
}

TEST_CASE("A branch stacked on another counts the lower branch's commits as its own too")
{
	GitFixture fixture;
	fixture.Write("a.txt", "trunk\n");
	fixture.Commit("trunk");
	fixture.Branch("lower");
	fixture.Checkout("lower");
	fixture.Write("b.txt", "lower\n");
	fixture.CommitAs("lower work", "Bob", "bob@example.com");
	fixture.Branch("upper");
	fixture.Checkout("upper");
	fixture.Write("c.txt", "upper\n");
	fixture.CommitAs("upper work", "Ann", "ann@example.com");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const std::vector<TipAuthors> answers = AnswersTo(
		repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/upper"), TipOf(repository, "refs/heads/lower")},
									  .Hidden = {TipOf(repository, "refs/heads/master")}});

	REQUIRE(answers.size() == 2);
	CHECK(answers[0].Tip == TipOf(repository, "refs/heads/upper"));
	CHECK(EmailsOf(answers[0]) == std::vector<std::string>{"ann@example.com", "bob@example.com"});
	CHECK(EmailsOf(answers[1]) == std::vector<std::string>{"bob@example.com"});
}

TEST_CASE("A branch with no history in common with the trunk stops at the cap and says so")
{
	GitFixture fixture;
	fixture.Write("a.txt", "trunk\n");
	fixture.Commit("trunk");
	fixture.StartOrphan("pages");
	fixture.Write("p.txt", "one\n");
	fixture.CommitAs("page one", "Carl", "carl@example.com");
	fixture.Write("p.txt", "two\n");
	fixture.CommitAs("page two", "Carl", "carl@example.com");
	fixture.Write("p.txt", "three\n");
	fixture.CommitAs("page three", "Carl", "carl@example.com");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const std::size_t cap = 2;
	const TipAuthors answer = OnlyAnswerTo(repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/pages")},
																		.Hidden = {TipOf(repository, "refs/heads/master")},
																		.MaxCommitsPerTip = cap});

	CHECK(answer.OwnCommits == cap);
	CHECK(answer.Truncated);
	CHECK(EmailsOf(answer) == std::vector<std::string>{"carl@example.com"});
}

TEST_CASE("With nothing hidden every commit a tip reaches is its own")
{
	GitFixture fixture;
	BuildFeatureByAnnAndBob(fixture);

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const TipAuthors answer =
		OnlyAnswerTo(repository, OwnAuthorsRequest{.Tips = {TipOf(repository, "refs/heads/feature")}});

	CHECK(answer.OwnCommits == 3);
	CHECK(EmailsOf(answer) == std::vector<std::string>{"bob@example.com", "ann@example.com", "test@example.com"});
}

TEST_CASE("Two branches sharing a tip get one answer")
{
	GitFixture fixture;
	BuildFeatureByAnnAndBob(fixture);

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Oid featureTip = TipOf(repository, "refs/heads/feature");

	const std::vector<TipAuthors> answers = AnswersTo(
		repository, OwnAuthorsRequest{.Tips = {featureTip, featureTip}, .Hidden = {TipOf(repository, "refs/heads/master")}});

	CHECK(answers.size() == 1);
}

TEST_CASE("A request with no tips is answered with nothing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "trunk\n");
	fixture.Commit("trunk");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(AnswersTo(repository, OwnAuthorsRequest{}).empty());
}

TEST_CASE("An author's key is the e-mail lower-cased without surrounding spaces")
{
	CHECK(Core::AuthorKeyOf("  Ann@Example.COM ") == "ann@example.com");
}