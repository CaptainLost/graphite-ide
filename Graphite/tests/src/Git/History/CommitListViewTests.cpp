#include <doctest/doctest.h>

#include "Git/History/CommitListView.h"
#include "UI/Lists/ListRow.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Core/Base/Base.h"
#include "Core/Git/GraphLayout.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

using namespace Graphite;

namespace {

		constexpr float kHistoryWidth = 900.0f;
	constexpr float kHistoryHeight = 600.0f;

	Core::CommitSummary CommitNumbered(int number)
	{
		Core::CommitSummary commit;
		commit.Id = *Core::Oid::FromHex(std::string(39, '0') + std::to_string(number));
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
				view.Render(page, graph, palette, std::chrono::minutes{0},
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
		view.Render(page, graph, palette, std::chrono::minutes{0},
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
		view.Render(page, graph, palette, std::chrono::minutes{0},
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
