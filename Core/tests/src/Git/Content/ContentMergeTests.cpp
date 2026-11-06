#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <git2.h>

#include <optional>
#include <string>
#include <utility>

namespace {

	const std::string kBase = "1\n2\n3\n4\n5\n6\n7\n8\n9\n";
	const std::string kTopicEdit = "1\ntwo\n3\n4\n5\n6\n7\n8\n9\n";
	const std::string kTopicMoreEdit = "1\ntwo\n3\n4\n5\n6\n7\neight\n9\n";
	const std::string kLineEightEdit = "1\n2\n3\n4\n5\n6\n7\neight\n9\n";

	Core::Oid OidOf(const std::string& hex)
	{
		const std::optional<Core::Oid> id = Core::Oid::FromHex(hex);
		REQUIRE(id.has_value());
		return *id;
	}

	Core::Repository OpenInMemory(const GitFixture& fixture)
	{
		Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath(), Core::ObjectWrites::InMemory);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	// master holds a.txt as kBase; topic is branched from it and left checked out.
	void StartTopic(GitFixture& fixture)
	{
		fixture.Write("a.txt", kBase);
		fixture.Commit("base");
		fixture.Branch("topic");
		fixture.Checkout("topic");
	}

	std::string BlobIdOf(const std::string& contents)
	{
		git_oid id{};
		REQUIRE(git_odb_hash(&id, contents.data(), contents.size(), GIT_OBJECT_BLOB) == 0);
		char hex[GIT_OID_SHA1_HEXSIZE + 1] = {};
		git_oid_tostr(hex, sizeof(hex), &id);
		return hex;
	}
}

TEST_CASE("Content: a squash of the branch on the line changes nothing when merged")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	fixture.Commit("topic one");
	fixture.Write("a.txt", kTopicMoreEdit);
	const std::string topicTip = fixture.Commit("topic two");

	fixture.Checkout("master");
	fixture.Write("a.txt", kTopicMoreEdit);
	const std::string masterTip = fixture.Commit("squash of topic");

	const Core::Repository repository = OpenInMemory(fixture);
	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue() == Core::ContentAnswer::Yes);
}

TEST_CASE("Content: a branch with a change the line lacks changes the line")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	const std::string topicTip = fixture.Commit("topic");

	fixture.Checkout("master");
	fixture.Write("b.txt", "other\n");
	const std::string masterTip = fixture.Commit("unrelated");

	const Core::Repository repository = OpenInMemory(fixture);
	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue() == Core::ContentAnswer::No);
}

TEST_CASE("Content: a conflict answers no")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	const std::string topicTip = fixture.Commit("topic");

	fixture.Checkout("master");
	fixture.Write("a.txt", "1\nzwei\n3\n4\n5\n6\n7\n8\n9\n");
	const std::string masterTip = fixture.Commit("master edits the same line");

	const Core::Repository repository = OpenInMemory(fixture);
	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue() == Core::ContentAnswer::No);
}

TEST_CASE("Content: the check writes no object into the repository")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");

	// The line edits another line of the same file, so merging builds a new file: a blob and a
	// tree that no commit holds, which must not reach the object directory.
	fixture.Checkout("master");
	fixture.Write("a.txt", kLineEightEdit);
	const std::string masterTip = fixture.Commit("master edits line eight");

	const Core::Repository repository = OpenInMemory(fixture);
	const std::size_t before = fixture.ObjectFileCount();

	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue() == Core::ContentAnswer::No);
	CHECK(fixture.ObjectFileCount() == before);

	repository.DropObjectsInMemory();
	CHECK(fixture.ObjectFileCount() == before);
}

TEST_CASE("Content: a missing blob answers unknown")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	const std::string topicTip = fixture.Commit("topic");

	fixture.Checkout("master");
	fixture.Write("a.txt", kLineEightEdit);
	const std::string masterTip = fixture.Commit("master edits another line");

	fixture.RemoveObject(BlobIdOf(kTopicEdit));

	const Core::Repository repository = OpenInMemory(fixture);
	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue() == Core::ContentAnswer::Unknown);
}

TEST_CASE("Content: dropping the objects of a moved-from repository touches nothing")
{
	GitFixture fixture;
	StartTopic(fixture);

	Core::Repository movedFrom = OpenInMemory(fixture);

	{
		const Core::Repository owner = std::move(movedFrom);
	}

	movedFrom.DropObjectsInMemory();

	CHECK_FALSE(movedFrom.HeadCommit().HasValue());
}

TEST_CASE("Content: the in-memory check needs a repository opened for it")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kTopicEdit);
	const std::string topicTip = fixture.Commit("topic");
	fixture.Checkout("master");
	fixture.Write("b.txt", "other\n");
	const std::string masterTip = fixture.Commit("unrelated");

	Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath());
	REQUIRE(opened.HasValue());
	const Core::Repository repository = std::move(opened).GetValue();

	const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(OidOf(masterTip), OidOf(topicTip));

	REQUIRE_FALSE(answer.HasValue());
	CHECK(answer.GetError().Code == Core::ErrorCode::InvalidArgument);
}
