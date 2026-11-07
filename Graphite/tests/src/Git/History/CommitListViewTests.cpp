#include <doctest/doctest.h>

#include "Git/History/CommitListView.h"
#include "UI/Lists/ListRow.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Core/Base/Base.h"
#include "Core/Git/GraphLayout.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

using namespace Graphite;

namespace {

		constexpr float kHistoryWidth = 900.0f;
	constexpr float kHistoryHeight = 600.0f;

	// The number's decimal digits, padded with zeros to the 40 hex digits of an id, so each number has an id of its own.
	Core::CommitSummary CommitNumbered(int number)
	{
		constexpr std::size_t kHexDigits = 40;
		const std::string digits = std::to_string(number);

		Core::CommitSummary commit;
		commit.Id = *Core::Oid::FromHex(std::string(kHexDigits - digits.size(), '0') + digits);
		commit.Summary = "commit " + std::to_string(number);
		return commit;
	}

	float LastRowTopOf(int commitCount, float& expectedPitch)
	{
		Core::LogPage page;

		for (int number = 1; number <= commitCount; ++number)
		{
			page.Commits.push_back(CommitNumbered(number));
		}

		const Core::CommitGraph graph = Core::LayoutCommits(page.Commits);
		const GitPalette palette;
		const CommitRefMap refs;
		CommitListView view;
		ImGuiFrames frames;

		ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

		float height = 0.0f;

		for (int frame = 0; frame < 3; ++frame)
		{
			frames.Next([&] {
				ImGui::SetNextWindowSize(ImVec2(kHistoryWidth, kHistoryHeight));
				ImGui::Begin("history");
				view.Render(page, graph, palette, nullptr,
							[](const Core::Oid&) { return Core::Ref<const Core::Diff>{}; },
							[](const Core::Oid&) { return false; }, refs);
				expectedPitch = Graphite::ListRowPitch();
				ImGui::End();

				ImGuiContext& context = *ImGui::GetCurrentContext();

				for (int index = 0; index < context.Tables.GetMapSize(); ++index)
				{
					if (const ImGuiTable* table = context.Tables.TryGetMapData(index))
					{
						height = table->RowPosY1;
					}
				}
			});
		}

		return height;
	}
}

TEST_CASE("A commit row is exactly a list row's pitch tall so the graph stays on its rows")
{
	float pitch = 0.0f;
	const float fourthRowTop = LastRowTopOf(4, pitch);
	const float thirdRowTop = LastRowTopOf(3, pitch);

	CHECK(fourthRowTop - thirdRowTop == doctest::Approx(pitch));
}

TEST_CASE("A commit row under the pointer is highlighted once across the history, inset at its ends, in the list colours")
{
	constexpr int kCommitCount = 3;
	constexpr int kSettleFrames = 3;
	constexpr float kFringe = 1.5f;
	constexpr float kHighlightInset = 6.0f;

	Core::LogPage page;

	for (int number = 1; number <= kCommitCount; ++number)
	{
		page.Commits.push_back(CommitNumbered(number));
	}

	const Core::CommitGraph graph = Core::LayoutCommits(page.Commits);
	const GitPalette palette;
	const CommitRefMap refs;
	CommitListView view;
	ImGuiFrames frames;

	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kHistoryWidth, kHistoryHeight));
		ImGui::Begin("history");
		view.Render(page, graph, palette, nullptr,
					[](const Core::Oid&) { return Core::Ref<const Core::Diff>{}; },
					[](const Core::Oid&) { return false; }, refs);
		ImGui::End();
	};

	const auto commitTable = [] {
		ImGuiContext& context = *ImGui::GetCurrentContext();

		for (int index = 0; index < context.Tables.GetMapSize(); ++index)
		{
			if (const ImGuiTable* table = context.Tables.TryGetMapData(index))
			{
				return table;
			}
		}

		return static_cast<const ImGuiTable*>(nullptr);
	};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		frames.Next(draw);
	}

	const ImGuiTable* table = commitTable();
	REQUIRE(table != nullptr);

	const float lastRowTop = table->RowPosY1;
	const float lastRowBottom = table->RowPosY2;
	const ImRect ends = table->WorkRect;

	ImGui::GetIO().AddMousePosEvent((ends.Min.x + ends.Max.x) * 0.5f, (lastRowTop + lastRowBottom) * 0.5f);

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		frames.Next(draw);
	}

	const ImDrawList& drawing = *table->InnerWindow->DrawList;
	const ImU32 hoveredColour = ImGui::GetColorU32(Graphite::CurrentListRowColours().Hovered);

	float top = FLT_MAX;
	float bottom = -FLT_MAX;
	float left = FLT_MAX;
	float right = -FLT_MAX;

	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == hoveredColour)
		{
			top = std::min(top, vertex.pos.y);
			bottom = std::max(bottom, vertex.pos.y);
			left = std::min(left, vertex.pos.x);
			right = std::max(right, vertex.pos.x);
		}
	}

	REQUIRE(top < bottom);
	CHECK(std::abs(top - lastRowTop) <= kFringe);
	CHECK(std::abs(bottom - lastRowBottom) <= kFringe);
	CHECK(std::abs(left - (ends.Min.x + kHighlightInset)) <= kFringe);
	CHECK(std::abs(right - (ends.Max.x - kHighlightInset)) <= kFringe);

	CHECK_FALSE(DrawsInColour(drawing, ImGui::GetColorU32(ImGuiCol_HeaderHovered)));
}

TEST_CASE("Root anchor highlights a root file's name in an expanded commit")
{
	constexpr int kSettleFrames = 3;

	Core::LogPage page;
	page.Commits.push_back(CommitNumbered(1));

	const Core::CommitGraph graph = Core::LayoutCommits(page.Commits);
	const GitPalette palette;
	const CommitRefMap refs;
	CommitListView view;
	ImGuiFrames frames;

	auto diff = std::make_shared<Core::Diff>();
	diff->Files.push_back(Core::FileDiff{.OldPath = "README.md", .NewPath = "README.md", .Change = Core::FileChange::Modified});
	const Core::Ref<const Core::Diff> shownDiff = diff;

	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	view.SetExpanded(page.Commits[0].Id, true);

	// What File History writes for a file at the root.
	const std::string_view pathFilter = "/README.md";

	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kHistoryWidth, kHistoryHeight));
		ImGui::Begin("history");
		view.Render(page, graph, palette, nullptr, [&shownDiff](const Core::Oid&) { return shownDiff; },
					[](const Core::Oid&) { return false; }, refs, {}, pathFilter);
		ImGui::End();
	};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		frames.Next(draw);
	}

	const ImGuiTable* table = nullptr;
	ImGuiContext& context = *ImGui::GetCurrentContext();

	for (int index = 0; index < context.Tables.GetMapSize(); ++index)
	{
		if (const ImGuiTable* listed = context.Tables.TryGetMapData(index))
		{
			table = listed;
		}
	}

	REQUIRE(table != nullptr);
	CHECK(DrawsInColour(*table->InnerWindow->DrawList, ImGui::GetColorU32(ImGuiCol_CheckMark)));
}

TEST_CASE("Commit reveal scrolls the history to a commit far down the page")
{
	constexpr int kCommitCount = 300;
	constexpr int kTargetCommit = 250;
	constexpr int kSettleFrames = 3;
	constexpr int kByteValues = 256;

	Core::LogPage page;

	for (int number = 0; number < kCommitCount; ++number)
	{
		Core::CommitSummary commit;
		commit.Id.Bytes[0] = static_cast<std::uint8_t>(number / kByteValues + 1);
		commit.Id.Bytes[1] = static_cast<std::uint8_t>(number % kByteValues);
		commit.Summary = "commit " + std::to_string(number);
		page.Commits.push_back(commit);
	}

	const Core::CommitGraph graph = Core::LayoutCommits(page.Commits);
	const GitPalette palette;
	const CommitRefMap refs;
	CommitListView view;
	ImGuiFrames frames;
	float pitch = 0.0f;

	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kHistoryWidth, kHistoryHeight));
		ImGui::Begin("history");
		view.Render(page, graph, palette, nullptr,
					[](const Core::Oid&) { return Core::Ref<const Core::Diff>{}; },
					[](const Core::Oid&) { return false; }, refs);
		pitch = Graphite::ListRowPitch();
		ImGui::End();
	};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		frames.Next(draw);
	}

	view.ScrollToCommit(page.Commits[kTargetCommit].Id);

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		frames.Next(draw);
	}

	const ImGuiTable* table = nullptr;
	ImGuiContext& context = *ImGui::GetCurrentContext();

	for (int index = 0; index < context.Tables.GetMapSize(); ++index)
	{
		if (const ImGuiTable* listed = context.Tables.TryGetMapData(index))
		{
			table = listed;
		}
	}

	REQUIRE(table != nullptr);

	const ImGuiWindow& list = *table->InnerWindow;
	const float targetTop = static_cast<float>(kTargetCommit) * pitch;

	CHECK(list.Scroll.y > 0.0f);
	CHECK(targetTop >= list.Scroll.y);
	CHECK(targetTop + pitch <= list.Scroll.y + list.Size.y);
}

namespace {

	// A short history drawn with marks, and clicks on its rows by their display index.
	struct MarkedHistory
	{
		explicit MarkedHistory(const char* windowName, int commitCount) : WindowName(windowName)
		{
			for (int number = 1; number <= commitCount; ++number)
			{
				Page.Commits.push_back(CommitNumbered(number));
			}

			Graph = Core::LayoutCommits(Page.Commits);
			ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kHistoryWidth, kHistoryHeight));
			ImGui::Begin(WindowName);
			View.Render(Page, Graph, Palette, nullptr, [](const Core::Oid&) { return Core::Ref<const Core::Diff>{}; },
						[](const Core::Oid&) { return false; }, Refs, {}, {}, Marks);
			ImGui::End();
		}

		void Settle()
		{
			for (int frame = 0; frame < 3; ++frame)
			{
				Frames.Next([&] { Draw(); });
			}
		}

		const ImGuiTable* Table() const
		{
			ImGuiContext& context = *ImGui::GetCurrentContext();

			for (int index = 0; index < context.Tables.GetMapSize(); ++index)
			{
				if (const ImGuiTable* table = context.Tables.TryGetMapData(index))
				{
					return table;
				}
			}

			return nullptr;
		}

		// The rows are all in view, so the last one drawn is the last of the list.
		void ClickRow(int displayRow, int rowCount)
		{
			const ImGuiTable* table = Table();
			REQUIRE(table != nullptr);

			const float pitch = ListRowPitch();
			const float lastRowTop = table->RowPosY1;
			const float rowMiddle = lastRowTop - static_cast<float>(rowCount - 1 - displayRow) * pitch + pitch * 0.5f;

			Frames.Click(ImVec2((table->WorkRect.Min.x + table->WorkRect.Max.x) * 0.5f, rowMiddle), [&] { Draw(); });
		}

		const char* WindowName;
		ImGuiFrames Frames;
		Core::LogPage Page;
		Core::CommitGraph Graph;
		GitPalette Palette;
		CommitRefMap Refs;
		HistoryMarks Marks;
		CommitListView View;
	};
}

namespace {

	int ColumnNamed(const ImGuiTable& table, std::string_view name)
	{
		for (int column = 0; column < table.ColumnsCount; ++column)
		{
			if (std::string_view{ImGui::TableGetColumnName(&table, column)} == name)
			{
				return column;
			}
		}

		return -1;
	}
}

TEST_CASE("Git window: dragging the separator between the message and the author widens the author's column for good")
{
	constexpr int kCommitCount = 30;
	constexpr float kDragLeft = 40.0f;
	constexpr int kHoverFrames = 6; // past ImGui's delay before a separator shows it is under the pointer
	constexpr float kTruncation = 1.0f;

	MarkedHistory history("ResizedAuthorHistory", kCommitCount);
	const auto draw = [&] { history.Draw(); };

	history.Settle();

	const ImGuiTable* table = history.Table();
	REQUIRE(table != nullptr);

	const int message = ColumnNamed(*table, "Message");
	const int author = ColumnNamed(*table, "Author");
	REQUIRE(message >= 0);
	REQUIRE(author >= 0);

	const float widthBefore = table->Columns[author].WidthGiven;
	const float separatorX = table->Columns[message].MaxX;
	const float middleY = (table->InnerRect.Min.y + table->InnerRect.Max.y) * 0.5f;
	const ImU32 heldSeparator = ImGui::GetColorU32(ImGuiCol_SeparatorActive);

	// No line stands between the columns until a separator is pointed at.
	CHECK_FALSE(DrawsInColour(*table->InnerWindow->DrawList, table->BorderColorStrong));

	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(separatorX, middleY);

	for (int frame = 0; frame < kHoverFrames; ++frame)
	{
		history.Frames.Next(draw);
	}

	CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW);

	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	history.Frames.Next(draw);
	io.AddMousePosEvent(separatorX - kDragLeft, middleY);
	history.Frames.Next(draw);
	history.Frames.Next(draw);

	CHECK(DrawsInColour(*table->InnerWindow->DrawList, heldSeparator));

	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	history.Frames.Next(draw);
	io.AddMousePosEvent(table->InnerRect.Min.x + 1.0f, middleY);
	history.Frames.Next(draw);

	const float widthAfter = table->Columns[author].WidthGiven;
	CHECK(widthAfter >= widthBefore + kDragLeft - kTruncation);
	CHECK(widthAfter <= widthBefore + kDragLeft + kTruncation);

	history.Settle();

	CHECK(table->Columns[author].WidthGiven == widthAfter);
	CHECK_FALSE(DrawsInColour(*table->InnerWindow->DrawList, heldSeparator));
}

TEST_CASE("Git window: showing the newer commits dims them")
{
	MarkedHistory history("DimmedHistory", 3);
	const ImU32 dimmedText = Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_Text), CommitListView::kDimmedRowAlpha);

	history.Settle();
	REQUIRE(history.Table() != nullptr);
	CHECK_FALSE(DrawsInColour(*history.Table()->InnerWindow->DrawList, dimmedText));

	const std::unordered_set<Core::Oid> newer = {history.Page.Commits[0].Id};
	history.Marks.Dimmed = &newer;
	history.Settle();

	CHECK(DrawsInColour(*history.Table()->InnerWindow->DrawList, dimmedText));
}

TEST_CASE("Git window: clicking the fold row is reported once")
{
	MarkedHistory history("FoldClickHistory", 3);
	history.Marks.Fold = FoldRow{.BeforeCommit = 1, .Text = "2 newer commits on develop since this branch forked", .Action = "Show them"};

	history.Settle();
	CHECK_FALSE(history.View.TakeFoldClick());

	constexpr int kRowCount = 4;
	constexpr int kFoldRow = 1;
	history.ClickRow(kFoldRow, kRowCount);

	CHECK(history.View.TakeFoldClick());
	CHECK_FALSE(history.View.TakeFoldClick());
	CHECK_FALSE(history.View.GetSelectedCommit().has_value());
}

TEST_CASE("Git window: choosing the local changes row selects it and choosing a commit leaves it")
{
	MarkedHistory history("LocalChangesHistory", 2);
	history.Marks.LocalChanges = "1 staged \xC2\xB7 0 not staged";

	history.Settle();
	CHECK_FALSE(history.View.IsLocalChangesSelected());

	constexpr int kRowCount = 3;
	history.ClickRow(0, kRowCount);

	CHECK(history.View.TakeLocalChangesClick());
	CHECK_FALSE(history.View.TakeLocalChangesClick());
	CHECK(history.View.IsLocalChangesSelected());

	history.ClickRow(2, kRowCount);

	CHECK_FALSE(history.View.IsLocalChangesSelected());
	CHECK(history.View.GetSelectedCommit() == std::optional<Core::Oid>{history.Page.Commits[1].Id});
}

TEST_CASE("Git window: while the history waits the local changes row stands above one waiting row")
{
	MarkedHistory waiting("WaitingWithLocalChanges", 0);
	waiting.Marks.LocalChanges = "\xE2\x80\xA6";
	waiting.Marks.Waiting = "Reading the branches' lines\xE2\x80\xA6";

	waiting.Settle();
	REQUIRE(waiting.Table() != nullptr);

	constexpr int kRowCount = 2;
	waiting.ClickRow(0, kRowCount);
	CHECK(waiting.View.IsLocalChangesSelected());
}

TEST_CASE("Git window: while a history without local changes waits it is one waiting row of the list")
{
	MarkedHistory waiting("WaitingAlone", 0);
	waiting.Marks.Waiting = "Reading the branches' lines\xE2\x80\xA6";

	waiting.Settle();
	CHECK(waiting.Table() != nullptr);
}

TEST_CASE("Git window: a history with no commits and nothing on its way says so in place of the list")
{
	MarkedHistory empty("NothingToList", 0);

	empty.Settle();
	CHECK(empty.Table() == nullptr);
}

TEST_CASE("Git window: the local changes selection goes with its row")
{
	MarkedHistory history("LocalChangesGone", 1);
	history.Marks.LocalChanges = "nothing to commit";
	history.View.SelectLocalChanges();

	history.Settle();
	CHECK(history.View.IsLocalChangesSelected());

	history.Marks.LocalChanges.reset();
	history.Settle();

	CHECK_FALSE(history.View.IsLocalChangesSelected());
}

TEST_CASE("Git window: a brought commit is selected by a click on its row")
{
	MarkedHistory history("BroughtHistory", 2);
	history.Page.Commits[0].Parents.resize(2);

	Core::LogPage brought;
	brought.Commits.push_back(CommitNumbered(7));
	brought.Commits.push_back(CommitNumbered(8));
	const Core::Ref<const Core::LogPage> held = Core::CreateRef<const Core::LogPage>(brought);

	history.Marks.BroughtOf = [held](const Core::Oid&) { return held; };
	history.View.SetExpanded(history.Page.Commits[0].Id, true);

	history.Settle();

	constexpr int kRowCount = 4;
	history.ClickRow(2, kRowCount);

	CHECK(history.View.GetSelectedCommit() == std::optional<Core::Oid>{CommitNumbered(8).Id});

	// The selection stays while its merge is in the page and open.
	history.View.KeepSelectionWithin(history.Page, history.Marks);
	CHECK(history.View.GetSelectedCommit().has_value());

	history.View.SetExpanded(history.Page.Commits[0].Id, false);
	history.View.KeepSelectionWithin(history.Page, history.Marks);
	CHECK_FALSE(history.View.GetSelectedCommit().has_value());
}

TEST_CASE("Git window: an opened merge whose commits are being read is followed by one waiting row")
{
	MarkedHistory history("BroughtWaiting", 2);
	history.Page.Commits[0].Parents.resize(2);
	history.Marks.BroughtOf = [](const Core::Oid&) { return Core::Ref<const Core::LogPage>{}; };
	history.View.SetExpanded(history.Page.Commits[0].Id, true);

	history.Settle();

	constexpr int kRowCount = 3;
	history.ClickRow(2, kRowCount);
	CHECK(history.View.GetSelectedCommit() == std::optional<Core::Oid>{history.Page.Commits[1].Id});
}

TEST_CASE("Git window: a collapsed merge still takes its count from the page it holds")
{
	Core::CommitSummary merge = CommitNumbered(1);
	merge.Parents.resize(2);
	const Core::CommitSummary plain = CommitNumbered(2);

	const Core::Ref<const Core::LogPage> held = Core::CreateRef<const Core::LogPage>();

	HistoryMarks marks;
	CHECK(BroughtCountedFor(marks, merge) == nullptr);

	marks.BroughtOf = [held](const Core::Oid&) { return held; };

	CommitListView view;
	CHECK_FALSE(view.IsExpanded(merge.Id));
	CHECK(BroughtCountedFor(marks, merge) == held);
	CHECK(BroughtCountedFor(marks, plain) == nullptr);
}

TEST_CASE("Git window: an opened merge whose commits could not be read is followed by one row saying so")
{
	MarkedHistory history("BroughtFailed", 2);
	history.Page.Commits[0].Parents.resize(2);
	history.Marks.BroughtOf = [](const Core::Oid&) { return Core::Ref<const Core::LogPage>{}; };
	history.Marks.BroughtFailure = [](const Core::Oid&) { return std::optional<std::string>{"bad object"}; };
	history.View.SetExpanded(history.Page.Commits[0].Id, true);

	history.Settle();

	constexpr int kRowCount = 3;
	history.ClickRow(2, kRowCount);
	CHECK(history.View.GetSelectedCommit() == std::optional<Core::Oid>{history.Page.Commits[1].Id});
}
