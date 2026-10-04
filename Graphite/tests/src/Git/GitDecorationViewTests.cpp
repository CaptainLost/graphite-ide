#include <doctest/doctest.h>

#include "Git/GitDecorationView.h"

namespace {

	using Graphite::GitDecoration;
	using Core::StatusFlags;

	Core::FileStatus Entry(const char8_t* path, StatusFlags workTree)
	{
		Core::FileStatus file;
		file.Path = path;
		file.WorkTree = workTree;
		return file;
	}

	Core::WorkingTreeStatus StatusOf(std::vector<Core::FileStatus> entries)
	{
		Core::WorkingTreeStatus status;
		status.Entries = std::move(entries);
		return status;
	}

	const std::filesystem::path kWorkingDirectory = "C:/repo";
}

TEST_CASE("GitDecorationView: a path inside the working directory is looked up by its relative key")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view{.Index = &index, .WorkingDirectory = kWorkingDirectory};

	const auto relative = view.RelativeOf("C:/repo/src/a.cpp");
	REQUIRE(relative.has_value());
	CHECK(relative->generic_u8string() == u8"src/a.cpp");

	CHECK(view.FileOf("C:/repo/src/a.cpp") == GitDecoration::Modified);
	CHECK(view.FileOf("C:\\repo\\src\\a.cpp") == GitDecoration::Modified);
	CHECK(view.FolderOf("C:/repo/src") == GitDecoration::Modified);
	CHECK(view.FileOf("C:/repo/src/b.cpp") == GitDecoration::None);
}

TEST_CASE("GitDecorationView: the working directory itself is the root aggregate")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view{.Index = &index, .WorkingDirectory = kWorkingDirectory};

	CHECK(view.RelativeOf("C:/repo").has_value());
	CHECK(view.FolderOf("C:/repo") == GitDecoration::Modified);
	CHECK(view.FolderOf("C:/repo/") == GitDecoration::Modified);
}

TEST_CASE("GitDecorationView: a path outside the working directory has no key and no decoration")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"x", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view{.Index = &index, .WorkingDirectory = kWorkingDirectory};

	CHECK_FALSE(view.RelativeOf("C:/other/x").has_value());
	CHECK(view.FileOf("C:/other/x") == GitDecoration::None);
	CHECK(view.FolderOf("C:/other") == GitDecoration::None);

	// A sibling whose name starts with the working directory's is not inside it.
	CHECK_FALSE(view.RelativeOf("C:/repo2/x").has_value());
	CHECK(view.FileOf("C:/repo2/x") == GitDecoration::None);
}

#ifdef _WIN32
TEST_CASE("GitDecorationView: Windows paths are found whatever their case")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view{.Index = &index, .WorkingDirectory = kWorkingDirectory};

	CHECK(view.FileOf("c:/REPO/Src/A.cpp") == GitDecoration::Modified);
	CHECK(view.FolderOf("C:/Repo/SRC") == GitDecoration::Modified);
}
#endif

TEST_CASE("GitDecorationView: without an index nothing is decorated")
{
	const Graphite::GitDecorationView view{.WorkingDirectory = kWorkingDirectory};

	CHECK(view.FileOf("C:/repo/src/a.cpp") == GitDecoration::None);
	CHECK(view.FolderOf("C:/repo/src") == GitDecoration::None);
	CHECK(view.FolderOf("C:/repo") == GitDecoration::None);
	CHECK_FALSE(view.IsUntrackedFolderOf("C:/repo/src"));
}

TEST_CASE("GitDecorationView: without a working directory nothing is decorated")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view{.Index = &index};

	CHECK_FALSE(view.RelativeOf("C:/repo/src/a.cpp").has_value());
	CHECK(view.FileOf("src/a.cpp") == GitDecoration::None);
}

TEST_CASE("GitDecorationView: an untracked directory is reported by its absolute path")
{
	Core::FileStatus untracked;
	untracked.Path = u8"build/gen";
	untracked.IsUntracked = true;
	untracked.IsDirectory = true;

	const Graphite::GitDecorations index(StatusOf({untracked}));
	const Graphite::GitDecorationView view{.Index = &index, .WorkingDirectory = kWorkingDirectory};

	CHECK(view.IsUntrackedFolderOf("C:/repo/build/gen"));
	CHECK_FALSE(view.IsUntrackedFolderOf("C:/repo/build"));
	CHECK(view.FolderOf("C:/repo/build") == GitDecoration::Untracked);
}

TEST_CASE("GitDecorationView: each decoration's colour comes from its gutter slot")
{
	Graphite::GitDecorationView view;
	view.Colours[0] = 0x11111111u;
	view.Colours[1] = 0x22222222u;
	view.Colours[2] = 0x33333333u;

	CHECK(view.ColourFor(GitDecoration::Untracked) == 0x11111111u);
	CHECK(view.ColourFor(GitDecoration::Added) == 0x11111111u);
	CHECK(view.ColourFor(GitDecoration::Renamed) == 0x22222222u);
	CHECK(view.ColourFor(GitDecoration::Modified) == 0x22222222u);
	CHECK(view.ColourFor(GitDecoration::Conflicted) == 0x33333333u);
}
