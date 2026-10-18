#include <doctest/doctest.h>

#include "Git/History/CommitListView.h"
#include "UI/Lists/ListRow.h"

#include "ImGuiFrames.h"

#include "Core/Base/Base.h"
#include "Core/Git/GraphLayout.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
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
