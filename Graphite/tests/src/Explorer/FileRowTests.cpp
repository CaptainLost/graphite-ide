#include <doctest/doctest.h>

#include "Explorer/FileRow.h"

namespace {

	using Graphite::GitDecoration;
	using Core::StatusFlags;

	constexpr ImU32 kAdded = 0x11111111u;
	constexpr ImU32 kModified = 0x22222222u;
	constexpr ImU32 kRemoved = 0x33333333u;

	Core::FileStatus Entry(const char8_t* path, StatusFlags workTree)
	{
		Core::FileStatus file;
		file.Path = path;
		file.WorkTree = workTree;
		return file;
	}

	Core::FileStatus UntrackedDirectory(const char8_t* path)
	{
		Core::FileStatus file;
		file.Path = path;
		file.IsUntracked = true;
		file.IsDirectory = true;
		return file;
	}

	Graphite::GitDecorationView ViewOf(const Graphite::GitDecorations& index)
	{
		Graphite::GitDecorationView view;
		view.Index = &index;
		view.SetWorkingDirectory("C:/repo");
		view.Colours[0] = kAdded;
		view.Colours[1] = kModified;
		view.Colours[2] = kRemoved;
		return view;
	}

	Graphite::GitDecorations IndexOf(std::vector<Core::FileStatus> entries)
	{
		Core::WorkingTreeStatus status;
		status.Entries = std::move(entries);
		return Graphite::GitDecorations{status};
	}
}

TEST_CASE("FileRowStatusOf: a changed file gets its letter and its colour")
{
	const auto index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), Entry(u8"src/b.cpp", StatusFlags::New)});
	const auto view = ViewOf(index);

	const Graphite::RowStatus modified = Graphite::FileRowStatusOf(view, "C:/repo/src/a.cpp");
	CHECK(modified.Decoration == GitDecoration::Modified);
	CHECK(modified.Letter == 'M');
	CHECK_FALSE(modified.Dot);
	CHECK(modified.Ink == kModified);
	CHECK(modified.HasMarker());

	const Graphite::RowStatus added = Graphite::FileRowStatusOf(view, "C:/repo/src/b.cpp");
	CHECK(added.Letter == 'A');
	CHECK(added.Ink == kAdded);
}

TEST_CASE("FileRowStatusOf: a clean file, or one outside the repository, has no marker")
{
	const auto index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified)});
	const auto view = ViewOf(index);

	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/repo/src/clean.cpp").HasMarker());
	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/other/src/a.cpp").HasMarker());
	CHECK_FALSE(Graphite::FileRowStatusOf(Graphite::GitDecorationView{}, "C:/repo/src/a.cpp").HasMarker());
}

TEST_CASE("FolderRowStatusOf: a folder with a change beneath it gets a dot in the highest state's colour")
{
	Core::FileStatus conflicted = Entry(u8"src/deep/c.cpp", StatusFlags::Modified);
	conflicted.IsConflicted = true;

	const auto index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), conflicted});
	const auto view = ViewOf(index);

	const Graphite::RowStatus folder = Graphite::FolderRowStatusOf(view, "C:/repo/src");
	CHECK(folder.Decoration == GitDecoration::Conflicted);
	CHECK(folder.Dot);
	CHECK(folder.Letter == '\0');
	CHECK(folder.Ink == kRemoved);

	CHECK(Graphite::FolderRowStatusOf(view, "C:/repo").Dot);
	CHECK_FALSE(Graphite::FolderRowStatusOf(view, "C:/repo/lib").HasMarker());
}

TEST_CASE("FolderRowStatusOf: an untracked directory shows U, the folders above it a dot")
{
	const auto index = IndexOf({UntrackedDirectory(u8"build/gen")});
	const auto view = ViewOf(index);

	const Graphite::RowStatus untracked = Graphite::FolderRowStatusOf(view, "C:/repo/build/gen");
	CHECK(untracked.Letter == 'U');
	CHECK_FALSE(untracked.Dot);
	CHECK(untracked.Ink == kAdded);

	const Graphite::RowStatus parent = Graphite::FolderRowStatusOf(view, "C:/repo/build");
	CHECK(parent.Dot);
	CHECK(parent.Letter == '\0');
	CHECK(parent.Ink == kAdded);

	CHECK(Graphite::FileRowStatusOf(view, "C:/repo/build/gen/a.o").Letter == 'U');
}

TEST_CASE("FileRowStatusOf: an ignored row keeps its own status but not an untracked directory's")
{
	const auto index = IndexOf({UntrackedDirectory(u8"gen"), Entry(u8"out/tracked.cpp", StatusFlags::Modified)});
	const auto view = ViewOf(index);

	const Graphite::RowStatus tracked = Graphite::FileRowStatusOf(view, "C:/repo/out/tracked.cpp", true);
	CHECK(tracked.Decoration == GitDecoration::Modified);
	CHECK(tracked.Letter == 'M');

	CHECK(Graphite::FileRowStatusOf(view, "C:/repo/gen/a.cpp").Letter == 'U');
	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/repo/gen/a.cpp", true).HasMarker());
}
