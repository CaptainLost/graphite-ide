#include <doctest/doctest.h>

#include "Git/ChangesRows.h"

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

TEST_CASE("An untracked file is unstaged and nothing else")
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

	// A rename that also changed the file is a rename first: that is the fact a reader
	// cannot work out from the path alone.
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Renamed | Core::StatusFlags::Modified, false) == 'R');
}
