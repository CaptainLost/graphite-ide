#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/NavigatorView.h"
#include "Theme/ActiveScheme.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/ContextMenu.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <utility>
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

TEST_CASE("The state dot is green to fast-forward, amber behind the trunk and grey otherwise, in the dark and the light scheme")
{
	for (const ResolvedScheme& scheme : {DarkScheme(), LightScheme()})
	{
		CHECK(WorkStateDotColour(WorkState::CanFastForward, scheme) == ReadGitPalette(scheme).GutterAdded);
		CHECK(WorkStateDotColour(WorkState::BehindTrunk, scheme) == ReadEditorPalette(scheme).DiagnosticWarning);
		CHECK(WorkStateDotColour(WorkState::BehindTrunk, scheme) != ReadGitPalette(scheme).GutterModified);
		CHECK(WorkStateDotColour(WorkState::Other, scheme) == ReadGitPalette(scheme).LineNumber);
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

TEST_CASE("A menu opened on a branch row still names that branch after the rows reorder")
{
	ImGuiFrames frames;
	const ActiveScheme scheme;
	NavigatorView view(scheme);

	std::optional<std::string> menuOf;
	view.DrawBranchMenu = [&](const Core::Branch& branch) {
		if (Core::BeginRightClickPopup("ref-menu"))
		{
			menuOf = branch.FullName;
			ImGui::EndPopup();
		}
	};

	Core::Branch first;
	first.Name = "first";
	first.FullName = "refs/heads/first";
	Core::Branch second;
	second.Name = "second";
	second.FullName = "refs/heads/second";
	const std::vector<Core::Branch> branches = {first, second};

	std::vector<NavigatorRow> rows = {
		{.Kind = NavigatorRowKind::Branch, .Label = "first", .Index = 0, .Branch = "refs/heads/first"},
		{.Kind = NavigatorRowKind::Branch, .Label = "second", .Index = 1, .Branch = "refs/heads/second"},
	};

	ImVec2 listTop;
	const auto draw = [&] { listTop = DrawWindow(view, rows, branches); };

	frames.Next(draw);
	frames.Next(draw);
	frames.Click(RowCentre(listTop, 0), draw, ImGuiMouseButton_Right);

	REQUIRE(menuOf == std::optional<std::string>{"refs/heads/first"});

	std::swap(rows[0], rows[1]);
	menuOf.reset();
	frames.Next(draw);

	CHECK(menuOf == std::optional<std::string>{"refs/heads/first"});
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
