#include <doctest/doctest.h>

#include "Core/Git/Repository.h"
#include "Core/Git/Tag.h"

#include "GitFixture.h"

#include <git2.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr std::int64_t kFirstCommitTime = 1000000000;

	std::chrono::sys_seconds At(std::int64_t seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}

	Core::Oid OidOf(const std::string& hex)
	{
		const std::optional<Core::Oid> id = Core::Oid::FromHex(hex);
		REQUIRE(id.has_value());
		return *id;
	}

	std::string TreeHexOfHead(const GitFixture& fixture)
	{
		git_repository* handle = nullptr;
		REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

		git_object* tree = nullptr;
		REQUIRE(git_revparse_single(&tree, handle, "HEAD^{tree}") == 0);

		const std::string hex = HexTextOf(*git_object_id(tree));

		git_object_free(tree);
		git_repository_free(handle);
		return hex;
	}

	std::vector<Core::Tag> ListTags(const GitFixture& fixture, Core::TagCache& cache)
	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		Core::Result<std::vector<Core::Tag>> tags = repository.QueryTags(cache);
		REQUIRE(tags.HasValue());
		return std::move(tags).GetValue();
	}

	std::vector<Core::Tag> ListTags(const GitFixture& fixture)
	{
		Core::TagCache cache;
		return ListTags(fixture, cache);
	}
}

TEST_CASE("Tags: a lightweight tag names its commit with the commit's time")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string second = fixture.Commit("second");
	fixture.TagLightweight("v1", first);

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 1);
	CHECK(tags[0].Name == "v1");
	CHECK(tags[0].FullName == "refs/tags/v1");
	CHECK(tags[0].Target == OidOf(first));
	CHECK(tags[0].When == At(kFirstCommitTime));
	CHECK_FALSE(tags[0].Annotated);
	CHECK(tags[0].Subject.empty());
	CHECK(OidOf(second) != tags[0].Target);
}

TEST_CASE("Tags: an annotated tag is peeled to its commit and dated by its tagger")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	fixture.TagAnnotated("release/1.2", commit, 1500000000, "Release 1.2\n\nLonger text.\n");

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 1);
	CHECK(tags[0].Name == "release/1.2");
	CHECK(tags[0].FullName == "refs/tags/release/1.2");
	CHECK(tags[0].Target == OidOf(commit));
	CHECK(tags[0].When == At(1500000000));
	CHECK(tags[0].Annotated);
	CHECK(tags[0].Subject == "Release 1.2");
}

TEST_CASE("Tags: a tag of a tag is peeled to the commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	const std::string inner = fixture.TagAnnotated("inner", commit, 1500000000, "Inner\n");
	fixture.TagAnnotated("outer", inner, 1600000000, "Outer\n");

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 2);
	CHECK(tags[0].Name == "outer");
	CHECK(tags[0].Target == OidOf(commit));
	CHECK(tags[0].When == At(1600000000));
	CHECK(tags[0].Subject == "Outer");
	CHECK(tags[1].Name == "inner");
	CHECK(tags[1].Target == OidOf(commit));
}

TEST_CASE("Tags: a tag of a tree or a blob is not listed")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	const std::string tree = TreeHexOfHead(fixture);
	const std::string blob = BlobHexOf("one\n");

	fixture.TagObject("lightweight-tree", tree);
	fixture.TagObject("lightweight-blob", blob);
	fixture.TagAnnotated("annotated-tree", tree, 1500000000, "Tree\n");
	fixture.TagAnnotated("annotated-blob", blob, 1500000000, "Blob\n");

	CHECK(ListTags(fixture).empty());
}

TEST_CASE("Tags: a tag whose commit is missing is not listed and is no failure")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string second = fixture.Commit("second");
	fixture.Write("a.txt", "three\n");
	const std::string third = fixture.Commit("third");

	fixture.TagLightweight("light", second);
	fixture.TagAnnotated("annotated", second, 1500000000, "Gone\n");
	fixture.TagLightweight("kept", first);
	fixture.TagLightweight("also-kept", third);
	fixture.RemoveObject(second);

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 2);
	CHECK(tags[0].Name == "also-kept");
	CHECK(tags[1].Name == "kept");
}

TEST_CASE("Tags: two tags on one commit are both listed, newest first then by name")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	fixture.TagAnnotated("beta", commit, 1500000000, "Beta\n");
	fixture.TagAnnotated("alpha", commit, 1500000000, "Alpha\n");
	fixture.TagAnnotated("newest", commit, 1700000000, "Newest\n");
	fixture.TagLightweight("light", commit);

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 4);
	CHECK(tags[0].Name == "newest");
	CHECK(tags[1].Name == "alpha");
	CHECK(tags[2].Name == "beta");
	CHECK(tags[3].Name == "light");
	CHECK(tags[3].When == At(kFirstCommitTime));
	CHECK(tags[0].Target == tags[3].Target);
}

TEST_CASE("Tags: a re-read takes a known annotated tag from the cache")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	const std::string tagObject = fixture.TagAnnotated("v1", commit, 1500000000, "Release\n");
	fixture.TagLightweight("light", commit);

	Core::TagCache cache;
	const std::vector<Core::Tag> firstRead = ListTags(fixture, cache);
	REQUIRE(firstRead.size() == 2);
	CHECK(cache.GetSize() == 1);
	REQUIRE(cache.Find(OidOf(tagObject)).has_value());

	// With the tag object gone, only the cache can still answer for it.
	fixture.RemoveObject(tagObject);

	const std::vector<Core::Tag> secondRead = ListTags(fixture, cache);
	CHECK(cache.GetSize() == 1);
	CHECK(secondRead == firstRead);
}

TEST_CASE("Tags: a repository without tags lists none")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	CHECK(ListTags(fixture).empty());
}

TEST_CASE("Tags: a symbolic ref under refs/tags is not listed")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	fixture.TagLightweight("real", commit);

	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);
	git_reference* alias = nullptr;
	REQUIRE(git_reference_symbolic_create(&alias, handle, "refs/tags/alias", "refs/tags/real", 1, nullptr) == 0);
	git_reference_free(alias);
	git_repository_free(handle);

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 1);
	CHECK(tags[0].Name == "real");
}

TEST_CASE("Tags: a tag of a tag whose inner tag is missing is not listed and is no failure")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");
	const std::string inner = fixture.TagAnnotated("inner", commit, 1500000000, "Inner\n");
	fixture.TagAnnotated("outer", inner, 1600000000, "Outer\n");
	fixture.TagLightweight("kept", commit);
	fixture.RemoveObject(inner);

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 1);
	CHECK(tags[0].Name == "kept");
}

TEST_CASE("Tags: an annotated tag with no tagger is dated by its commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string commit = fixture.Commit("first");

	// libgit2 writes a tag object only with a tagger, so the object is written from its raw text.
	const std::string raw = "object " + commit + "\ntype commit\ntag untagged\n\nNo tagger\n";
	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);
	git_odb* odb = nullptr;
	REQUIRE(git_repository_odb(&odb, handle) == 0);
	git_oid tagId{};
	REQUIRE(git_odb_write(&tagId, odb, raw.data(), raw.size(), GIT_OBJECT_TAG) == 0);
	git_odb_free(odb);
	git_repository_free(handle);
	fixture.TagObject("untagged", HexTextOf(tagId));

	const std::vector<Core::Tag> tags = ListTags(fixture);

	REQUIRE(tags.size() == 1);
	CHECK(tags[0].Annotated);
	CHECK(tags[0].Target == OidOf(commit));
	CHECK(tags[0].When == At(kFirstCommitTime));
	CHECK(tags[0].Subject == "No tagger");
}
