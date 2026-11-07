#include <doctest/doctest.h>

#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"
#include "Git/Sidebar/SidebarView.h"
#include "Theme/BuiltInSchemes.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	using namespace Graphite;

	constexpr float kWidth = 300.0f;
	constexpr float kHeight = 400.0f;

	SidebarRow Section(std::string key)
	{
		SidebarRow row;
		row.Kind = SidebarRowKind::Section;
		row.Key = key;
		row.Label = "Yours";
		row.Count = 2;
		row.Open = true;
		return row;
	}

	SidebarRow BranchRow(std::size_t ref)
	{
		SidebarRow row;
		row.Kind = SidebarRowKind::Branch;
		row.Key = "yours";
		row.Ref = ref;
		return row;
	}

	SidebarRowText TextOfRow(const SidebarRow& row)
	{
		SidebarRowText text;
		text.Head = "branch-" + std::to_string(row.Ref.value_or(0));
		text.Meta.push_back({"3 days ago", SidebarTone::Dim});
		text.Meta.push_back({"local only", SidebarTone::Local});
		return text;
	}

	// The window the sidebar's rows scroll in, found by name since ImGui names a child after its parent.
	ImGuiWindow* RowsWindowOf(std::string_view parentName)
	{
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (window->ParentWindow != nullptr && parentName == window->ParentWindow->Name &&
				std::strstr(window->Name, "SidebarRows") != nullptr)
			{
				return window;
			}
		}

		return nullptr;
	}

	struct Harness
	{
		ImGuiFrames Frames;
		SidebarView View;
		const char* WindowName;
		const std::vector<SidebarRow>* Rows = nullptr;
		std::function<SidebarRowText(const SidebarRow&)> TextOf = TextOfRow;
		std::optional<SidebarWaitingRow> Waiting;
		ResolvedScheme Scheme = DarkScheme();

		explicit Harness(const char* windowName) : WindowName(windowName) {}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin(WindowName, nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			View.Draw(Rows, TextOf, Waiting, SidebarSelection{}, ReadSidebarColours(Scheme));
			ImGui::End();
			ImGui::PopStyleVar();
		}

		void Next() { Frames.Next([&] { Draw(); }); }

		void Settle()
		{
			Next();
			Next();
		}

		void Click(ImVec2 at, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			Frames.Click(at, [&] { Draw(); }, button);
		}

		ImGuiWindow* RowsWindow() const { return RowsWindowOf(WindowName); }
	};

	ImVec2 InRows(const ImGuiWindow& rows, float down)
	{
		return ImVec2(rows.Pos.x + 60.0f, rows.Pos.y + down);
	}
}

TEST_CASE("Git window: clicking a branch row reports that row")
{
	Harness harness("SidebarClickBranch");
	const std::vector<SidebarRow> rows = {Section("yours"), BranchRow(0), BranchRow(1)};
	harness.Rows = &rows;

	std::vector<SidebarRow> clicked;
	harness.View.OnClick = [&](const SidebarRow& row) { clicked.push_back(row); };

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	const float secondBranchMiddle = ListRowPitch() + SidebarTwoLineRowHeight() * 1.5f;
	harness.Click(InRows(*list, secondBranchMiddle));

	REQUIRE(clicked.size() == 1);
	CHECK(clicked[0].Kind == SidebarRowKind::Branch);
	CHECK(clicked[0].Ref == std::optional<std::size_t>{1});
}

TEST_CASE("Git window: clicking a section header reports it for toggling")
{
	Harness harness("SidebarClickSection");
	const std::vector<SidebarRow> rows = {Section("yours"), BranchRow(0)};
	harness.Rows = &rows;

	std::vector<SidebarRow> clicked;
	harness.View.OnClick = [&](const SidebarRow& row) { clicked.push_back(row); };

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	harness.Click(InRows(*list, ListRowPitch() * 0.5f));

	REQUIRE(clicked.size() == 1);
	CHECK(clicked[0].Kind == SidebarRowKind::Section);
	CHECK(clicked[0].Key == "yours");
	CHECK(rows[0].Open);
}

TEST_CASE("Git window: right-clicking a branch row opens its menu")
{
	Harness harness("SidebarMenuBranch");
	const std::vector<SidebarRow> rows = {Section("yours"), BranchRow(0)};
	harness.Rows = &rows;

	std::vector<SidebarRow> menuFor;
	harness.View.DrawRowMenu = [&](const SidebarRow& row) {
		menuFor.push_back(row);
		ImGui::MenuItem("Checkout");
	};

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	harness.Click(InRows(*list, ListRowPitch() * 0.5f), ImGuiMouseButton_Right);
	harness.Next();
	CHECK(menuFor.empty());

	harness.Click(InRows(*list, ListRowPitch() + SidebarTwoLineRowHeight() * 0.5f), ImGuiMouseButton_Right);
	harness.Next();

	REQUIRE_FALSE(menuFor.empty());
	CHECK(menuFor.back().Kind == SidebarRowKind::Branch);
	CHECK(menuFor.back().Ref == std::optional<std::size_t>{0});
}

TEST_CASE("Git window: with no rows one waiting row is drawn")
{
	Harness harness("SidebarWaiting");
	harness.Waiting = SidebarWaitingRow{.Text = "Reading branches"};
	int described = 0;
	harness.TextOf = [&](const SidebarRow& row) {
		++described;
		return TextOfRow(row);
	};

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	std::size_t letters = 0;

	for (const char character : harness.Waiting->Text)
	{
		letters += character != ' ' ? 1 : 0;
	}

	CHECK(described == 0);
	CHECK(DrawnGlyphs(*list->DrawList).size() == letters);
	CHECK(list->ContentSize.y <= ListRowPitch() * 2.0f);
}

TEST_CASE("Git window: a failed waiting row turns no loader beside its text")
{
	// Past the wait before a loader shows, the loader stands before the text of a row still waiting.
	constexpr int kFramesPastTheLoaderWait = 30;

	const auto firstGlyphLeft = [](const char* windowName, bool failed) {
		Harness harness(windowName);
		harness.Waiting = SidebarWaitingRow{.Text = "Cannot read", .Failed = failed};

		for (int frame = 0; frame < kFramesPastTheLoaderWait; ++frame)
		{
			harness.Next();
		}

		const ImGuiWindow* list = harness.RowsWindow();
		REQUIRE(list != nullptr);

		const std::vector<DrawnGlyph> glyphs = DrawnGlyphs(*list->DrawList);
		REQUIRE_FALSE(glyphs.empty());

		float left = glyphs.front().Box.Min.x;

		for (const DrawnGlyph& glyph : glyphs)
		{
			left = std::min(left, glyph.Box.Min.x);
		}

		return left;
	};

	CHECK(firstGlyphLeft("SidebarFailedRow", true) < firstGlyphLeft("SidebarWaitingRow", false));
}

TEST_CASE("Git window: rows under the waiting row answer clicks")
{
	Harness harness("SidebarRowsUnderWaiting");
	harness.Waiting = SidebarWaitingRow{.Text = "Reading branches"};

	SidebarRow stashes = Section("stashes");
	stashes.Section = SidebarSection::Stashes;
	SidebarRow stash;
	stash.Kind = SidebarRowKind::Stash;
	stash.Section = SidebarSection::Stashes;
	stash.Stash = 0;
	const std::vector<SidebarRow> rows = {stashes, stash};
	harness.Rows = &rows;

	std::vector<SidebarRow> clicked;
	harness.View.OnClick = [&](const SidebarRow& row) { clicked.push_back(row); };

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	harness.Click(InRows(*list, ListRowPitch() * 2.5f));

	REQUIRE(clicked.size() == 1);
	CHECK(clicked[0].Kind == SidebarRowKind::Stash);
	CHECK(clicked[0].Stash == std::optional<std::size_t>{0});
}

TEST_CASE("Git window: of ten thousand rows only those in view are described")
{
	Harness harness("SidebarTenThousand");

	std::vector<SidebarRow> rows;

	for (std::size_t index = 0; index < 10000; ++index)
	{
		rows.push_back(BranchRow(index));
	}

	harness.Rows = &rows;

	std::size_t highest = 0;
	std::size_t described = 0;
	harness.TextOf = [&](const SidebarRow& row) {
		++described;
		highest = std::max(highest, row.Ref.value_or(0));
		return TextOfRow(row);
	};

	harness.Next();
	described = 0;
	harness.Next();

	CHECK(described > 0);
	CHECK(described < 20);

	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);
	ImGui::GetIO().AddMousePosEvent(list->Pos.x + 60.0f, list->Pos.y + 100.0f);
	harness.Next();
	ImGui::GetIO().AddMouseWheelEvent(0.0f, -30.0f);
	harness.Next();
	described = 0;
	harness.Next();

	CHECK(highest > 20);
	CHECK(described < 20);
}

TEST_CASE("Git window: scrolled rows stay at their place in the list")
{
	Harness harness("SidebarScrolled");

	constexpr std::size_t kRowCount = 100;
	constexpr std::size_t kRowsScrolledPast = 5;
	constexpr float kIntoTheNextRow = 3.0f;

	std::vector<SidebarRow> rows;

	for (std::size_t index = 0; index < kRowCount; ++index)
	{
		rows.push_back(BranchRow(index));
	}

	harness.Rows = &rows;

	std::vector<std::pair<std::size_t, float>> drawnTops;
	harness.TextOf = [&](const SidebarRow& row) {
		drawnTops.emplace_back(row.Ref.value_or(0), ImGui::GetItemRectMin().y);
		return TextOfRow(row);
	};

	harness.Settle();
	ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	const float rowHeight = SidebarTwoLineRowHeight();
	const float scrolled = rowHeight * static_cast<float>(kRowsScrolledPast) + kIntoTheNextRow;
	ImGui::SetScrollY(list, scrolled);
	harness.Next();
	drawnTops.clear();
	harness.Next();

	REQUIRE(list->Scroll.y == doctest::Approx(scrolled));
	REQUIRE_FALSE(drawnTops.empty());

	const float listTop = list->Pos.y + list->WindowPadding.y;
	const auto& [firstRow, firstTop] = drawnTops.front();

	CHECK(firstRow == kRowsScrolledPast);
	CHECK(firstTop == doctest::Approx(listTop + rowHeight * static_cast<float>(firstRow) - scrolled));
	CHECK(list->ContentSize.y == doctest::Approx(rowHeight * static_cast<float>(kRowCount)));
}

TEST_CASE("Git window: a notice row answers no click")
{
	Harness harness("SidebarNoticeClick");

	SidebarRow stashes = Section("stashes");
	stashes.Section = SidebarSection::Stashes;
	SidebarRow notice;
	notice.Kind = SidebarRowKind::Notice;
	notice.Section = SidebarSection::Stashes;
	notice.Label = "No stashes";
	const std::vector<SidebarRow> rows = {stashes, notice};
	harness.Rows = &rows;

	std::vector<SidebarRow> clicked;
	harness.View.OnClick = [&](const SidebarRow& row) { clicked.push_back(row); };

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	harness.Click(InRows(*list, ListRowPitch() * 1.5f));

	CHECK(clicked.empty());

	std::size_t noticeGlyphs = 0;

	for (const DrawnGlyph& glyph : DrawnGlyphs(*list->DrawList))
	{
		noticeGlyphs += glyph.Box.Min.y >= list->Pos.y + ListRowPitch() ? 1 : 0;
	}

	CHECK(noticeGlyphs == std::string("Nostashes").size());
}

TEST_CASE("Git window: typing in the search field changes the search")
{
	Harness harness("SidebarSearch");
	const std::vector<SidebarRow> rows = {Section("yours"), BranchRow(0)};
	harness.Rows = &rows;

	harness.Settle();
	CHECK(harness.View.GetSearch().empty());

	harness.Click(ImVec2(100.0f, 20.0f));
	ImGui::GetIO().AddInputCharactersUTF8("fix");
	harness.Next();
	harness.Next();

	CHECK(harness.View.GetSearch() == "fix");
}

TEST_CASE("Git window: a search word is drawn in the match colour in a row's name")
{
	Harness harness("SidebarHighlight");
	const std::vector<SidebarRow> rows = {BranchRow(0)};
	harness.Rows = &rows;
	harness.TextOf = [](const SidebarRow&) {
		SidebarRowText text;
		text.Head = "login-Fix-page";
		text.Meta.push_back({"3 days ago", SidebarTone::Dim});
		return text;
	};

	harness.Settle();
	harness.Click(ImVec2(100.0f, 20.0f));
	ImGui::GetIO().AddInputCharactersUTF8("fix");
	harness.Next();
	harness.Next();
	REQUIRE(harness.View.GetSearch() == "fix");

	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	const ImU32 match = ReadSidebarColours(harness.Scheme).Link;
	std::size_t matched = 0;

	for (const DrawnGlyph& glyph : DrawnGlyphs(*list->DrawList))
	{
		matched += glyph.Colour == match ? 1 : 0;
	}

	CHECK(matched == 3);
}

TEST_CASE("Git window: a request to show a branch's row scrolls the list to it once, centred, in the Draw that takes it")
{
	Harness harness("SidebarScrollToRef");

	constexpr std::size_t kBranchCount = 60;
	constexpr std::size_t kShownRef = 40;
	constexpr float kTruncation = 1.0f;

	std::vector<SidebarRow> rows = {Section("yours")};

	for (std::size_t ref = 0; ref < kBranchCount; ++ref)
	{
		rows.push_back(BranchRow(ref));
	}

	harness.Rows = &rows;
	harness.Settle();

	ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);
	REQUIRE(list->Scroll.y == 0.0f);

	harness.View.ScrollToRef(kShownRef);
	harness.Next();

	const float rowHeight = SidebarTwoLineRowHeight();
	const float rowMiddle = ListRowPitch() + rowHeight * static_cast<float>(kShownRef) + rowHeight * 0.5f;

	CHECK(list->Scroll.y > 0.0f);
	CHECK(std::abs(list->Scroll.y + list->Size.y * 0.5f - rowMiddle) <= kTruncation);

	// The reader scrolls back up, and rows built again later do not scroll the list a second time.
	ImGui::SetScrollY(list, 0.0f);
	const std::vector<SidebarRow> rebuilt = rows;
	harness.Rows = &rebuilt;
	harness.Settle();

	CHECK(list->Scroll.y == 0.0f);
}

TEST_CASE("Git window: a request reaches a row that comes with the rows of the same Draw")
{
	Harness harness("SidebarScrollToOpenedRef");

	constexpr std::size_t kShownRef = 40;
	constexpr float kTruncation = 1.0f;

	std::vector<SidebarRow> opened = {Section("yours")};

	for (std::size_t ref = 0; ref < 60; ++ref)
	{
		opened.push_back(BranchRow(ref));
	}

	// The section closed, as a reader left it: the list has nothing to scroll.
	const std::vector<SidebarRow> closed = {Section("yours")};
	harness.Rows = &closed;
	harness.Settle();

	// The chip opens the section and asks for the row in one go, so the rows that hold it are new in that Draw.
	harness.Rows = &opened;
	harness.View.ScrollToRef(kShownRef);
	harness.Next();

	ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	const float rowHeight = SidebarTwoLineRowHeight();
	const float rowMiddle = ListRowPitch() + rowHeight * static_cast<float>(kShownRef) + rowHeight * 0.5f;

	CHECK(std::abs(list->Scroll.y + list->Size.y * 0.5f - rowMiddle) <= kTruncation);
}

TEST_CASE("Git window: a request to show a row the list does not hold is dropped")
{
	Harness harness("SidebarScrollToMissingRef");

	std::vector<SidebarRow> rows = {Section("yours")};

	for (std::size_t ref = 0; ref < 60; ++ref)
	{
		rows.push_back(BranchRow(ref));
	}

	const std::vector<SidebarRow> withoutTheRow(rows.begin(), rows.begin() + 10);
	harness.Rows = &withoutTheRow;
	harness.Settle();

	harness.View.ScrollToRef(40);
	harness.Settle();

	// The row comes with a later build: by then the request is gone, so the list stays where the reader left it.
	harness.Rows = &rows;
	harness.Settle();

	ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);
	CHECK(list->Scroll.y == 0.0f);
}

TEST_CASE("Git window: a cleared search is gone from the field")
{
	Harness harness("SidebarClearedSearch");
	const std::vector<SidebarRow> rows = {Section("yours"), BranchRow(0)};
	harness.Rows = &rows;

	harness.Settle();
	harness.Click(ImVec2(100.0f, 20.0f));
	ImGui::GetIO().AddInputCharactersUTF8("fix");
	harness.Next();
	harness.Next();
	REQUIRE(harness.View.GetSearch() == "fix");

	// The reader's click lands elsewhere first, as on the window bar's chip.
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);
	harness.Click(InRows(*list, ListRowPitch() + SidebarTwoLineRowHeight() * 0.5f));

	harness.View.ClearSearch();
	harness.Settle();

	CHECK(harness.View.GetSearch().empty());
}

TEST_CASE("Git window: a tag row draws in a folder and answers a click")
{
	Harness harness("SidebarTagRows");

	SidebarRow folder;
	folder.Kind = SidebarRowKind::Group;
	folder.Section = SidebarSection::Tags;
	folder.Key = "tags/rel";
	folder.Label = "rel/";
	folder.Count = 1;
	folder.Open = true;

	SidebarRow tag;
	tag.Kind = SidebarRowKind::Tag;
	tag.Section = SidebarSection::Tags;
	tag.Label = "1.0";
	tag.Tag = 4;
	tag.Depth = 1;

	const std::vector<SidebarRow> rows = {folder, tag};
	harness.Rows = &rows;

	std::vector<SidebarRow> clicked;
	harness.View.OnClick = [&](const SidebarRow& row) { clicked.push_back(row); };

	harness.Settle();
	const ImGuiWindow* list = harness.RowsWindow();
	REQUIRE(list != nullptr);

	harness.Click(InRows(*list, ListRowPitch() * 1.5f));

	REQUIRE(clicked.size() == 1);
	CHECK(clicked[0].Kind == SidebarRowKind::Tag);
	CHECK(clicked[0].Tag == std::optional<std::size_t>{4});
}
