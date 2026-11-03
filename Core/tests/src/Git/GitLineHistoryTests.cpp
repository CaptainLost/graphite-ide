#include <doctest/doctest.h>

#include "Core/Git/Blame.h"
#include "Core/Git/LineHistory.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using Core::DiffLineKind;
using Core::LineHistory;
using Core::LineHistoryRequest;
using Core::Oid;
using Core::Repository;
using Core::Result;

namespace {

	Oid OidOf(const std::string& hex)
	{
		return Oid::FromHex(hex).value();
	}

	LineHistory HistoryOf(const Repository& repository, const LineHistoryRequest& request)
	{
		const Result<LineHistory> history = repository.QueryLineHistory(request);
		REQUIRE(history.HasValue());
		return history.GetValue();
	}

	std::vector<Oid> CommitsOf(const LineHistory& history)
	{
		std::vector<Oid> commits;

		for (const auto& change : history.Changes)
		{
			commits.push_back(change.Commit.Id);
		}

		return commits;
	}

	// The request the editor makes for a line of HEAD: from the commit blame names for it, at the path and line it
	// had there.
	LineHistoryRequest RequestFromBlame(const Repository& repository, const std::filesystem::path& file,
										std::size_t line)
	{
		const Result<Core::Blame> blame = repository.QueryBlame(Core::BlameRequest{file, HeadCommitId(repository)});
		REQUIRE(blame.HasValue());

		const Core::BlameHunk* const hunk = Core::HunkOfLine(blame.GetValue(), line);
		REQUIRE(hunk != nullptr);

		return LineHistoryRequest{.From = hunk->Commit,
								  .File = hunk->OriginalPath,
								  .Line = hunk->OriginalFirstLine + (line - hunk->FirstLine)};
	}
}

TEST_CASE("Line history lists the commits that changed the line newest first")
{
	GitFixture fixture;

	fixture.Write("a.txt", "x\none\ny\n");
	const Oid c1 = OidOf(fixture.Commit("c1"));

	fixture.Write("a.txt", "x\ntwo\ny\n");
	const Oid c2 = OidOf(fixture.Commit("c2"));

	fixture.Write("b.txt", "unrelated\n");
	OidOf(fixture.Commit("c3"));

	fixture.Write("a.txt", "x\nthree\ny\n");
	const Oid c4 = OidOf(fixture.Commit("c4"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history = HistoryOf(repository, LineHistoryRequest{.From = c4, .File = "a.txt", .Line = 2});

	CHECK(CommitsOf(history) == std::vector<Oid>{c4, c2, c1});
	CHECK(history.Complete);
	REQUIRE(history.Changes.size() == 3);
	CHECK(history.Changes[0].ParentLine == 2);
	CHECK_FALSE(history.Changes[2].ParentLine.has_value());
}

TEST_CASE("Line history ends at the commit that inserted the line in the middle")
{
	GitFixture fixture;

	fixture.Write("a.txt", "a\nb\n");
	OidOf(fixture.Commit("c1"));

	fixture.Write("a.txt", "a\nnew\nb\n");
	const Oid c2 = OidOf(fixture.Commit("c2"));

	fixture.Write("a.txt", "a\nNEW\nb\n");
	const Oid c3 = OidOf(fixture.Commit("c3"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history = HistoryOf(repository, LineHistoryRequest{.From = c3, .File = "a.txt", .Line = 2});

	CHECK(CommitsOf(history) == std::vector<Oid>{c3, c2});
	CHECK(history.Complete);
	REQUIRE(history.Changes.size() == 2);
	CHECK_FALSE(history.Changes[1].ParentLine.has_value());
}

TEST_CASE("Line history pairs a line that replaced a block by position")
{
	GitFixture fixture;

	fixture.Write("a.txt", "a\nb\nc\nd\n");
	const Oid c1 = OidOf(fixture.Commit("c1"));

	fixture.Write("a.txt", "a\nX\nY\nZ\nd\n");
	const Oid c2 = OidOf(fixture.Commit("c2"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const LineHistory paired = HistoryOf(repository, LineHistoryRequest{.From = c2, .File = "a.txt", .Line = 3});
	CHECK(CommitsOf(paired) == std::vector<Oid>{c2, c1});
	REQUIRE(paired.Changes.size() == 2);
	CHECK(paired.Changes[0].ParentLine == 3);

	const LineHistory extra = HistoryOf(repository, LineHistoryRequest{.From = c2, .File = "a.txt", .Line = 4});
	CHECK(CommitsOf(extra) == std::vector<Oid>{c2});
}

TEST_CASE("Line history follows a rename")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	const Oid c1 = OidOf(fixture.Commit("c1"));

	fixture.Rename("old.txt", "new.txt");
	OidOf(fixture.Commit("c2"));

	fixture.Write("new.txt", "one\ntwo\nthree\nFOUR\n");
	OidOf(fixture.Commit("c3"));

	fixture.Write("new.txt", "ONE\ntwo\nthree\nFOUR\n");
	const Oid c4 = OidOf(fixture.Commit("c4"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history = HistoryOf(repository, LineHistoryRequest{.From = c4, .File = "new.txt", .Line = 1});

	CHECK(CommitsOf(history) == std::vector<Oid>{c4, c1});
	CHECK(history.Complete);
	REQUIRE(history.Changes.size() == 2);
	CHECK(history.Changes[0].Path == "new.txt");
	CHECK(history.Changes[1].Path == "old.txt");
}

TEST_CASE("Line history follows the first parent of a merge")
{
	GitFixture fixture;

	fixture.Write("a.txt", "x\none\ny\n");
	const Oid c0 = OidOf(fixture.Commit("c0"));

	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("a.txt", "x\nside\ny\n");
	OidOf(fixture.Commit("s1"));

	fixture.Checkout("master");
	fixture.Write("a.txt", "x\nside\ny\n");
	const Oid merge = OidOf(fixture.MergeCommit("merge side", "side"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history = HistoryOf(repository, LineHistoryRequest{.From = merge, .File = "a.txt", .Line = 2});

	CHECK(CommitsOf(history) == std::vector<Oid>{merge, c0});
	CHECK(history.Complete);
}

TEST_CASE("Line history from the blamed commit equals the walk from HEAD on a linear history")
{
	GitFixture fixture;

	fixture.Write("a.txt", "x\none\ny\nz\n");
	const Oid c1 = OidOf(fixture.Commit("c1"));

	fixture.Write("a.txt", "x\ntwo\ny\nz\n");
	const Oid c2 = OidOf(fixture.Commit("c2"));

	fixture.Write("a.txt", "x\ntwo\ny\nZ\n");
	OidOf(fixture.Commit("c3"));

	fixture.Write("b.txt", "unrelated\n");
	OidOf(fixture.Commit("c4"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const LineHistoryRequest fromBlamed = RequestFromBlame(repository, "a.txt", 2);
	REQUIRE(fromBlamed.From == c2);

	const LineHistory blamed = HistoryOf(repository, fromBlamed);
	const LineHistory walked =
		HistoryOf(repository, LineHistoryRequest{.From = HeadCommitId(repository), .File = "a.txt", .Line = 2});

	CHECK(CommitsOf(blamed) == std::vector<Oid>{c2, c1});
	CHECK(CommitsOf(blamed) == CommitsOf(walked));
	CHECK(blamed.Complete == walked.Complete);
}

TEST_CASE("Line history of a line from a merged branch starts at the side commit blame names")
{
	GitFixture fixture;

	fixture.Write("a.txt", "x\none\ny\n");
	const Oid c0 = OidOf(fixture.Commit("c0"));

	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("a.txt", "x\nside\ny\n");
	const Oid s1 = OidOf(fixture.Commit("s1"));

	fixture.Checkout("master");
	fixture.Write("b.txt", "main\n");
	OidOf(fixture.Commit("m1"));

	fixture.Write("a.txt", "x\nside\ny\n");
	const Oid merge = OidOf(fixture.MergeCommit("merge side", "side"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(HeadCommitId(repository) == merge);

	// Blame follows every parent, so it names the commit that wrote the line on its branch rather than the merge;
	// the history then walks that commit's first parents.
	const LineHistoryRequest request = RequestFromBlame(repository, "a.txt", 2);
	CHECK(request.From == s1);

	const LineHistory history = HistoryOf(repository, request);

	REQUIRE_FALSE(history.Changes.empty());
	CHECK(history.Changes.front().Commit.Id == s1);
	CHECK(CommitsOf(history) == std::vector<Oid>{s1, c0});
	CHECK(history.Complete);
}

TEST_CASE("Line history stops at the walk bound and says so")
{
	GitFixture fixture;

	Oid last;
	const std::vector<std::string> words = {"one", "two", "three", "four", "five"};

	for (const std::string& word : words)
	{
		fixture.Write("a.txt", "x\n" + word + "\ny\n");
		last = OidOf(fixture.Commit(word));
	}

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history =
		HistoryOf(repository, LineHistoryRequest{.From = last, .File = "a.txt", .Line = 2, .MaxWalk = 2});

	CHECK(history.Changes.size() == 2);
	CHECK_FALSE(history.Complete);
}

TEST_CASE("Line history returns the hunk that contains the line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\n");
	OidOf(fixture.Commit("c1"));

	fixture.Write("a.txt", "A\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nL\n");
	const Oid c2 = OidOf(fixture.Commit("c2"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const LineHistory history = HistoryOf(repository, LineHistoryRequest{.From = c2, .File = "a.txt", .Line = 12});

	REQUIRE_FALSE(history.Changes.empty());
	REQUIRE(history.Changes[0].Hunks.size() == 1);

	bool found = false;

	for (const auto& line : history.Changes[0].Hunks[0].Lines)
	{
		found = found || (line.Kind == DiffLineKind::Added
						  && static_cast<std::size_t>(line.NewLine) == history.Changes[0].Line);
	}

	CHECK(found);
}
