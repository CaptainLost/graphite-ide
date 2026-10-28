#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/NavigatorView.h"
#include "Theme/ActiveScheme.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace {

	using namespace Graphite;

	constexpr float kWidth = 300.0f;
	constexpr float kHeight = 400.0f;

	// The row's vertical middle, found from the list's top as the rows lay out.
	ImVec2 RowCentre(ImVec2 listTop, std::size_t rowIndex)
	{
		return ImVec2(listTop.x + 40.0f, listTop.y + (static_cast<float>(rowIndex) + 0.5f) * ListRowPitch());
	}

	ImVec2 DrawWindow(NavigatorView& view, const std::vector<NavigatorRow>& rows, const std::vector<Core::Branch>& branches)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
		ImGui::Begin("Navigator", nullptr,
					 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
						 ImGuiWindowFlags_NoSavedSettings);

		const ImVec2 listTop = ImGui::GetCursorScreenPos();

		{
			const ListBodyScope listBody;
			view.Draw(rows, branches);
		}

		ImGui::End();
		return listTop;
	}
}

TEST_CASE("Clicking a thread row selects its thread")
{
	ImGuiFrames frames;
	const ActiveScheme scheme;
	NavigatorView view(scheme);

	std::optional<GitWindowSelection> selected;
	view.OnSelect = [&](const GitWindowSelection& selection) { selected = selection; };

	const std::vector<NavigatorRow> rows = {
		{.Kind = NavigatorRowKind::Work, .Label = "Work"},
		{.Kind = NavigatorRowKind::Thread, .Label = "topic", .Index = 1, .Branch = "refs/heads/topic"},
		{.Kind = NavigatorRowKind::Changes, .Label = "Changes"},
	};
	const std::vector<Core::Branch> branches;

	ImVec2 listTop;
	const auto draw = [&] { listTop = DrawWindow(view, rows, branches); };

	frames.Next(draw);
	frames.Next(draw);
	frames.Click(RowCentre(listTop, 1), draw);

	REQUIRE(selected.has_value());
	CHECK(*selected == GitWindowSelection{GitWindowView::Thread, "refs/heads/topic"});

	selected.reset();
	frames.Click(RowCentre(listTop, 2), draw);

	REQUIRE(selected.has_value());
	CHECK(selected->View == GitWindowView::Changes);
}

TEST_CASE("Clicking a branch row opens its history")
{
	ImGuiFrames frames;
	const ActiveScheme scheme;
	NavigatorView view(scheme);

	std::optional<GitWindowSelection> selected;
	view.OnSelect = [&](const GitWindowSelection& selection) { selected = selection; };

	Core::Branch topic;
	topic.Name = "topic";
	topic.FullName = "refs/heads/topic";
	const std::vector<Core::Branch> branches = {topic};

	const std::vector<NavigatorRow> rows = {
		{.Kind = NavigatorRowKind::BranchesHeading, .Label = "Branches"},
		{.Kind = NavigatorRowKind::Branch, .Label = "topic", .Index = 0, .Branch = "refs/heads/topic"},
	};

	ImVec2 listTop;
	const auto draw = [&] { listTop = DrawWindow(view, rows, branches); };

	frames.Next(draw);
	frames.Next(draw);
	frames.Click(RowCentre(listTop, 1), draw);

	REQUIRE(selected.has_value());
	CHECK(*selected == GitWindowSelection{GitWindowView::History, "refs/heads/topic"});
}
