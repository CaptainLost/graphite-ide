#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Branches/BranchListView.h"
#include "UI/Lists/ListRow.h"
#include "Core/ImGui/IconButton.h"

#include "Core/Base/Base.h"
#include "Core/ImGui/Toggle.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr long long kNowSeconds = 1700000000;
	constexpr std::size_t kManyBranches = 2000;
	constexpr float kListWidth = 320.0f;
	constexpr float kListHeight = 400.0f;
	constexpr int kFramesToSettle = 3;

	Core::Branch LocalBranchAt(const std::string& name, long long tipSeconds, std::size_t tipNumber)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Tip.Bytes[0] = static_cast<std::uint8_t>(tipNumber & 0xFF);
		branch.Tip.Bytes[1] = static_cast<std::uint8_t>((tipNumber >> 8) & 0xFF);

		Core::CommitSummary tip;
		tip.Id = branch.Tip;
		tip.Author = Core::Signature{.Name = "Ann", .Email = "ann@example.com", .When = SystemSecondsAt(tipSeconds)};
		tip.Committer = tip.Author;
		branch.TipCommit = tip;

		return branch;
	}

	Graphite::BranchListFrame FrameOf(const Core::Ref<const std::vector<Core::Branch>>& branches)
	{
		Graphite::BranchListFrame frame;
		frame.Branches = branches;
		frame.Now = SystemSecondsAt(kNowSeconds);
		return frame;
	}

	void BeginNamedListWindow(const char* windowName, float width)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, kListHeight));
		ImGui::Begin(windowName, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
											  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
	}

	void BeginListWindow(float width)
	{
		BeginNamedListWindow("BranchList", width);
	}

	// The list's top-left corner, so a test can click a row by its index.
	ImVec2 DrawListWindow(Graphite::BranchListView& list, const Graphite::BranchListFrame& frame, float width)
	{
		BeginListWindow(width);

		const ImVec2 listTop = ImGui::GetCursorScreenPos();
		list.DrawRows(frame);

		ImGui::End();
		return listTop;
	}

	struct MeasuredRows
	{
		ImVec2 ListTop{};
		float FirstTextTop = 0.0f;
		float LastItemTop = 0.0f;
		float CursorTop = 0.0f;
		float RowHeight = 0.0f;
	};

	MeasuredRows DrawAndMeasureRows(Graphite::BranchListView& list, const Graphite::BranchListFrame& frame)
	{
		BeginListWindow(kListWidth);

		MeasuredRows measured;
		measured.ListTop = ImGui::GetCursorScreenPos();
		measured.FirstTextTop = Graphite::ListRowTextTop(measured.ListTop.y);

		list.DrawRows(frame);

		measured.LastItemTop = ImGui::GetItemRectMin().y;
		measured.CursorTop = ImGui::GetCursorScreenPos().y;
		measured.RowHeight = Graphite::ListRowPitch();

		ImGui::End();
		return measured;
	}

	float RowsAboveLastItem(const MeasuredRows& measured)
	{
		return (measured.LastItemTop - measured.FirstTextTop) / measured.RowHeight;
	}

	constexpr std::size_t kTipBefore = 1;
	constexpr std::size_t kTipMoved = 2;
	constexpr std::size_t kOneOwnCommit = 1;

	Core::Ref<const Graphite::BranchAuthors> AuthorsWhereBobOwns(const Core::Branch& branch)
	{
		const Core::Signature bob{.Name = "Bob", .Email = "bob@example.com", .When = SystemSecondsAt(kNowSeconds)};

		Graphite::BranchAuthors authors;
		authors.ByTip.emplace(branch.Tip, Core::TipAuthors{.Tip = branch.Tip, .Authors = {bob}, .OwnCommits = kOneOwnCommit});

		return Core::CreateRef<const Graphite::BranchAuthors>(std::move(authors));
	}

	Core::Ref<const std::vector<Core::Branch>> ListOf(const Core::Branch& branch)
	{
		return Core::CreateRef<const std::vector<Core::Branch>>(std::vector<Core::Branch>{branch});
	}

	Graphite::BranchListFrame FrameWithAuthors(const Core::Ref<const std::vector<Core::Branch>>& branches,
											   const Core::Ref<const Graphite::BranchAuthors>& authors,
											   bool authorsInFlight)
	{
		Graphite::BranchListFrame frame = FrameOf(branches);
		frame.Authors = authors;
		frame.AuthorsInFlight = authorsInFlight;
		return frame;
	}

	ImVec2 IntoRow(const ImVec2& listTop, float rowHeight, float rowIndex)
	{
		const float halfARow = 0.5f;
		const float clearOfTheLeftEdge = 10.0f;
		return ImVec2(listTop.x + clearOfTheLeftEdge, listTop.y + rowHeight * (rowIndex + halfARow));
	}

	constexpr float kHalf = 0.5f;

	// WARNING: The header's widgets keep their hover state outside the ImGui context, by id, so
	// each test that draws it does so in a window of its own name.
	void DrawHeaderAndRows(Graphite::BranchListView& list, const Graphite::BranchListFrame& frame,
						   const char* windowName)
	{
		BeginNamedListWindow(windowName, kListWidth);
		list.DrawHeader(frame);
		list.DrawRows(frame);
		ImGui::End();
	}

	ImVec2 FilterBoxPoint()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float intoTheBox = 20.0f;
		return ImVec2(style.WindowPadding.x + intoTheBox, style.WindowPadding.y + ImGui::GetFrameHeight() * kHalf);
	}

	ImGuiID HoveredAt(ImGuiFrames& frames, const std::function<void()>& draw, ImVec2 at)
	{
		ImGui::GetIO().AddMousePosEvent(at.x, at.y);
		frames.Next(draw);
		return ImGui::GetCurrentContext()->HoveredId;
	}

	ImVec2 PanelButtonCentre()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		return ImVec2(kListWidth - style.WindowPadding.x - Core::IconButtonSize() * kHalf,
					  style.WindowPadding.y + ImGui::GetFrameHeight() * kHalf);
	}

	int OpenPopups()
	{
		return ImGui::GetCurrentContext()->OpenPopupStack.Size;
	}

	ImGuiWindow& PanelWindow()
	{
		ImGuiContext& context = *ImGui::GetCurrentContext();
		REQUIRE(context.OpenPopupStack.Size == 1);
		REQUIRE(context.OpenPopupStack[0].Window != nullptr);
		return *context.OpenPopupStack[0].Window;
	}

	std::optional<float> HeightWhereHovered(ImGuiFrames& frames, const std::function<void()>& draw, float probeLeft,
											ImGuiID item)
	{
		const float step = 2.0f;
		const float panelTop = PanelWindow().Pos.y;
		const float panelBottom = panelTop + PanelWindow().Size.y;

		for (float probeHeight = panelTop; probeHeight < panelBottom; probeHeight += step)
		{
			if (HoveredAt(frames, draw, ImVec2(probeLeft, probeHeight)) == item)
			{
				return probeHeight;
			}
		}

		return std::nullopt;
	}

	ImVec2 MiddleSortSegmentCentre()
	{
		const ImGuiWindow& panel = PanelWindow();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float contentWidth = panel.Size.x - style.WindowPadding.x * 2.0f;

		return ImVec2(panel.Pos.x + style.WindowPadding.x + contentWidth * kHalf,
					  panel.Pos.y + style.WindowPadding.y + ImGui::GetTextLineHeightWithSpacing() +
						  ImGui::GetFrameHeight() * kHalf);
	}

	const ImGuiWindow& AuthorListWindow()
	{
		const ImGuiWindow& panel = PanelWindow();
		const ImGuiWindow* authorList = nullptr;

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const bool isTheList = window->ParentWindow == &panel && (window->Flags & ImGuiWindowFlags_ChildWindow) != 0;

			if (isTheList)
			{
				authorList = window;
			}
		}

		REQUIRE(authorList != nullptr);
		return *authorList;
	}

	ImVec2 FirstAuthorTickBoxCentre()
	{
		const ImGuiWindow& authorList = AuthorListWindow();
		const float intoTheBox = 8.0f;
		return ImVec2(authorList.Pos.x + intoTheBox, authorList.Pos.y + ImGui::GetFrameHeight() * kHalf);
	}

	Core::Branch LocalBranchBy(const std::string& name, const std::string& authorName, std::size_t tipNumber)
	{
		Core::Branch branch = LocalBranchAt(name, kNowSeconds, tipNumber);
		branch.TipCommit->Author.Name = authorName;
		branch.TipCommit->Author.Email = authorName + "@example.com";
		return branch;
	}

	std::optional<float> LeftmostTextBetween(const ImGuiWindow& window, ImU32 textColour, float bandTop,
											 float bandBottom)
	{
		std::optional<float> leftmost;

		for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
		{
			const bool inBand = vertex.pos.y >= bandTop && vertex.pos.y < bandBottom;
			const bool isText = vertex.col == textColour;

			if (inBand && isText && (!leftmost.has_value() || vertex.pos.x < *leftmost))
			{
				leftmost = vertex.pos.x;
			}
		}

		return leftmost;
	}

	Core::Ref<const std::vector<Core::Branch>> BranchesByNumberedAuthors(std::size_t authorCount)
	{
		std::vector<Core::Branch> branches;

		for (std::size_t authorIndex = 0; authorIndex < authorCount; ++authorIndex)
		{
			branches.push_back(LocalBranchBy(std::format("branch-{}", authorIndex), std::format("U{}", authorIndex),
											 authorIndex + 1));
		}

		return Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches));
	}

	struct AuthorListLayout
	{
		bool Scrolls = false;
		float ContentRight = 0.0f;
		float RowsRight = 0.0f;
		float WindowPadding = 0.0f;
	};

	AuthorListLayout SettledAuthorList(std::size_t authorCount, const char* windowName)
	{
		ImGuiFrames frames;

		const bool notInFlight = false;
		const Graphite::BranchListFrame frame = FrameWithAuthors(
			BranchesByNumberedAuthors(authorCount), Core::CreateRef<const Graphite::BranchAuthors>(), notInFlight);

		Graphite::BranchListState options;
		Graphite::BranchListView list{options};

		const auto draw = [&list, &frame, windowName] { DrawHeaderAndRows(list, frame, windowName); };

		frames.Next(draw);
		frames.Click(PanelButtonCentre(), draw);
		frames.Next(draw);
		frames.Next(draw);
		REQUIRE(OpenPopups() == 1);

		const ImGuiWindow& authorList = AuthorListWindow();

		return AuthorListLayout{.Scrolls = authorList.ScrollbarY,
								.ContentRight = authorList.ContentRegionRect.Max.x,
								.RowsRight = authorList.DC.CursorMaxPos.x,
								.WindowPadding = ImGui::GetStyle().WindowPadding.x};
	}
}

TEST_CASE("Two thousand branches ask the badge only of the rows on screen")
{
	ImGuiFrames frames;
	std::vector<Core::Branch> branches;

	for (std::size_t branchNumber = 0; branchNumber < kManyBranches; ++branchNumber)
	{
		branches.push_back(LocalBranchAt(std::format("feature/PROJ-{}", branchNumber),
										 kNowSeconds - static_cast<long long>(branchNumber), branchNumber + 1));
	}

	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(branches);

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	int badgesAsked = 0;
	list.BadgeOf = [&badgesAsked](const Core::Branch&) {
		++badgesAsked;
		return std::string{};
	};

	for (int frameIndex = 0; frameIndex < kFramesToSettle; ++frameIndex)
	{
		badgesAsked = 0;
		frames.Next([&list, &listed] { DrawListWindow(list, FrameOf(listed), kListWidth); });
	}

	CHECK(badgesAsked > 0);
	CHECK(badgesAsked < 60);
}

TEST_CASE("A branch whose name holds a hash and a percent sign draws whole and reports its click")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{LocalBranchAt("fix/#123-release-100%", kNowSeconds, 1)});

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	std::optional<std::string> clicked;
	list.OnBranchClicked = [&clicked](const Core::Branch& branch) { clicked = branch.Name; };

	ImVec2 listTop{};
	float rowHeight = 0.0f;
	frames.Next([&list, &listed, &listTop, &rowHeight] {
		listTop = DrawListWindow(list, FrameOf(listed), kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const float intoTheSecondRow = 1.5f;
	const float clearOfTheLeftEdge = 10.0f;
	const ImVec2 firstBranchRow{listTop.x + clearOfTheLeftEdge, listTop.y + rowHeight * intoTheSecondRow};

	frames.Click(firstBranchRow, [&list, &listed] { DrawListWindow(list, FrameOf(listed), kListWidth); });

	REQUIRE(clicked.has_value());
	CHECK(*clicked == "fix/#123-release-100%");
}

TEST_CASE("A branch whose name holds a double hash draws its whole name")
{
	ImGuiFrames frames;

	Core::Branch doubleHash = LocalBranchAt("fix##2", kNowSeconds, 1);
	doubleHash.TipCommit.reset();

	const Core::Ref<const std::vector<Core::Branch>> listed =
		Core::CreateRef<const std::vector<Core::Branch>>(std::vector<Core::Branch>{doubleHash});

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	float drawnWidth = 0.0f;
	float nameWidth = 0.0f;
	list.DrawBranchMenu = [&drawnWidth, &nameWidth](const Core::Branch& branch) {
		drawnWidth = ImGui::GetItemRectSize().x;
		nameWidth = ImGui::CalcTextSize(branch.Name.c_str()).x;
	};

	frames.Next([&list, &listed] { DrawListWindow(list, FrameOf(listed), kListWidth); });

	CHECK(nameWidth > 0.0f);
	CHECK(drawnWidth == doctest::Approx(nameWidth));
}

TEST_CASE("The age ends where a list row's end stands, eight pixels in from the list's right edge")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = ListOf(LocalBranchAt("main", kNowSeconds, kTipBefore));

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	float rowEnd = 0.0f;
	float ageRight = 0.0f;
	list.DrawBranchMenu = [&rowEnd, &ageRight](const Core::Branch&) {
		rowEnd = Graphite::ListRowEnd();
		ageRight = ImGui::GetItemRectMax().x;
	};

	frames.Next([&list, &listed] { DrawListWindow(list, FrameOf(listed), kListWidth); });

	CHECK(ageRight > 0.0f);
	CHECK(ageRight == doctest::Approx(rowEnd));
}

TEST_CASE("Rows draw in a column narrower than the age without an assertion")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{LocalBranchAt("feature/a-very-long-branch-name", kNowSeconds - 3600, 1)});

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};
	list.BadgeOf = [](const Core::Branch&) { return std::string{"3/10"}; };

	const float narrowWidth = 30.0f;

	for (int frameIndex = 0; frameIndex < kFramesToSettle; ++frameIndex)
	{
		frames.Next([&list, &listed, narrowWidth] { DrawListWindow(list, FrameOf(listed), narrowWidth); });
	}

	CHECK(ImGui::GetFrameCount() == kFramesToSettle);
}

TEST_CASE("Every kind of row draws frame after frame without an assertion")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{LocalBranchAt("main", kNowSeconds, 1), LocalBranchAt("other", kNowSeconds - 60, 2)});

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	constexpr int kFramesPastTheLoaderDelay = 30;

	Graphite::BranchListFrame waiting = FrameOf(listed);

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &waiting] { DrawListWindow(list, waiting, kListWidth); });
	}

	Graphite::BranchListFrame failed = FrameOf(listed);
	failed.AuthorsFailure = "the walk stopped";
	frames.Next([&list, &failed] { DrawListWindow(list, failed, kListWidth); });

	Graphite::BranchListFrame grouped = FrameOf(listed);
	grouped.Authors = Core::CreateRef<const Graphite::BranchAuthors>();
	frames.Next([&list, &grouped] { DrawListWindow(list, grouped, kListWidth); });

	Graphite::BranchListFrame loading;
	loading.Now = SystemSecondsAt(kNowSeconds);

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &loading] { DrawListWindow(list, loading, kListWidth); });
	}

	const int singleFrames = 2;
	CHECK(ImGui::GetFrameCount() == 2 * kFramesPastTheLoaderDelay + singleFrames);
}

TEST_CASE("A branch whose tip cannot be read stays one row high")
{
	ImGuiFrames frames;

	Core::Branch unreadable = LocalBranchAt("a-unreadable", kNowSeconds, 1);
	unreadable.TipCommit.reset();

	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{unreadable, LocalBranchAt("b-readable", kNowSeconds, 2)});

	Graphite::BranchListState options;
	options.Sort = Graphite::BranchSort::Name;
	Graphite::BranchListView list{options};

	std::vector<float> rowTops;
	list.DrawBranchMenu = [&rowTops](const Core::Branch&) { rowTops.push_back(ImGui::GetItemRectMin().y); };

	float rowHeight = 0.0f;
	frames.Next([&list, &listed, &rowHeight] {
		DrawListWindow(list, FrameOf(listed), kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const std::size_t branchRows = 2;
	REQUIRE(rowTops.size() == branchRows);
	CHECK(rowTops.back() - rowTops.front() == doctest::Approx(rowHeight));
}

TEST_CASE("Each kind of row moves the list down by exactly one list row")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{LocalBranchAt("main", kNowSeconds, 1), LocalBranchAt("other", kNowSeconds - 60, 2)});

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};
	list.BadgeOf = [](const Core::Branch&) { return std::string{"3/10"}; };

	constexpr int kFramesPastTheLoaderDelay = 30;
	const float rowsAboveRemoteHeading = 2.0f;
	const float rowsAboveRemoteHeadingWithGroupOpen = 4.0f;

	MeasuredRows measured;

	const Graphite::BranchListFrame waiting = FrameOf(listed);
	frames.Next([&list, &waiting, &measured] { measured = DrawAndMeasureRows(list, waiting); });
	CHECK(RowsAboveLastItem(measured) == doctest::Approx(rowsAboveRemoteHeading));

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &waiting, &measured] { measured = DrawAndMeasureRows(list, waiting); });
	}

	CHECK(RowsAboveLastItem(measured) == doctest::Approx(rowsAboveRemoteHeading));

	Graphite::BranchListFrame failed = FrameOf(listed);
	failed.AuthorsFailure = "the walk stopped";
	frames.Next([&list, &failed, &measured] { measured = DrawAndMeasureRows(list, failed); });
	CHECK(RowsAboveLastItem(measured) == doctest::Approx(rowsAboveRemoteHeading));

	Graphite::BranchListFrame grouped = FrameOf(listed);
	grouped.Authors = Core::CreateRef<const Graphite::BranchAuthors>();
	frames.Next([&list, &grouped, &measured] { measured = DrawAndMeasureRows(list, grouped); });
	CHECK(RowsAboveLastItem(measured) == doctest::Approx(rowsAboveRemoteHeading));

	const float intoTheSecondRow = 1.5f;
	const float clearOfTheLeftEdge = 10.0f;
	const ImVec2 authorGroupRow{measured.ListTop.x + clearOfTheLeftEdge,
								measured.ListTop.y + measured.RowHeight * intoTheSecondRow};

	frames.Click(authorGroupRow, [&list, &grouped] { DrawAndMeasureRows(list, grouped); });
	frames.Next([&list, &grouped, &measured] { measured = DrawAndMeasureRows(list, grouped); });
	CHECK(RowsAboveLastItem(measured) == doctest::Approx(rowsAboveRemoteHeadingWithGroupOpen));
}

TEST_CASE("A branch list that failed waits again before its loader shows")
{
	ImGuiFrames frames;

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	Graphite::BranchListFrame loading;
	loading.Now = SystemSecondsAt(kNowSeconds);

	Graphite::BranchListFrame failed = loading;
	failed.BranchesFailed = true;

	constexpr int kFramesPastTheLoaderDelay = 30;
	MeasuredRows measured;

	frames.Next([&list, &loading, &measured] { measured = DrawAndMeasureRows(list, loading); });

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &failed, &measured] { measured = DrawAndMeasureRows(list, failed); });
	}

	frames.Next([&list, &loading, &measured] { measured = DrawAndMeasureRows(list, loading); });
	CHECK(measured.CursorTop == doctest::Approx(measured.ListTop.y));

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &loading, &measured] { measured = DrawAndMeasureRows(list, loading); });
	}

	CHECK(measured.CursorTop > measured.ListTop.y);
}

TEST_CASE("A branch list that lands while its authors are in flight keeps the rows built before it")
{
	ImGuiFrames frames;

	const Core::Branch featureBefore = LocalBranchAt("feature", kNowSeconds, kTipBefore);
	const Core::Branch featureMoved = LocalBranchAt("feature", kNowSeconds, kTipMoved);

	const bool inFlight = true;
	const bool notInFlight = false;

	const Graphite::BranchListFrame settled =
		FrameWithAuthors(ListOf(featureBefore), AuthorsWhereBobOwns(featureBefore), notInFlight);
	const Graphite::BranchListFrame held = FrameWithAuthors(ListOf(featureMoved), settled.Authors, inFlight);
	const Graphite::BranchListFrame landed =
		FrameWithAuthors(held.Branches, AuthorsWhereBobOwns(featureMoved), notInFlight);

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	std::vector<Core::Oid> drawnTips;
	list.DrawBranchMenu = [&drawnTips](const Core::Branch& branch) { drawnTips.push_back(branch.Tip); };

	ImVec2 listTop{};
	float rowHeight = 0.0f;
	frames.Next([&list, &settled, &listTop, &rowHeight] {
		listTop = DrawListWindow(list, settled, kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const float bobGroupRow = 1.0f;
	frames.Click(IntoRow(listTop, rowHeight, bobGroupRow),
				 [&list, &settled] { DrawListWindow(list, settled, kListWidth); });

	drawnTips.clear();
	frames.Next([&list, &settled] { DrawListWindow(list, settled, kListWidth); });
	REQUIRE(drawnTips == std::vector<Core::Oid>{featureBefore.Tip});

	drawnTips.clear();
	frames.Next([&list, &held] { DrawListWindow(list, held, kListWidth); });
	CHECK(drawnTips == std::vector<Core::Oid>{featureBefore.Tip});

	drawnTips.clear();
	frames.Next([&list, &landed] { DrawListWindow(list, landed, kListWidth); });
	CHECK(drawnTips == std::vector<Core::Oid>{featureMoved.Tip});
}

TEST_CASE("With an author's name typed a branch list that lands while authors are in flight keeps the rows built before it")
{
	ImGuiFrames frames;

	const Core::Branch featureBefore = LocalBranchAt("feature", kNowSeconds, kTipBefore);
	const Core::Branch featureMoved = LocalBranchAt("feature", kNowSeconds, kTipMoved);

	const bool inFlight = true;
	const bool notInFlight = false;

	const Graphite::BranchListFrame settled =
		FrameWithAuthors(ListOf(featureBefore), AuthorsWhereBobOwns(featureBefore), notInFlight);
	const Graphite::BranchListFrame held = FrameWithAuthors(ListOf(featureMoved), settled.Authors, inFlight);
	const Graphite::BranchListFrame landed =
		FrameWithAuthors(held.Branches, AuthorsWhereBobOwns(featureMoved), notInFlight);

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	std::vector<Core::Oid> drawnTips;
	list.DrawBranchMenu = [&drawnTips](const Core::Branch& branch) { drawnTips.push_back(branch.Tip); };

	const char* const windowName = "TypedAuthorWord";

	frames.Next([&list, &settled, windowName] { DrawHeaderAndRows(list, settled, windowName); });
	frames.Click(FilterBoxPoint(), [&list, &settled, windowName] { DrawHeaderAndRows(list, settled, windowName); });

	ImGui::GetIO().AddInputCharactersUTF8("bob");
	frames.Next([&list, &settled, windowName] { DrawHeaderAndRows(list, settled, windowName); });

	drawnTips.clear();
	frames.Next([&list, &settled, windowName] { DrawHeaderAndRows(list, settled, windowName); });
	REQUIRE(drawnTips == std::vector<Core::Oid>{featureBefore.Tip});

	drawnTips.clear();
	frames.Next([&list, &held, windowName] { DrawHeaderAndRows(list, held, windowName); });
	CHECK(drawnTips == std::vector<Core::Oid>{featureBefore.Tip});

	drawnTips.clear();
	frames.Next([&list, &landed, windowName] { DrawHeaderAndRows(list, landed, windowName); });
	CHECK(drawnTips == std::vector<Core::Oid>{featureMoved.Tip});
}

TEST_CASE("Without grouping a branch list that lands while authors are in flight shows at once")
{
	ImGuiFrames frames;

	const Core::Branch featureBefore = LocalBranchAt("feature", kNowSeconds, kTipBefore);
	const Core::Branch featureMoved = LocalBranchAt("feature", kNowSeconds, kTipMoved);

	const bool inFlight = true;
	const bool notInFlight = false;

	const Graphite::BranchListFrame settled =
		FrameWithAuthors(ListOf(featureBefore), AuthorsWhereBobOwns(featureBefore), notInFlight);
	const Graphite::BranchListFrame arrived = FrameWithAuthors(ListOf(featureMoved), settled.Authors, inFlight);

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	std::vector<Core::Oid> drawnTips;
	list.DrawBranchMenu = [&drawnTips](const Core::Branch& branch) { drawnTips.push_back(branch.Tip); };

	frames.Next([&list, &settled] { DrawListWindow(list, settled, kListWidth); });

	drawnTips.clear();
	frames.Next([&list, &arrived] { DrawListWindow(list, arrived, kListWidth); });
	CHECK(drawnTips == std::vector<Core::Oid>{featureMoved.Tip});
}

TEST_CASE("A section collapsed while authors are in flight rebuilds the rows at once")
{
	ImGuiFrames frames;

	const Core::Branch featureBefore = LocalBranchAt("feature", kNowSeconds, kTipBefore);
	const Core::Branch featureMoved = LocalBranchAt("feature", kNowSeconds, kTipMoved);

	const bool inFlight = true;
	const bool notInFlight = false;

	const Graphite::BranchListFrame settled =
		FrameWithAuthors(ListOf(featureBefore), AuthorsWhereBobOwns(featureBefore), notInFlight);
	const Graphite::BranchListFrame held = FrameWithAuthors(ListOf(featureMoved), settled.Authors, inFlight);

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	std::vector<Core::Oid> drawnTips;
	list.DrawBranchMenu = [&drawnTips](const Core::Branch& branch) { drawnTips.push_back(branch.Tip); };

	ImVec2 listTop{};
	float rowHeight = 0.0f;
	frames.Next([&list, &settled, &listTop, &rowHeight] {
		listTop = DrawListWindow(list, settled, kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const float localHeadingRow = 0.0f;
	const float bobGroupRow = 1.0f;
	frames.Click(IntoRow(listTop, rowHeight, bobGroupRow),
				 [&list, &settled] { DrawListWindow(list, settled, kListWidth); });

	frames.Next([&list, &settled] { DrawListWindow(list, settled, kListWidth); });

	drawnTips.clear();
	frames.Next([&list, &held] { DrawListWindow(list, held, kListWidth); });
	REQUIRE(drawnTips == std::vector<Core::Oid>{featureBefore.Tip});

	frames.Click(IntoRow(listTop, rowHeight, localHeadingRow),
				 [&list, &held] { DrawListWindow(list, held, kListWidth); });

	drawnTips.clear();
	frames.Next([&list, &held] { DrawListWindow(list, held, kListWidth); });
	CHECK(drawnTips.empty());
}

TEST_CASE("A branch inside an author group is highlighted from the list's inset left edge, not from its indent")
{
	ImGuiFrames frames;

	const bool notInFlight = false;
	const Graphite::BranchListFrame frame = FrameWithAuthors(
		ListOf(LocalBranchAt("Ann-work", kNowSeconds, 1)), Core::CreateRef<const Graphite::BranchAuthors>(), notInFlight);

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	ImVec2 listTop{};
	float rowHeight = 0.0f;
	frames.Next([&list, &frame, &listTop, &rowHeight] {
		listTop = DrawListWindow(list, frame, kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const float annGroupRow = 1.0f;
	const float annWorkRow = 2.0f;
	frames.Click(IntoRow(listTop, rowHeight, annGroupRow), [&list, &frame] { DrawListWindow(list, frame, kListWidth); });

	const ImVec2 onTheBranch = IntoRow(listTop, rowHeight, annWorkRow);
	ImGui::GetIO().AddMousePosEvent(onTheBranch.x, onTheBranch.y);

	// An item that allows overlap takes the hover on the frame after the pointer reached it.
	ImU32 hoverColour = 0;
	frames.Next([&list, &frame] { DrawListWindow(list, frame, kListWidth); });
	frames.Next([&list, &frame, &hoverColour] {
		DrawListWindow(list, frame, kListWidth);
		hoverColour = ImGui::GetColorU32(Graphite::CurrentListRowColours().Hovered);
	});

	const ImGuiWindow* listWindow = ImGui::FindWindowByName("BranchList");
	REQUIRE(listWindow != nullptr);

	float highlightLeft = FLT_MAX;
	for (const ImDrawVert& vertex : listWindow->DrawList->VtxBuffer)
	{
		if (vertex.col == hoverColour)
		{
			highlightLeft = std::min(highlightLeft, vertex.pos.x);
		}
	}

	const float listRowHighlightInset = 6.0f;
	const float antiAliasedFringe = 1.0f;
	CHECK(highlightLeft >= listWindow->InnerRect.Min.x + listRowHighlightInset - antiAliasedFringe);
	CHECK(highlightLeft <= listWindow->InnerRect.Min.x + listRowHighlightInset + antiAliasedFringe);
}

TEST_CASE("A branch inside an author group starts a chevron's width past the group's name")
{
	ImGuiFrames frames;

	const bool notInFlight = false;
	const Graphite::BranchListFrame frame = FrameWithAuthors(
		ListOf(LocalBranchAt("Ann-work", kNowSeconds, 1)), Core::CreateRef<const Graphite::BranchAuthors>(), notInFlight);

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	ImVec2 listTop{};
	float rowHeight = 0.0f;
	frames.Next([&list, &frame, &listTop, &rowHeight] {
		listTop = DrawListWindow(list, frame, kListWidth);
		rowHeight = Graphite::ListRowPitch();
	});

	const float annGroupRow = 1.0f;
	const float annWorkRow = 2.0f;
	frames.Click(IntoRow(listTop, rowHeight, annGroupRow), [&list, &frame] { DrawListWindow(list, frame, kListWidth); });

	ImU32 textColour = 0;
	float chevronWidth = 0.0f;
	frames.Next([&list, &frame, &textColour, &chevronWidth] {
		DrawListWindow(list, frame, kListWidth);
		textColour = ImGui::GetColorU32(ImGuiCol_Text);
		chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x;
	});

	const ImGuiWindow* listWindow = ImGui::FindWindowByName("BranchList");
	REQUIRE(listWindow != nullptr);

	const std::optional<float> groupNameLeft =
		LeftmostTextBetween(*listWindow, textColour, listTop.y + rowHeight * annGroupRow,
							listTop.y + rowHeight * (annGroupRow + 1.0f));
	const std::optional<float> branchLabelLeft =
		LeftmostTextBetween(*listWindow, textColour, listTop.y + rowHeight * annWorkRow,
							listTop.y + rowHeight * (annWorkRow + 1.0f));

	REQUIRE(groupNameLeft.has_value());
	REQUIRE(branchLabelLeft.has_value());
	CHECK(*branchLabelLeft > *groupNameLeft);
	CHECK(*branchLabelLeft - *groupNameLeft >= chevronWidth);
}

TEST_CASE("Choosing a sort in the panel leaves the panel open")
{
	ImGuiFrames frames;

	const Graphite::BranchListFrame frame = FrameOf(Core::CreateRef<const std::vector<Core::Branch>>(
		std::vector<Core::Branch>{LocalBranchAt("main", kNowSeconds, 1), LocalBranchAt("other", kNowSeconds - 60, 2)}));

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	const auto draw = [&list, &frame] { DrawHeaderAndRows(list, frame, "SortPanel"); };

	frames.Next(draw);
	frames.Click(PanelButtonCentre(), draw);
	frames.Next(draw);
	REQUIRE(OpenPopups() == 1);

	frames.Click(MiddleSortSegmentCentre(), draw);

	CHECK(options.Sort == Graphite::BranchSort::Oldest);
	CHECK(OpenPopups() == 1);
}

TEST_CASE("Ticking an author in the panel leaves the panel open and narrows the list")
{
	ImGuiFrames frames;

	const bool notInFlight = false;
	const Graphite::BranchListFrame frame = FrameWithAuthors(
		Core::CreateRef<const std::vector<Core::Branch>>(
			std::vector<Core::Branch>{LocalBranchBy("by-ann", "Ann", 1), LocalBranchBy("by-bob", "Bob", 2)}),
		Core::CreateRef<const Graphite::BranchAuthors>(), notInFlight);

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	std::vector<std::string> drawnBranches;
	list.DrawBranchMenu = [&drawnBranches](const Core::Branch& branch) { drawnBranches.push_back(branch.Name); };

	const auto draw = [&list, &frame] { DrawHeaderAndRows(list, frame, "TickPanel"); };

	frames.Next(draw);
	frames.Click(PanelButtonCentre(), draw);
	frames.Next(draw);
	REQUIRE(OpenPopups() == 1);

	frames.Click(FirstAuthorTickBoxCentre(), draw);

	drawnBranches.clear();
	frames.Next(draw);

	CHECK(OpenPopups() == 1);
	CHECK(drawnBranches == std::vector<std::string>{"by-ann"});
}

TEST_CASE("Author counts end a window padding short of the authors list's scrollbar")
{
	constexpr std::size_t kAuthorsPastTheListHeight = 14;

	const AuthorListLayout layout = SettledAuthorList(kAuthorsPastTheListHeight, "ScrolledCounts");

	REQUIRE(layout.Scrolls);
	CHECK(layout.RowsRight == doctest::Approx(layout.ContentRight - layout.WindowPadding));
}

TEST_CASE("Without a scrollbar author counts end at the panel's content edge")
{
	constexpr std::size_t kAuthorsThatFit = 3;

	const AuthorListLayout layout = SettledAuthorList(kAuthorsThatFit, "FittingCounts");

	REQUIRE_FALSE(layout.Scrolls);
	CHECK(layout.RowsRight == doctest::Approx(layout.ContentRight));
}

TEST_CASE("The Also commit message toggle ends at the right edge of the panel's content")
{
	ImGuiFrames frames;

	const bool notInFlight = false;
	const Graphite::BranchListFrame frame =
		FrameWithAuthors(Core::CreateRef<const std::vector<Core::Branch>>(
							 std::vector<Core::Branch>{LocalBranchBy("by-ann", "Ann", 1)}),
						 Core::CreateRef<const Graphite::BranchAuthors>(), notInFlight);

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	const auto draw = [&list, &frame] { DrawHeaderAndRows(list, frame, "TogglePanel"); };

	frames.Next(draw);
	frames.Click(PanelButtonCentre(), draw);
	frames.Next(draw);
	REQUIRE(OpenPopups() == 1);

	ImGuiWindow& panel = PanelWindow();
	const ImGuiID alsoMessage = panel.GetID("##alsoMessage");

	const float contentRight = panel.Pos.x + panel.Size.x - ImGui::GetStyle().WindowPadding.x;
	const float toggleMiddle = contentRight - Core::Scaled(Core::kToggleWidth) * kHalf;
	const float justInside = contentRight - 2.0f;
	const float justOutside = contentRight + 2.0f;

	const std::optional<float> alsoMessageHeight = HeightWhereHovered(frames, draw, toggleMiddle, alsoMessage);
	REQUIRE(alsoMessageHeight.has_value());

	CHECK(HoveredAt(frames, draw, ImVec2(justInside, *alsoMessageHeight)) == alsoMessage);
	CHECK(HoveredAt(frames, draw, ImVec2(justOutside, *alsoMessageHeight)) != alsoMessage);
}

TEST_CASE("The panel draws while authors are read and after they fail or land without an assertion")
{
	ImGuiFrames frames;

	constexpr std::size_t kAuthorsPastTheListHeight = 14;
	constexpr int kFramesPastTheLoaderDelay = 30;

	std::vector<Core::Branch> branches;

	for (std::size_t authorIndex = 0; authorIndex < kAuthorsPastTheListHeight; ++authorIndex)
	{
		branches.push_back(LocalBranchBy(std::format("branch-{}", authorIndex), std::format("U{}", authorIndex),
										 authorIndex + 1));
	}

	const Core::Ref<const std::vector<Core::Branch>> listed =
		Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches));

	Graphite::BranchAuthors answer;
	answer.UserEmail = "U3@example.com";

	const Graphite::BranchListFrame waiting = FrameOf(listed);

	Graphite::BranchListFrame failed = FrameOf(listed);
	failed.AuthorsFailure = "the walk stopped on an object that could not be read from the pack";

	const bool notInFlight = false;
	const Graphite::BranchListFrame landed =
		FrameWithAuthors(listed, Core::CreateRef<const Graphite::BranchAuthors>(std::move(answer)), notInFlight);

	Graphite::BranchListState options;
	options.Group = Graphite::BranchGrouping::Author;
	Graphite::BranchListView list{options};

	frames.Next([&list, &waiting] { DrawHeaderAndRows(list, waiting, "StatesPanel"); });
	frames.Click(PanelButtonCentre(), [&list, &waiting] { DrawHeaderAndRows(list, waiting, "StatesPanel"); });

	for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderDelay; ++frameIndex)
	{
		frames.Next([&list, &waiting] { DrawHeaderAndRows(list, waiting, "StatesPanel"); });
	}

	frames.Next([&list, &failed] { DrawHeaderAndRows(list, failed, "StatesPanel"); });
	frames.Next([&list, &failed] { DrawHeaderAndRows(list, failed, "StatesPanel"); });

	frames.Next([&list, &landed] { DrawHeaderAndRows(list, landed, "StatesPanel"); });
	frames.Next([&list, &landed] { DrawHeaderAndRows(list, landed, "StatesPanel"); });

	CHECK(OpenPopups() == 1);
}
