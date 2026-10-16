#include <doctest/doctest.h>

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ToastStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <string>

namespace {
	struct InkExtent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	InkExtent ExtentInColour(const ImDrawList& drawing, int from, ImU32 colour)
	{
		InkExtent extent;
		for (int vertex = from; vertex < drawing.VtxBuffer.Size; ++vertex)
		{
			const ImDrawVert& drawn = drawing.VtxBuffer[vertex];
			if (drawn.col == colour)
			{
				extent.Left = std::min(extent.Left, drawn.pos.x);
				extent.Right = std::max(extent.Right, drawn.pos.x);
				extent.Bottom = std::max(extent.Bottom, drawn.pos.y);
			}
		}
		return extent;
	}

	class OneFrame
	{
	public:
		OneFrame()
		{
			m_Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~OneFrame() { ImGui::DestroyContext(m_Context); }

		OneFrame(const OneFrame&) = delete;
		OneFrame& operator=(const OneFrame&) = delete;

		void Draw(const std::function<void(ImDrawList&)>& draw)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
			ImGui::Begin("Card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			draw(*ImGui::GetWindowDrawList());
			ImGui::End();
			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	Core::ToastPalette DistinctPalette()
	{
		return {
			.Surface = {.R = 0.1f},
			.Text = {.R = 1.0f, .G = 1.0f, .B = 1.0f},
			.TextDim = {.R = 0.5f, .G = 0.5f, .B = 0.5f},
			.Success = {.G = 1.0f},
			.Warning = {.R = 1.0f, .G = 1.0f},
			.Error = {.R = 1.0f},
		};
	}
}

TEST_CASE("Each toast kind takes its accent from the palette's colour for that kind")
{
	const Core::ToastPalette palette = DistinctPalette();

	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Success) == &palette.Success);
	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Warning) == &palette.Warning);
	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Error) == &palette.Error);
}

TEST_CASE("A notification's title cut short ends within its width, and its count follows it dim")
{
	OneFrame frame;
	const Core::ToastPalette palette = DistinctPalette();

	frame.Draw([&palette](ImDrawList& drawing) {
		const float left = 100.0f;
		const float width = 120.0f;
		const int from = drawing.VtxBuffer.Size;

		Core::DrawNotificationTitle(drawing, ImVec2(left, 50.0f), width, std::string(80, 'T'), "(x3)",
									Core::NotificationTitleFit::Ellipsis, palette);

		const InkExtent title = ExtentInColour(drawing, from, ImGui::GetColorU32(Core::ToImVec4(palette.Text)));
		const InkExtent count = ExtentInColour(drawing, from, ImGui::GetColorU32(Core::ToImVec4(palette.TextDim)));
		REQUIRE(title.Right > title.Left);
		REQUIRE(count.Right > count.Left);

		CHECK(title.Right <= left + width);
		CHECK(count.Left > title.Right);
		CHECK(count.Right <= left + width + 1.0f);
		CHECK(Core::NotificationTitleHeight(std::string(80, 'T'), "(x3)", width, Core::NotificationTitleFit::Ellipsis) ==
			  doctest::Approx(ImGui::GetFontSize()));
	});
}

TEST_CASE("A notification's title that wraps takes as many lines as it wraps to")
{
	OneFrame frame;
	const Core::ToastPalette palette = DistinctPalette();

	frame.Draw([&palette](ImDrawList& drawing) {
		const std::string title = "Cannot read the solution file because it is broken at line four";
		const float width = 120.0f;
		const float height = Core::NotificationTitleHeight(title, "", width, Core::NotificationTitleFit::Wrap);
		const int from = drawing.VtxBuffer.Size;

		Core::DrawNotificationTitle(drawing, ImVec2(100.0f, 50.0f), width, title, "", Core::NotificationTitleFit::Wrap, palette);

		const InkExtent drawn = ExtentInColour(drawing, from, ImGui::GetColorU32(Core::ToImVec4(palette.Text)));
		REQUIRE(drawn.Right > drawn.Left);

		CHECK(height > ImGui::GetFontSize() * 2.0f);
		CHECK(drawn.Right <= 100.0f + width);
		CHECK(drawn.Bottom <= 50.0f + height + 1.0f);
		CHECK(drawn.Bottom > 50.0f + height - ImGui::GetFontSize());
	});
}

TEST_CASE("A notification's icon is drawn centred on its first line of text and clear of the text after it")
{
	OneFrame frame;
	const Core::ToastPalette palette = DistinctPalette();

	frame.Draw([&palette](ImDrawList& drawing) {
		const float textLeft = 200.0f;
		const float firstLineTop = 40.0f;
		const int from = drawing.VtxBuffer.Size;

		Core::DrawToastIcon(drawing, Core::ToastKind::Error, Core::NotificationIconCentre(textLeft, firstLineTop), palette);

		float top = FLT_MAX;
		float bottom = -FLT_MAX;
		float right = -FLT_MAX;
		for (int vertex = from; vertex < drawing.VtxBuffer.Size; ++vertex)
		{
			top = std::min(top, drawing.VtxBuffer[vertex].pos.y);
			bottom = std::max(bottom, drawing.VtxBuffer[vertex].pos.y);
			right = std::max(right, drawing.VtxBuffer[vertex].pos.x);
		}

		REQUIRE(bottom > top);
		const float lineMiddle = firstLineTop + ImGui::GetFontSize() * 0.5f;
		CHECK(std::abs((top + bottom) * 0.5f - lineMiddle) <= 1.0f);
		CHECK(right <= textLeft - Core::NotificationIconGap() + 1.0f);
	});
}

TEST_CASE("A count after a title that wraps sits after the widest of its lines, not at the column's edge")
{
	OneFrame frame;
	const Core::ToastPalette palette = DistinctPalette();

	frame.Draw([&palette](ImDrawList& drawing) {
		const std::string title = "Abc defghijklmn";
		const float width = 140.0f;
		REQUIRE(Core::NotificationTitleHeight(title, "(x2)", width, Core::NotificationTitleFit::Wrap) > ImGui::GetFontSize());
		const int from = drawing.VtxBuffer.Size;

		Core::DrawNotificationTitle(drawing, ImVec2(0.0f, 0.0f), width, title, "(x2)", Core::NotificationTitleFit::Wrap, palette);

		const InkExtent text = ExtentInColour(drawing, from, ImGui::GetColorU32(Core::ToImVec4(palette.Text)));
		const InkExtent count = ExtentInColour(drawing, from, ImGui::GetColorU32(Core::ToImVec4(palette.TextDim)));
		REQUIRE(text.Right > text.Left);
		REQUIRE(count.Right > count.Left);

		CHECK(count.Left - text.Right <= ImGui::GetStyle().ItemSpacing.x + 3.0f);
	});
}

TEST_CASE("A toast's message sits most of the way from the dim text towards the full text")
{
	const Core::Color message = Core::ToastMessageColour(DistinctPalette());

	CHECK(message.R == doctest::Approx(0.5f + 0.5f * 0.85f));
	CHECK(message.A == doctest::Approx(1.0f));
}
