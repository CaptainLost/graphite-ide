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

	Graphite::GitDecorationView ViewOf(const Graphite::GitDecorations* index)
	{
		Graphite::GitDecorationView view;
		view.Index = index;
		view.SetWorkingDirectory(kWorkingDirectory);
		return view;
	}
}

TEST_CASE("GitDecorationView: a path inside the working directory is looked up by its relative key")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view = ViewOf(&index);

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
	const Graphite::GitDecorationView view = ViewOf(&index);

	CHECK(view.RelativeOf("C:/repo").has_value());
	CHECK(view.FolderOf("C:/repo") == GitDecoration::Modified);
	CHECK(view.FolderOf("C:/repo/") == GitDecoration::Modified);
}

TEST_CASE("GitDecorationView: a path outside the working directory has no key and no decoration")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"x", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view = ViewOf(&index);

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
	const Graphite::GitDecorationView view = ViewOf(&index);

	CHECK(view.FileOf("c:/REPO/Src/A.cpp") == GitDecoration::Modified);
	CHECK(view.FolderOf("C:/Repo/SRC") == GitDecoration::Modified);
}
#endif

TEST_CASE("GitDecorationView: without an index nothing is decorated")
{
	const Graphite::GitDecorationView view = ViewOf(nullptr);

	CHECK(view.FileOf("C:/repo/src/a.cpp") == GitDecoration::None);
	CHECK(view.FolderOf("C:/repo/src") == GitDecoration::None);
	CHECK(view.FolderOf("C:/repo") == GitDecoration::None);
	CHECK_FALSE(view.IsUntrackedFolderOf("C:/repo/src"));
}

TEST_CASE("GitDecorationView: without a working directory nothing is decorated")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	Graphite::GitDecorationView view;
	view.Index = &index;

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
	const Graphite::GitDecorationView view = ViewOf(&index);

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

TEST_CASE("GitDecorationView: a folder lookup gives the decoration and the untracked flag from one key")
{
	Core::FileStatus untracked;
	untracked.Path = u8"build/gen";
	untracked.IsUntracked = true;
	untracked.IsDirectory = true;

	const Graphite::GitDecorations index(StatusOf({untracked, Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView view = ViewOf(&index);

	const Graphite::FolderDecoration gen = view.FolderDecorationOf("C:/repo/build/gen");
	CHECK(gen.Decoration == GitDecoration::Untracked);
	CHECK(gen.Untracked);

	const Graphite::FolderDecoration build = view.FolderDecorationOf("C:/repo/build");
	CHECK(build.Decoration == GitDecoration::Untracked);
	CHECK_FALSE(build.Untracked);

	const Graphite::FolderDecoration src = view.FolderDecorationOf("C:/repo/src");
	CHECK(src.Decoration == GitDecoration::Modified);
	CHECK_FALSE(src.Untracked);

	CHECK(view.FolderDecorationOf("C:/other").Decoration == GitDecoration::None);
	CHECK(ViewOf(nullptr).FolderDecorationOf("C:/repo/src").Decoration == GitDecoration::None);
}

TEST_CASE("GitDecorationView: a copy keeps the working directory")
{
	const Graphite::GitDecorations index(StatusOf({Entry(u8"src/a.cpp", StatusFlags::Modified)}));
	const Graphite::GitDecorationView original = ViewOf(&index);
	const Graphite::GitDecorationView copy = original;

	CHECK(copy.FileOf("C:/repo/src/a.cpp") == GitDecoration::Modified);
}
