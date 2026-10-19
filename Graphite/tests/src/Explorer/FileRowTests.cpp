#include <doctest/doctest.h>

#include "Explorer/FileRow.h"
#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "TempDirectory.h"

#include "UI/FileIcons.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/UiScale.h"
#include "Core/Text/DocumentSet.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	using Core::StatusFlags;
	using Graphite::GitDecoration;

	constexpr ImU32 kAdded = 0x11111111u;
	constexpr ImU32 kModified = 0x22222222u;
	constexpr ImU32 kRemoved = 0x33333333u;
	constexpr ImU32 kNameInk = 0xFF2299EEu;
	constexpr bool kOwnEntryOnly = true;
	constexpr bool kDarkScheme = false;
	constexpr bool kNoMarker = false;
	constexpr float kOnePixel = 1.0f;

	struct TooltipCase
	{
		GitDecoration Decoration;
		const char* Letter;
		const char* Dot;
	};

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
	const Graphite::GitDecorations index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), Entry(u8"src/b.cpp", StatusFlags::New)});
	const Graphite::GitDecorationView view = ViewOf(index);

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
	const Graphite::GitDecorations index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified)});
	const Graphite::GitDecorationView view = ViewOf(index);

	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/repo/src/clean.cpp").HasMarker());
	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/other/src/a.cpp").HasMarker());
	CHECK_FALSE(Graphite::FileRowStatusOf(Graphite::GitDecorationView{}, "C:/repo/src/a.cpp").HasMarker());
}

TEST_CASE("FolderRowStatusOf: a folder with a change beneath it gets a dot in the highest state's colour")
{
	Core::FileStatus conflicted = Entry(u8"src/deep/c.cpp", StatusFlags::Modified);
	conflicted.IsConflicted = true;

	const Graphite::GitDecorations index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), conflicted});
	const Graphite::GitDecorationView view = ViewOf(index);

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
	const Graphite::GitDecorations index = IndexOf({UntrackedDirectory(u8"build/gen")});
	const Graphite::GitDecorationView view = ViewOf(index);

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
	const Graphite::GitDecorations index = IndexOf({UntrackedDirectory(u8"gen"), Entry(u8"out/tracked.cpp", StatusFlags::Modified)});
	const Graphite::GitDecorationView view = ViewOf(index);

	const Graphite::RowStatus tracked = Graphite::FileRowStatusOf(view, "C:/repo/out/tracked.cpp", kOwnEntryOnly);
	CHECK(tracked.Decoration == GitDecoration::Modified);
	CHECK(tracked.Letter == 'M');

	CHECK(Graphite::FileRowStatusOf(view, "C:/repo/gen/a.cpp").Letter == 'U');
	CHECK_FALSE(Graphite::FileRowStatusOf(view, "C:/repo/gen/a.cpp", kOwnEntryOnly).HasMarker());
}

TEST_CASE("MarkerTooltipOf: a letter names its state, a dot what lies beneath")
{
	const Graphite::GitDecorations index = IndexOf({Entry(u8"src/a.cpp", StatusFlags::Modified), UntrackedDirectory(u8"build/gen")});
	const Graphite::GitDecorationView view = ViewOf(index);

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

	float scrollableWidth = -1.0f;

	const auto drawFileRow = [&]
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(windowWidth, 300.0f));
		ImGui::Begin("##fileRowWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		{
			const Graphite::ListBodyScope listBody;
			const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();

			Graphite::ListRowSelectable("##row", false);
			Graphite::DrawFileRowLabel(layout, Graphite::FileRowLook{.Ink = 0xFFFFFFFFu}, longLabel);
		}

		scrollableWidth = ImGui::GetCurrentWindow()->ScrollMax.x;
		ImGui::End();
	};

	frames.Next(drawFileRow);
	frames.Next(drawFileRow);

	CHECK(scrollableWidth == doctest::Approx(0.0f));
}

TEST_CASE("An indented folder row's label starts past its chevron at the indent, though its row spans the whole list")
{
	ImGuiFrames frames;

	const float indent = 40.0f;
	float labelLeft = 0.0f;
	float expectedLeft = 0.0f;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
		ImGui::Begin("##folderRowWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		{
			const Graphite::ListBodyScope listBody;

			ImGui::Indent(indent);
			expectedLeft = ImGui::GetCursorScreenPos().x + Graphite::TreeRowLabelOffset();

			const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();
			const int verticesBefore = ImGui::GetWindowDrawList()->VtxBuffer.Size;

			if (Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				ImGui::TreePop();
			}

			const int labelFrom = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			constexpr bool kClosed = false;
			Graphite::DrawTreeRowLabel(layout, Graphite::FolderIconOf(kClosed), "folder", kNoMarker);

			labelLeft = FLT_MAX;
			const ImDrawList& drawing = *ImGui::GetWindowDrawList();
			for (int vertex = labelFrom; vertex < drawing.VtxBuffer.Size; ++vertex)
			{
				labelLeft = std::min(labelLeft, drawing.VtxBuffer[vertex].pos.x);
			}

			CHECK(labelFrom > verticesBefore);
			ImGui::Unindent(indent);
		}

		ImGui::End();
	});

	CHECK(labelLeft >= expectedLeft - 1.0f);
	CHECK(labelLeft <= expectedLeft + ImGui::GetFontSize());
}

TEST_CASE("A tree row's note follows its label dim, and the label is cut short first to leave it room")
{
	ImGuiFrames frames;

	float labelRight = -FLT_MAX;
	float noteLeft = FLT_MAX;
	float noteRight = -FLT_MAX;
	float rowEnd = 0.0f;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(220.0f, 200.0f));
		ImGui::Begin("##noteWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		int from = 0;

		{
			const Graphite::ListBodyScope listBody;
			const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();
			Graphite::ListRowSelectable("##root", false);
			from = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			Graphite::DrawTreeRowLabel(layout, Graphite::FileIconOf(Graphite::FileKind::Solution), std::string(80, 'D'),
									   kNoMarker, "(1 project)");
		}

		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImDrawList& drawing = *ImGui::GetWindowDrawList();

		for (int vertex = from; vertex < drawing.VtxBuffer.Size; ++vertex)
		{
			const ImDrawVert& drawn = drawing.VtxBuffer[vertex];

			if (drawn.col == text)
			{
				labelRight = std::max(labelRight, drawn.pos.x);
			}
			else if (drawn.col == dim)
			{
				noteLeft = std::min(noteLeft, drawn.pos.x);
				noteRight = std::max(noteRight, drawn.pos.x);
			}
		}

		const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
		rowEnd = window->InnerRect.Max.x - window->WindowPadding.x - ImGui::GetStyle().ItemSpacing.x;
		ImGui::End();
	});

	REQUIRE(labelRight > -FLT_MAX);
	REQUIRE(noteLeft < FLT_MAX);
	CHECK(noteLeft > labelRight);
	CHECK(noteRight <= rowEnd);
}

TEST_CASE("A tree row too narrow for its note beside its label shows the label alone")
{
	ImGuiFrames frames;

	bool labelDrawn = false;
	bool noteDrawn = false;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(110.0f, 200.0f));
		ImGui::Begin("##narrowNoteWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		int from = 0;

		{
			const Graphite::ListBodyScope listBody;
			const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();
			Graphite::ListRowSelectable("##root", false);
			from = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			Graphite::DrawTreeRowLabel(layout, Graphite::FileIconOf(Graphite::FileKind::Solution), "Demo solution",
									   kNoMarker, "(12 projects)");
		}

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		for (int vertex = from; vertex < drawing.VtxBuffer.Size; ++vertex)
		{
			labelDrawn = labelDrawn || drawing.VtxBuffer[vertex].col == ImGui::GetColorU32(ImGuiCol_Text);
			noteDrawn = noteDrawn || drawing.VtxBuffer[vertex].col == ImGui::GetColorU32(ImGuiCol_TextDisabled);
		}

		ImGui::End();
	});

	CHECK(labelDrawn);
	CHECK_FALSE(noteDrawn);
}

TEST_CASE("A file row draws its badge in its kind's colour at the label's start and its name in its ink past the badge")
{
	ImGuiFrames frames;

	Graphite::FileRowLayout layout;
	std::optional<HorizontalExtent> glyph;
	std::optional<HorizontalExtent> name;
	float iconWidth = 0.0f;
	float nameOffset = 0.0f;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
		ImGui::Begin("##kindRowWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

		{
			const Graphite::ListBodyScope listBody;
			layout = Graphite::LayoutFileRow();
			Graphite::ListRowSelectable("##row", false);
			const Graphite::FileRowLook look{.Ink = kNameInk, .Icon = Graphite::FileIconOf(Graphite::FileKind::CppSource)};
			Graphite::DrawFileRowLabel(layout, look, "Main.cpp");
		}

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		glyph = HorizontalExtentInColour(drawing, Graphite::ColourOf(Graphite::FileKind::CppSource, kDarkScheme));
		name = HorizontalExtentInColour(drawing, kNameInk);
		iconWidth = Graphite::FileIconWidth();
		nameOffset = Graphite::IconToNameOffset();

		ImGui::End();
	});

	REQUIRE(glyph.has_value());
	REQUIRE(name.has_value());
	CHECK(glyph->Left >= layout.LabelAt.x - kOnePixel);
	CHECK(glyph->Right <= layout.LabelAt.x + iconWidth + kOnePixel);
	CHECK(name->Left >= layout.LabelAt.x + nameOffset - kOnePixel);
	CHECK(name->Left > glyph->Right);
}

TEST_CASE("A folder row draws the outline glyph in the folder grey and its name in the text colour past it")
{
	ImGuiFrames frames;

	Graphite::FileRowLayout layout;
	std::optional<HorizontalExtent> glyph;
	std::optional<HorizontalExtent> name;
	float nameOffset = 0.0f;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
		ImGui::Begin("##folderKindWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

		{
			const Graphite::ListBodyScope listBody;
			layout = Graphite::LayoutFileRow();
			const bool open = Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth);
			Graphite::DrawTreeRowLabel(layout, Graphite::FolderIconOf(open), "folder", kNoMarker);

			if (open)
			{
				ImGui::TreePop();
			}
		}

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		glyph = HorizontalExtentInColour(drawing, Graphite::DimFolderColour());
		name = HorizontalExtentInColour(drawing, ImGui::GetColorU32(ImGuiCol_Text));
		nameOffset = Graphite::IconToNameOffset();

		ImGui::End();
	});

	REQUIRE(glyph.has_value());
	REQUIRE(name.has_value());
	CHECK(glyph->Left >= layout.LabelAt.x - kOnePixel);
	CHECK(name->Left >= layout.LabelAt.x + nameOffset - kOnePixel);
	CHECK(name->Left > glyph->Right);
}

TEST_CASE("A row's marker column is 12 px wide, whatever the font size")
{
	constexpr float kMarkerColumnInPixels = 12.0f;
	constexpr float kLargeFontSize = 40.0f;

	ImGuiFrames frames;

	frames.Next([] {
		const float atDefaultSize = Graphite::RowMarkerColumnWidth();

		ImGui::PushFont(nullptr, kLargeFontSize);
		const float atLargeSize = Graphite::RowMarkerColumnWidth();
		ImGui::PopFont();

		CHECK(atDefaultSize == doctest::Approx(Core::Scaled(kMarkerColumnInPixels)));
		CHECK(atLargeSize == doctest::Approx(atDefaultSize));
	});
}

TEST_CASE("A row's marker is drawn only when a letter of the label fits beside it")
{
	ImGuiFrames frames;

	Graphite::RowStatus dot;
	dot.Dot = true;
	dot.Ink = 0xFFFFFFFFu;

	const auto markerLeftEdgeInWindowOfWidth = [&](float windowWidth)
	{
		float markerLeftEdge = 0.0f;
		float rightEdge = 0.0f;

		const auto drawRow = [&]
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(windowWidth, 300.0f));
			ImGui::Begin("##markerWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

			{
				const Graphite::ListBodyScope listBody;
				const Graphite::FileRowLayout layout = Graphite::LayoutFileRow();
				Graphite::ListRowSelectable("##row", false);
				rightEdge = Graphite::DrawRowMarker(Graphite::RowStatus{}, layout);
				markerLeftEdge = Graphite::DrawRowMarker(dot, layout);
			}

			ImGui::End();
		};

		frames.Next(drawRow);
		frames.Next(drawRow);

		return std::pair{markerLeftEdge, rightEdge};
	};

	const auto [wideLeftEdge, wideRightEdge] = markerLeftEdgeInWindowOfWidth(400.0f);
	CHECK(wideLeftEdge < wideRightEdge);

	const auto [narrowLeftEdge, narrowRightEdge] = markerLeftEdgeInWindowOfWidth(50.0f);
	CHECK(narrowLeftEdge == doctest::Approx(narrowRightEdge));
}

TEST_CASE("MarkerTooltipOf covers every decoration, as a letter and as a dot")
{
	const TooltipCase cases[] = {
		{GitDecoration::Untracked, "Untracked", "Contains untracked files"},
		{GitDecoration::Added, "Added", "Contains added files"},
		{GitDecoration::Renamed, "Renamed", "Contains renamed files"},
		{GitDecoration::Modified, "Modified", "Contains modified or deleted files"},
		{GitDecoration::Conflicted, "Conflicted", "Contains files with conflicts"},
	};

	for (const TooltipCase& expected : cases)
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
