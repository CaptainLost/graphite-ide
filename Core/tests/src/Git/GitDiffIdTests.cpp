#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <git2.h>

#include <filesystem>
#include <string>

namespace {

	std::string BlobIdOf(const std::string& content)
	{
		git_oid blobId{};
		REQUIRE(git_odb_hash(&blobId, content.data(), content.size(), GIT_OBJECT_BLOB) == 0);
		return HexTextOf(blobId);
	}

	const Core::FileDiff& OnlyFileOf(const Core::Result<Core::Diff>& diff)
	{
		REQUIRE(diff.HasValue());
		REQUIRE(diff.GetValue().Files.size() == 1);
		return diff.GetValue().Files.front();
	}

	Core::Result<Core::Diff> RangeDiff(const Core::Repository& repository, const std::string& fromReference,
									   const std::string& toReference)
	{
		Core::DiffRequest request;
		request.Target = Core::DiffTarget::RevisionToRevision;
		request.From = repository.ResolveReference(fromReference).GetValue();
		request.To = repository.ResolveReference(toReference).GetValue();
		return repository.QueryDiff(request);
	}

	Core::Result<Core::Diff> WorkingTreeDiff(const Core::Repository& repository)
	{
		Core::DiffRequest request;
		request.Target = Core::DiffTarget::WorkTreeToHead;
		return repository.QueryDiff(request);
	}
}

TEST_CASE("A modified file between two revisions carries both blob ids")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("before");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Diff> diff = RangeDiff(repository, "refs/heads/before", "refs/heads/master");
	const Core::FileDiff& file = OnlyFileOf(diff);

	CHECK(file.OldId.ToHex() == BlobIdOf("one\n"));
	CHECK(file.NewId.ToHex() == BlobIdOf("two\n"));
}

TEST_CASE("An added file has no old blob id and a deleted file has no new one")
{
	GitFixture fixture;
	fixture.Write("kept.txt", "kept\n");
	fixture.Write("gone.txt", "gone\n");
	fixture.Commit("one");
	fixture.Branch("before");
	fixture.Remove("gone.txt");
	fixture.Write("new.txt", "new\n");
	fixture.Commit("two");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Diff> diff = RangeDiff(repository, "refs/heads/before", "refs/heads/master");
	REQUIRE(diff.HasValue());

	bool sawAdded = false;
	bool sawDeleted = false;

	for (const Core::FileDiff& file : diff.GetValue().Files)
	{
		if (file.Change == Core::FileChange::Added)
		{
			sawAdded = true;
			CHECK(file.OldId.IsZero());
			CHECK(file.NewId.ToHex() == BlobIdOf("new\n"));
		}

		if (file.Change == Core::FileChange::Deleted)
		{
			sawDeleted = true;
			CHECK(file.OldId.ToHex() == BlobIdOf("gone\n"));
			CHECK(file.NewId.IsZero());
		}
	}

	CHECK(sawAdded);
	CHECK(sawDeleted);
}

TEST_CASE("A file edited in the working tree carries the id of what is on disk")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "one\nmore\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Diff> diff = WorkingTreeDiff(repository);
	const Core::FileDiff& file = OnlyFileOf(diff);

	CHECK(file.OldId.ToHex() == BlobIdOf("one\n"));
	CHECK(file.NewId.ToHex() == BlobIdOf("one\nmore\n"));
}

TEST_CASE("An untracked file carries the id of its content and no old id")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("untracked.txt", "fresh\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Diff> diff = WorkingTreeDiff(repository);
	const Core::FileDiff& file = OnlyFileOf(diff);

	CHECK(file.Change == Core::FileChange::Added);
	CHECK(file.OldId.IsZero());
	CHECK(file.NewId.ToHex() == BlobIdOf("fresh\n"));
}
