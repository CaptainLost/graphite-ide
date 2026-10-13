#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <git2.h>

#include <string>

namespace {

	Core::Oid BlobIdOf(const std::string& content)
	{
		git_oid blobId{};
		REQUIRE(git_odb_hash(&blobId, content.data(), content.size(), GIT_OBJECT_BLOB) == 0);

		Core::Oid id;
		for (std::size_t byteIndex = 0; byteIndex < Core::Oid::kByteCount; ++byteIndex)
		{
			id.Bytes[byteIndex] = blobId.id[byteIndex];
		}

		return id;
	}

	std::size_t CountLinesOfKind(const Core::FileDiff& file, Core::DiffLineKind kind)
	{
		std::size_t count = 0;

		for (const Core::Hunk& hunk : file.Hunks)
		{
			for (const Core::DiffLine& line : hunk.Lines)
			{
				if (line.Kind == kind)
				{
					++count;
				}
			}
		}

		return count;
	}
}

TEST_CASE("A committed blob is read back by its id")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<std::string> bytes = repository.ReadBlobById(BlobIdOf("one\n"));

	REQUIRE(bytes.HasValue());
	CHECK(bytes.GetValue() == "one\n");
}

TEST_CASE("A blob the object database does not hold is NotFound")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<std::string> bytes = repository.ReadBlobById(BlobIdOf("never written\n"));

	REQUIRE_FALSE(bytes.HasValue());
	CHECK(bytes.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Two texts differ by the lines that changed")
{
	const Core::Result<Core::FileDiff> diff =
		Core::Repository::DiffTexts("a\nb\nc\n", "a\nB\nc\n", "f.txt", Core::WhitespaceInDiff::Shown);

	REQUIRE(diff.HasValue());
	CHECK(diff.GetValue().Change == Core::FileChange::Modified);
	CHECK(diff.GetValue().OldPath == std::filesystem::path{"f.txt"});
	CHECK(diff.GetValue().NewPath == std::filesystem::path{"f.txt"});
	CHECK(CountLinesOfKind(diff.GetValue(), Core::DiffLineKind::Removed) == 1);
	CHECK(CountLinesOfKind(diff.GetValue(), Core::DiffLineKind::Added) == 1);
}

TEST_CASE("Equal texts give a file with no hunks")
{
	const Core::Result<Core::FileDiff> diff =
		Core::Repository::DiffTexts("same\n", "same\n", "f.txt", Core::WhitespaceInDiff::Shown);

	REQUIRE(diff.HasValue());
	CHECK(diff.GetValue().Change == Core::FileChange::Unmodified);
	CHECK(diff.GetValue().Hunks.empty());
}

TEST_CASE("Two texts that differ and hold a NUL byte still give hunks")
{
	const std::string oldText("a\0b\n", 4);
	const std::string newText("a\0c\n", 4);

	const Core::Result<Core::FileDiff> diff =
		Core::Repository::DiffTexts(oldText, newText, "f.txt", Core::WhitespaceInDiff::Shown);

	REQUIRE(diff.HasValue());
	CHECK(diff.GetValue().Change == Core::FileChange::Modified);
	CHECK_FALSE(diff.GetValue().Hunks.empty());
}

TEST_CASE("A change of white space alone has hunks unless white space is ignored")
{
	const Core::Result<Core::FileDiff> shown =
		Core::Repository::DiffTexts("a b\n", "a  b\n", "f.txt", Core::WhitespaceInDiff::Shown);
	const Core::Result<Core::FileDiff> ignored =
		Core::Repository::DiffTexts("a b\n", "a  b\n", "f.txt", Core::WhitespaceInDiff::Ignored);

	REQUIRE(shown.HasValue());
	REQUIRE(ignored.HasValue());
	CHECK_FALSE(shown.GetValue().Hunks.empty());
	CHECK(ignored.GetValue().Hunks.empty());
}

TEST_CASE("A range diff ignoring white space keeps a whitespace-only file listed with no hunks")
{
	GitFixture fixture;
	fixture.Write("a.txt", "a b\n");
	fixture.Commit("one");
	fixture.Branch("before");
	fixture.Write("a.txt", "a  b\n");
	fixture.Commit("spaces");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::DiffRequest request;
	request.Target = Core::DiffTarget::RevisionToRevision;
	request.From = repository.ResolveReference("refs/heads/before").GetValue();
	request.To = repository.ResolveReference("refs/heads/master").GetValue();
	request.Whitespace = Core::WhitespaceInDiff::Ignored;

	const Core::Result<Core::Diff> ignored = repository.QueryDiff(request);
	request.Whitespace = Core::WhitespaceInDiff::Shown;
	const Core::Result<Core::Diff> shown = repository.QueryDiff(request);

	REQUIRE(ignored.HasValue());
	REQUIRE(shown.HasValue());
	REQUIRE(ignored.GetValue().Files.size() == 1);
	CHECK(ignored.GetValue().Files.front().Hunks.empty());
	REQUIRE(shown.GetValue().Files.size() == 1);
	CHECK_FALSE(shown.GetValue().Files.front().Hunks.empty());
}
