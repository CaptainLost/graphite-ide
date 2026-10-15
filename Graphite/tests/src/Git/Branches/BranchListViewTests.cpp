#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Branches/BranchListView.h"

#include "Core/Base/Base.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <format>
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

	void BeginListWindow(float width)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, kListHeight));
		ImGui::Begin("BranchList", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
												ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
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
		float LastItemTop = 0.0f;
		float CursorTop = 0.0f;
		float RowHeight = 0.0f;
	};

	MeasuredRows DrawAndMeasureRows(Graphite::BranchListView& list, const Graphite::BranchListFrame& frame)
	{
		BeginListWindow(kListWidth);

		MeasuredRows measured;
		measured.ListTop = ImGui::GetCursorScreenPos();

		list.DrawRows(frame);

		measured.LastItemTop = ImGui::GetItemRectMin().y;
		measured.CursorTop = ImGui::GetCursorScreenPos().y;
		measured.RowHeight = ImGui::GetTextLineHeightWithSpacing();

		ImGui::End();
		return measured;
	}

	float RowsAboveLastItem(const MeasuredRows& measured)
	{
		return (measured.LastItemTop - measured.ListTop.y) / measured.RowHeight;
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
		rowHeight = ImGui::GetTextLineHeightWithSpacing();
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

TEST_CASE("The age ends a window padding short of the row's right edge as the label starts that far in")
{
	ImGuiFrames frames;
	const Core::Ref<const std::vector<Core::Branch>> listed = ListOf(LocalBranchAt("main", kNowSeconds, kTipBefore));

	Graphite::BranchListState options;
	Graphite::BranchListView list{options};

	float rowRight = 0.0f;
	float ageRight = 0.0f;
	list.DrawBranchMenu = [&rowRight, &ageRight](const Core::Branch&) {
		rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		ageRight = ImGui::GetItemRectMax().x;
	};

	frames.Next([&list, &listed] { DrawListWindow(list, FrameOf(listed), kListWidth); });

	CHECK(ageRight > 0.0f);
	CHECK(rowRight - ageRight == doctest::Approx(ImGui::GetStyle().WindowPadding.x));
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
		rowHeight = ImGui::GetTextLineHeightWithSpacing();
	});

	const std::size_t branchRows = 2;
	REQUIRE(rowTops.size() == branchRows);
	CHECK(rowTops.back() - rowTops.front() == doctest::Approx(rowHeight));
}

TEST_CASE("Each kind of row moves the list down by exactly one text row")
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
		rowHeight = ImGui::GetTextLineHeightWithSpacing();
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
		rowHeight = ImGui::GetTextLineHeightWithSpacing();
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
