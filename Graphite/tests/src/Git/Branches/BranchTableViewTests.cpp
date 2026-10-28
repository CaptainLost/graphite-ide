#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Branches/BranchTableView.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr long long kNowSeconds = 1700000000;
	constexpr float kWindowWidth = 700.0f;
	constexpr float kWindowHeight = 400.0f;
	constexpr float kStep = 3.0f;

	Core::Branch LocalBranch(const std::string& name, std::uint8_t tip)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Tip.Bytes[0] = tip;

		Core::CommitSummary commit;
		commit.Id = branch.Tip;
		commit.Summary = "Work on " + name;
		commit.Author = Core::Signature{.Name = "Ann", .Email = "ann@example.com", .When = SystemSecondsAt(kNowSeconds)};
		commit.Committer = commit.Author;
		branch.TipCommit = commit;

		return branch;
	}

	void DrawTableWindow(Graphite::BranchTableView& view, const std::vector<Core::Branch>& branches)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight));
		ImGui::Begin("BranchTable", nullptr,
					 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
						 ImGuiWindowFlags_NoSavedSettings);

		Graphite::BranchTableInput input;
		input.Branches = &branches;
		input.Now = SystemSecondsAt(kNowSeconds);

		view.Draw(std::move(input), std::chrono::locate_zone("UTC"));

		ImGui::End();
	}
}

TEST_CASE("Clicking a row opens that branch")
{
	ImGuiFrames frames;

	const std::vector<Core::Branch> branches = {LocalBranch("side", 1)};

	Graphite::BranchTableView view;
	std::optional<ImGuiID> rowId;
	std::vector<std::string> clicked;

	view.DrawBranchMenu = [&](const Core::Branch&) { rowId = ImGui::GetItemID(); };
	view.OnBranchClicked = [&](const Core::Branch& branch) { clicked.push_back(branch.Name); };

	const std::function<void()> draw = [&] { DrawTableWindow(view, branches); };

	frames.Next(draw);
	frames.Next(draw);
	REQUIRE(rowId.has_value());

	std::optional<float> rowY;

	for (float y = 0.0f; y < kWindowHeight && !rowY.has_value(); y += kStep)
	{
		ImGui::GetIO().AddMousePosEvent(40.0f, y);
		frames.Next(draw);

		if (ImGui::GetCurrentContext()->HoveredId == *rowId)
		{
			rowY = y;
		}
	}

	REQUIRE(rowY.has_value());
	frames.Click(ImVec2(40.0f, *rowY), draw);

	REQUIRE(clicked.size() == 1);
	CHECK(clicked[0] == "side");
}
