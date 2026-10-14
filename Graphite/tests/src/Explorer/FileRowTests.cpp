#include <doctest/doctest.h>

#include "Explorer/FileRow.h"
#include "ImGuiFrames.h"

#include "TempDirectory.h"

#include "Core/Text/DocumentSet.h"

#include <filesystem>
#include <optional>
#include <string>

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

TEST_CASE("MarkerTooltipOf: a letter names its state, a dot what lies beneath")
{
	const auto index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), UntrackedDirectory(u8"build/gen")});
	const auto view = ViewOf(index);

	CHECK(std::string{Graphite::MarkerTooltipOf(Graphite::FileRowStatusOf(view, "C:/repo/src/a.cpp"))} == "Modified");
	CHECK(std::string{Graphite::MarkerTooltipOf(Graphite::FolderRowStatusOf(view, "C:/repo/src"))} ==
		  "Contains modified or deleted files");
	CHECK(std::string{Graphite::MarkerTooltipOf(Graphite::FolderRowStatusOf(view, "C:/repo/build/gen"))} ==
		  "Untracked");
	CHECK(std::string{Graphite::MarkerTooltipOf(Graphite::FolderRowStatusOf(view, "C:/repo/build"))} ==
		  "Contains untracked files");

	CHECK(Graphite::MarkerTooltipOf(Graphite::FileRowStatusOf(view, "C:/repo/src/b.cpp")) == nullptr);
}

TEST_CASE("A file row with a name far longer than the window does not widen the window's content")
{
	ImGuiFrames frames;

	const float windowWidth = 200.0f;
	const std::string longLabel(300, 'x');

	float rowWidth = 0.0f;
	float availableWidth = 0.0f;
	float scrollableWidth = -1.0f;

	const auto drawFileRow = [&]
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(windowWidth, 300.0f));
		ImGui::Begin("##fileRowWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		availableWidth = ImGui::GetContentRegionAvail().x;

		const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();
		rowWidth = layout.Width;

		ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(layout.Width, 0.0f));
		Graphite::DrawFileRowLabel(layout, Graphite::FileRowLook{.Ink = 0xFFFFFFFFu}, longLabel);

		scrollableWidth = ImGui::GetCurrentWindow()->ScrollMax.x;
		ImGui::End();
	};

	frames.Next(drawFileRow);
	frames.Next(drawFileRow);

	CHECK(rowWidth == doctest::Approx(availableWidth));
	CHECK(scrollableWidth == doctest::Approx(0.0f));
}

TEST_CASE("MarkerTooltipOf covers every decoration, as a letter and as a dot")
{
	const struct
	{
		GitDecoration Decoration;
		const char* Letter;
		const char* Dot;
	} cases[] = {
		{GitDecoration::Untracked, "Untracked", "Contains untracked files"},
		{GitDecoration::Added, "Added", "Contains added files"},
		{GitDecoration::Renamed, "Renamed", "Contains renamed files"},
		{GitDecoration::Modified, "Modified", "Contains modified or deleted files"},
		{GitDecoration::Conflicted, "Conflicted", "Contains files with conflicts"},
	};

	for (const auto& expected : cases)
	{
		CAPTURE(expected.Letter);

		Graphite::RowStatus status;
		status.Decoration = expected.Decoration;
		status.Letter = Graphite::LetterOf(expected.Decoration);
		CHECK(std::string{Graphite::MarkerTooltipOf(status)} == expected.Letter);

		status.Letter = '\0';
		status.Dot = true;
		CHECK(std::string{Graphite::MarkerTooltipOf(status)} == expected.Dot);
	}
}

TEST_CASE("OpenTabOf: a file in the preview tab reads as a preview until the tab is made permanent")
{
	TempDirectory folder;
	const std::filesystem::path previewed = folder.MakeFile("Previewed.cpp", "int previewed;");
	const std::filesystem::path permanent = folder.MakeFile("Permanent.cpp", "int permanent;");
	const std::filesystem::path closed = folder.MakeFile("Closed.cpp", "int closed;");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(permanent, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(previewed, Core::TabKind::Preview) == Core::OpenOutcome::Opened);

	CHECK(Graphite::OpenTabOf(documents, previewed) == Graphite::OpenTab::Preview);
	CHECK(Graphite::OpenTabOf(documents, permanent) == Graphite::OpenTab::Permanent);
	CHECK(Graphite::OpenTabOf(documents, closed) == Graphite::OpenTab::None);

	const std::optional<std::size_t> previewIndex = documents.Find(previewed);
	REQUIRE(previewIndex.has_value());
	documents.Promote(*previewIndex);

	CHECK(Graphite::OpenTabOf(documents, previewed) == Graphite::OpenTab::Permanent);
}

TEST_CASE("OpenTabOf: previewing another file moves the preview mark to it")
{
	TempDirectory folder;
	const std::filesystem::path first = folder.MakeFile("First.cpp", "int first;");
	const std::filesystem::path second = folder.MakeFile("Second.cpp", "int second;");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(first, Core::TabKind::Preview) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(second, Core::TabKind::Preview) == Core::OpenOutcome::Opened);

	CHECK(Graphite::OpenTabOf(documents, first) == Graphite::OpenTab::None);
	CHECK(Graphite::OpenTabOf(documents, second) == Graphite::OpenTab::Preview);
}

TEST_CASE("OpenTabTooltipOf: a preview tab says it is one, a permanent tab that the file is open")
{
	CHECK(std::string{Graphite::OpenTabTooltipOf(Graphite::OpenTab::Permanent)} == "Open in an editor tab");
	CHECK(std::string{Graphite::OpenTabTooltipOf(Graphite::OpenTab::Preview)} == "Open in a preview tab");
	CHECK(Graphite::OpenTabTooltipOf(Graphite::OpenTab::None) == nullptr);
}
