#include <doctest/doctest.h>

#include "Git/GitDecorations.h"

namespace {

	using Graphite::GitColor;
	using Graphite::GitDecoration;
	using Core::StatusFlags;

	Core::FileStatus Entry(const char8_t* path, StatusFlags index, StatusFlags workTree)
	{
		Core::FileStatus file;
		file.Path = path;
		file.Index = index;
		file.WorkTree = workTree;
		return file;
	}

	Core::FileStatus UntrackedEntry(const char8_t* path, bool directory)
	{
		Core::FileStatus file;
		file.Path = path;
		file.IsUntracked = true;
		file.IsDirectory = directory;
		return file;
	}

	Core::WorkingTreeStatus StatusOf(std::vector<Core::FileStatus> entries)
	{
		Core::WorkingTreeStatus status;
		status.Entries = std::move(entries);
		return status;
	}
}

TEST_CASE("DecorationOf: one value per entry, by priority")
{
	Core::FileStatus conflicted = Entry(u8"a", StatusFlags::None, StatusFlags::Modified);
	conflicted.IsConflicted = true;

	CHECK(Graphite::DecorationOf(conflicted) == GitDecoration::Conflicted);
	CHECK(Graphite::DecorationOf(Entry(u8"a", StatusFlags::New, StatusFlags::Modified)) == GitDecoration::Modified);
	CHECK(Graphite::DecorationOf(Entry(u8"a", StatusFlags::Renamed, StatusFlags::None)) == GitDecoration::Renamed);
	CHECK(Graphite::DecorationOf(Entry(u8"a", StatusFlags::New, StatusFlags::None)) == GitDecoration::Added);
	CHECK(Graphite::DecorationOf(UntrackedEntry(u8"a", false)) == GitDecoration::Untracked);
	CHECK(Graphite::DecorationOf(Entry(u8"a", StatusFlags::None, StatusFlags::TypeChanged)) == GitDecoration::Modified);
	CHECK(Graphite::DecorationOf(Entry(u8"a", StatusFlags::Modified, StatusFlags::None)) == GitDecoration::Modified);
}

TEST_CASE("DecorationOf: a deleted file has no row decoration but still weighs on its folders")
{
	const Core::FileStatus deletedInWorkTree = Entry(u8"a", StatusFlags::None, StatusFlags::Deleted);
	const Core::FileStatus deletedInIndex = Entry(u8"a", StatusFlags::Deleted, StatusFlags::None);

	CHECK(Graphite::DecorationOf(deletedInWorkTree) == GitDecoration::None);
	CHECK(Graphite::FolderWeightOf(deletedInWorkTree) == GitDecoration::Modified);
	CHECK(Graphite::DecorationOf(deletedInIndex) == GitDecoration::None);
	CHECK(Graphite::FolderWeightOf(deletedInIndex) == GitDecoration::Modified);
	CHECK(Graphite::FolderWeightOf(Entry(u8"a", StatusFlags::New, StatusFlags::None)) == GitDecoration::Added);
	CHECK(Graphite::FolderWeightOf(Entry(u8"a", StatusFlags::None, StatusFlags::None)) == GitDecoration::None);
}

TEST_CASE("LetterOf and ColourOf cover every decoration")
{
	CHECK(Graphite::LetterOf(GitDecoration::None) == '\0');
	CHECK(Graphite::LetterOf(GitDecoration::Untracked) == 'U');
	CHECK(Graphite::LetterOf(GitDecoration::Added) == 'A');
	CHECK(Graphite::LetterOf(GitDecoration::Renamed) == 'R');
	CHECK(Graphite::LetterOf(GitDecoration::Modified) == 'M');
	CHECK(Graphite::LetterOf(GitDecoration::Conflicted) == '!');

	CHECK(Graphite::ColourOf(GitDecoration::Untracked) == GitColor::GutterAdded);
	CHECK(Graphite::ColourOf(GitDecoration::Added) == GitColor::GutterAdded);
	CHECK(Graphite::ColourOf(GitDecoration::Renamed) == GitColor::GutterModified);
	CHECK(Graphite::ColourOf(GitDecoration::Modified) == GitColor::GutterModified);
	CHECK(Graphite::ColourOf(GitDecoration::Conflicted) == GitColor::GutterRemoved);
	CHECK(Graphite::ColourOf(GitDecoration::None) == GitColor::GutterModified);
}

TEST_CASE("GitDecorations: files get their own status and folders the highest beneath them")
{
	const Graphite::GitDecorations decorations(StatusOf({
		Entry(u8"src/a.cpp", StatusFlags::None, StatusFlags::Modified),
		Entry(u8"src/deep/x/y.h", StatusFlags::New, StatusFlags::None),
		Entry(u8"docs/old.md", StatusFlags::None, StatusFlags::Deleted),
	}));

	CHECK_FALSE(decorations.IsEmpty());
	CHECK(decorations.File("src/a.cpp") == GitDecoration::Modified);
	CHECK(decorations.File("src/deep/x/y.h") == GitDecoration::Added);
	CHECK(decorations.File("docs/old.md") == GitDecoration::None);
	CHECK(decorations.Folder("src") == GitDecoration::Modified);
	CHECK(decorations.Folder("src/deep") == GitDecoration::Added);
	CHECK(decorations.Folder("src/deep/x") == GitDecoration::Added);
	CHECK(decorations.Folder("docs") == GitDecoration::Modified);
	CHECK(decorations.Folder("") == GitDecoration::Modified);
	CHECK(decorations.File("src/b.cpp") == GitDecoration::None);
	CHECK(decorations.Folder("lib") == GitDecoration::None);
}

TEST_CASE("GitDecorations: a file at the top reaches the working directory under every spelling of it")
{
	const Graphite::GitDecorations decorations(
		StatusOf({Entry(u8"README.md", StatusFlags::None, StatusFlags::Modified)}));

	CHECK(decorations.File("README.md") == GitDecoration::Modified);
	CHECK(decorations.Folder("") == GitDecoration::Modified);
	CHECK(decorations.Folder(".") == GitDecoration::Modified);
}

TEST_CASE("GitDecorations: a folder spelled with a trailing separator is the same folder")
{
	const Graphite::GitDecorations decorations(
		StatusOf({Entry(u8"src/a.cpp", StatusFlags::None, StatusFlags::Modified)}));

	CHECK(decorations.Folder("src/") == GitDecoration::Modified);
	CHECK(decorations.Folder("src\\") == GitDecoration::Modified);
}

TEST_CASE("GitDecorations: the folders above an untracked directory aggregate it but are not untracked")
{
	const Graphite::GitDecorations decorations(StatusOf({UntrackedEntry(u8"build/gen", true)}));

	CHECK(decorations.Folder("build") == GitDecoration::Untracked);
	CHECK(decorations.Folder("") == GitDecoration::Untracked);
	CHECK_FALSE(decorations.IsUntrackedFolder("build"));
	CHECK_FALSE(decorations.IsUntrackedFolder(""));
}

TEST_CASE("GitDecorations: an untracked directory marks everything beneath it")
{
	const Graphite::GitDecorations decorations(StatusOf({UntrackedEntry(u8"build/gen", true)}));

	CHECK(decorations.IsUntrackedFolder("build/gen"));
	CHECK(decorations.IsUntrackedFolder("build/gen/out"));
	CHECK_FALSE(decorations.IsUntrackedFolder("build"));
	CHECK(decorations.File("build/gen/out/a.o") == GitDecoration::Untracked);
	CHECK(decorations.Folder("build/gen/out") == GitDecoration::Untracked);
	CHECK(decorations.Folder("build") == GitDecoration::Untracked);
	CHECK(decorations.Folder("build/gen") == GitDecoration::Untracked);
	CHECK(decorations.File("build/genx.txt") == GitDecoration::None);
	CHECK_FALSE(decorations.IsUntrackedFolder("build/genx"));
}

TEST_CASE("GitDecorations: non-ASCII names are found by the same path")
{
	const Graphite::GitDecorations decorations(
		StatusOf({Entry(u8"źródła/żółw.cpp", StatusFlags::None, StatusFlags::Modified)}));

	CHECK(decorations.File(u8"źródła/żółw.cpp") == GitDecoration::Modified);
	CHECK(decorations.Folder(u8"źródła") == GitDecoration::Modified);
}

#ifdef _WIN32
TEST_CASE("GitDecorations: Windows paths compare without regard to case")
{
	const Graphite::GitDecorations decorations(StatusOf({
		Entry(u8"src/a.cpp", StatusFlags::None, StatusFlags::Modified),
		Entry(u8"źródła/żółw.cpp", StatusFlags::None, StatusFlags::Modified),
	}));

	CHECK(decorations.File("SRC/A.CPP") == GitDecoration::Modified);
	CHECK(decorations.Folder("Src") == GitDecoration::Modified);
	CHECK(decorations.File(u8"ŹRÓDŁA/ŻÓŁW.CPP") == GitDecoration::Modified);
}
#endif

TEST_CASE("GitDecorations: either separator finds the same entry")
{
	const Graphite::GitDecorations decorations(
		StatusOf({Entry(u8"src/deep/a.cpp", StatusFlags::None, StatusFlags::Modified)}));

	CHECK(decorations.File("src/deep/a.cpp") == GitDecoration::Modified);
	CHECK(decorations.File("src\\deep\\a.cpp") == GitDecoration::Modified);
	CHECK(decorations.Folder("src\\deep") == GitDecoration::Modified);
}

TEST_CASE("GitDecorations: an empty status decorates nothing")
{
	const Graphite::GitDecorations decorations{Core::WorkingTreeStatus{}};

	CHECK(decorations.IsEmpty());
	CHECK(decorations.File("a.cpp") == GitDecoration::None);
	CHECK(decorations.Folder("") == GitDecoration::None);
	CHECK(decorations.Folder("src") == GitDecoration::None);
	CHECK_FALSE(decorations.IsUntrackedFolder("src"));
	CHECK(Graphite::GitDecorations{}.IsEmpty());
}

TEST_CASE("GitDecorations: the folder a file was renamed out of is decorated too")
{
	Core::FileStatus renamed = Entry(u8"b/x.cpp", StatusFlags::Renamed, StatusFlags::None);
	renamed.RenamedFrom = std::filesystem::path(u8"a/x.cpp");
	const Graphite::GitDecorations decorations(StatusOf({renamed}));

	CHECK(decorations.Folder("a") == GitDecoration::Renamed);
	CHECK(decorations.Folder("b") == GitDecoration::Renamed);
	CHECK(decorations.File("a/x.cpp") == GitDecoration::None);
	CHECK(decorations.File("b/x.cpp") == GitDecoration::Renamed);
}

TEST_CASE("GitDecorations: the renamed-from path raises every ancestor up to the root")
{
	Core::FileStatus renamed = Entry(u8"new/x.cpp", StatusFlags::Renamed, StatusFlags::None);
	renamed.RenamedFrom = std::filesystem::path(u8"old/deep/x.cpp");
	const Graphite::GitDecorations decorations(StatusOf({renamed}));

	CHECK(decorations.Folder("old/deep") == GitDecoration::Renamed);
	CHECK(decorations.Folder("old") == GitDecoration::Renamed);
	CHECK(decorations.Folder("") == GitDecoration::Renamed);
}

TEST_CASE("GitDecorations: OwnFile ignores what a file inherits from an untracked directory")
{
	const Graphite::GitDecorations decorations(StatusOf({
		UntrackedEntry(u8"gen", true),
		Entry(u8"lib/kept.cpp", StatusFlags::None, StatusFlags::Modified),
	}));

	CHECK(decorations.File("gen/a.cpp") == GitDecoration::Untracked);
	CHECK(decorations.OwnFile("gen/a.cpp") == GitDecoration::None);
	CHECK(decorations.OwnFile("lib/kept.cpp") == GitDecoration::Modified);
	CHECK(decorations.OwnFile("lib/other.cpp") == GitDecoration::None);
	CHECK(Graphite::GitDecorations{}.OwnFile("a.cpp") == GitDecoration::None);
}
