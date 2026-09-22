#include <doctest/doctest.h>

#include "Git/ChangesRows.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace {

	Core::FileStatus Entry(const char* path, Core::StatusFlags index, Core::StatusFlags workTree)
	{
		Core::FileStatus file;
		file.Path = path;
		file.Index = index;
		file.WorkTree = workTree;
		return file;
	}
}

TEST_CASE("A file staged and edited again appears in both sections")
{
	Core::WorkingTreeStatus status;
	status.Entries.push_back(
		Entry("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified));

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Staged == std::vector<std::size_t>{0});
	CHECK(sections.Unstaged == std::vector<std::size_t>{0});
	CHECK(sections.Conflicted.empty());
}

TEST_CASE("A conflicted file is only conflicted")
{
	Core::WorkingTreeStatus status;
	Core::FileStatus file = Entry("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified);
	file.IsConflicted = true;
	status.Entries.push_back(file);

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Conflicted == std::vector<std::size_t>{0});
	CHECK(sections.Staged.empty());
	CHECK(sections.Unstaged.empty());
}

TEST_CASE("An entry changed only in the working tree is unstaged and nothing else")
{
	Core::WorkingTreeStatus status;
	Core::FileStatus file = Entry("new.txt", Core::StatusFlags::None, Core::StatusFlags::New);
	file.IsUntracked = true;
	status.Entries.push_back(file);

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Unstaged == std::vector<std::size_t>{0});
	CHECK(sections.Staged.empty());
}

TEST_CASE("A clean entry lands in no section at all")
{
	Core::WorkingTreeStatus status;
	status.Entries.push_back(Entry("a.txt", Core::StatusFlags::None, Core::StatusFlags::None));

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Conflicted.empty());
	CHECK(sections.Staged.empty());
	CHECK(sections.Unstaged.empty());
}

TEST_CASE("The change letter names the most consequential flag")
{
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::New, true) == '?');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::New, false) == 'A');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Deleted, false) == 'D');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Renamed, false) == 'R');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Modified, false) == 'M');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::TypeChanged, false) == 'T');

	// libgit2's status.c (index_delta2status and workdir_delta2status) sets each delta
	// status to exactly one bit, except GIT_DELTA_RENAMED which may also set MODIFIED
	// when the old and new blob OIDs differ. diff_tform.c's rename detection rewrites
	// typechange deltas matched to renames as purely RENAMED before status conversion.
	// Thus Renamed+Modified is the only multi-bit combination ever produced. See:
	// vendor/libgit2/src/libgit2/status.c (~lines 25-118) and diff_tform.c (~lines 980-1075).
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Renamed | Core::StatusFlags::Modified, false) == 'R');
}

TEST_CASE("A selection that is still there stays where it is")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"c.txt", "a.txt", "b.txt"};

	CHECK(Graphite::NextSelection("b.txt", before, after)
		  == std::optional<std::filesystem::path>("b.txt"));
}

TEST_CASE("A selection that left is replaced by the row that took its place")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"a.txt", "c.txt"};

	CHECK(Graphite::NextSelection("b.txt", before, after)
		  == std::optional<std::filesystem::path>("c.txt"));
}

TEST_CASE("A selection on the last row falls back to the new last row")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"a.txt", "b.txt"};

	CHECK(Graphite::NextSelection("c.txt", before, after)
		  == std::optional<std::filesystem::path>("b.txt"));
}

TEST_CASE("A selection in a section that emptied is dropped")
{
	const std::vector<std::filesystem::path> before{"a.txt"};
	const std::vector<std::filesystem::path> after{};

	CHECK(Graphite::NextSelection("a.txt", before, after) == std::nullopt);
}

TEST_CASE("A selection that was never in the list is dropped")
{
	const std::vector<std::filesystem::path> before{"a.txt"};
	const std::vector<std::filesystem::path> after{"a.txt"};

	CHECK(Graphite::NextSelection("gone.txt", before, after) == std::nullopt);
}
