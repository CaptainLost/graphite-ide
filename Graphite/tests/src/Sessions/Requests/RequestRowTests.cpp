#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Sessions/Requests/RequestRow.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <optional>

using namespace Graphite;

namespace {

	constexpr float kFringe = 1.5f;
	constexpr float kHighlightInset = 6.0f;
	constexpr float kItemsPadding = 8.0f;
	constexpr std::size_t kFirstItem = 0;
	constexpr std::size_t kSecondItem = 1;
	constexpr std::optional<std::size_t> kNothingToReveal;

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<Extent> extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			if (!extent.has_value())
			{
				extent = Extent{};
			}

			extent->Left = std::min(extent->Left, vertex.pos.x);
			extent->Right = std::max(extent->Right, vertex.pos.x);
			extent->Top = std::min(extent->Top, vertex.pos.y);
			extent->Bottom = std::max(extent->Bottom, vertex.pos.y);
		}

		return extent;
	}

	bool WithinFringe(float drawn, float expected)
	{
		return std::abs(drawn - expected) <= kFringe;
	}

	ApiRequest RequestWithTwoSentItems()
	{
		ApiRequest request;
		request.Index = 1;
		request.Sent.push_back(SentItem{.Kind = SentKind::Prompt, .Label = "Fix the build", .Bytes = 13});
		request.Sent.push_back(SentItem{.Kind = SentKind::Reminder, .Label = "A reminder", .Bytes = 10});
		request.Response.push_back(ResponseItem{.Kind = ResponseKind::Text, .Label = "Done"});
		return request;
	}

	// The request's items as the details pane holds them: a child padded at its sides only.
	struct DetailItemsScreen
	{
		ImGuiFrames Frames;
		ImRect Items;
		float Pitch = 0.0f;
		ApiRequest Request = RequestWithTwoSentItems();
		SessionPalette Palette;

		DetailItemsScreen() { ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize; }

		void Draw(std::optional<std::size_t> selected)
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
			ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kItemsPadding, 0.0f));
			ImGui::BeginChild("##detailItems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleVar();

			{
				const ListBodyScope listBody;
				Items = ImGui::GetCurrentWindowRead()->InnerRect;
				Pitch = ListRowPitch();
				DrawRequestDetails(Request, Palette, selected, kNothingToReveal);
			}

			ImGui::EndChild();
			ImGui::End();
		}

		void DrawFrame(std::optional<std::size_t> selected)
		{
			Frames.Next([this, selected] { Draw(selected); });
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* details = ImGui::FindWindowByName("Details");
			REQUIRE(details != nullptr);
			REQUIRE(!details->DC.ChildWindows.empty());
			return *details->DC.ChildWindows.back()->DrawList;
		}
	};
}

TEST_CASE("A request's items are list rows, a pitch apart, highlighted inset in the list colours")
{
	DetailItemsScreen screen;
	const ImU32 selectedColour = ImGui::GetColorU32(CurrentListRowColours().Selected);

	screen.DrawFrame(kFirstItem);
	const std::optional<Extent> first = ExtentInColour(DetailItemsScreen::DrawList(), selectedColour);

	screen.DrawFrame(kSecondItem);
	const std::optional<Extent> second = ExtentInColour(DetailItemsScreen::DrawList(), selectedColour);

	REQUIRE(first.has_value());
	REQUIRE(second.has_value());

	CHECK(WithinFringe(first->Bottom - first->Top, screen.Pitch));
	CHECK(WithinFringe(second->Top - first->Top, screen.Pitch));
	CHECK(WithinFringe(first->Left, screen.Items.Min.x + kHighlightInset));
	CHECK(WithinFringe(first->Right, screen.Items.Max.x - kHighlightInset));
	CHECK_FALSE(DrawsInColour(DetailItemsScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Header)));
}
