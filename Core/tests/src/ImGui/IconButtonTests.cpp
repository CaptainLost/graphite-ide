#include <doctest/doctest.h>

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/IconButton.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

namespace {

	class ButtonWindow
	{
	public:
		ButtonWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();
		}

		~ButtonWindow() { ImGui::DestroyContext(m_Context); }

		ButtonWindow(const ButtonWindow&) = delete;
		ButtonWindow& operator=(const ButtonWindow&) = delete;

		struct Drawn
		{
			ImRect Button;
			float RowTop = 0.0f;
			float Advance = 0.0f;
			int Vertices = 0;
			float FollowerTop = 0.0f;
		};

		Drawn Draw(Core::IconButtonFit fit, bool engaged = false, bool followedOnTheLine = false)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::Begin("Buttons", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			Drawn drawn;
			drawn.RowTop = ImGui::GetCursorScreenPos().y;
			const float before = ImGui::GetCursorPosY();
			const int verticesBefore = ImGui::GetWindowDrawList()->VtxBuffer.Size;

			Core::DrawIconButton("##button", "x", engaged, "Do it", {.Fit = fit});

			drawn.Button = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			if (followedOnTheLine)
			{
				ImGui::SameLine();
				ImGui::Button("after");
				drawn.FollowerTop = ImGui::GetItemRectMin().y;
			}

			drawn.Advance = ImGui::GetCursorPosY() - before;
			drawn.Vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - verticesBefore;

			ImGui::End();
			ImGui::EndFrame();

			return drawn;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("A line icon button is one line of text high, so a list row does not grow for it")
{
	ButtonWindow window;

	const ButtonWindow::Drawn drawn = window.Draw(Core::IconButtonFit::Line);

	CHECK(drawn.Button.GetHeight() == doctest::Approx(ImGui::GetTextLineHeight()));
	CHECK(drawn.Button.GetWidth() == doctest::Approx(drawn.Button.GetHeight()));
	CHECK(drawn.Button.Min.y == doctest::Approx(drawn.RowTop));
	CHECK(drawn.Advance == doctest::Approx(ImGui::GetTextLineHeightWithSpacing()));
}

TEST_CASE("A frame icon button is a square a little taller than a line, centred in a row one frame high")
{
	ButtonWindow window;

	const ButtonWindow::Drawn drawn = window.Draw(Core::IconButtonFit::Frame);
	const float frameHeight = ImGui::GetFrameHeight();

	CHECK(drawn.Button.GetHeight() > ImGui::GetTextLineHeight());
	CHECK(drawn.Button.GetCenter().y == doctest::Approx(drawn.RowTop + frameHeight * 0.5f).epsilon(0.05));
}

TEST_CASE("A frame icon button leaves the line's top where it was, so what follows it on the line starts there too")
{
	ButtonWindow window;

	const bool followedOnTheLine = true;
	const ButtonWindow::Drawn drawn = window.Draw(Core::IconButtonFit::Frame, false, followedOnTheLine);

	CHECK(drawn.FollowerTop == doctest::Approx(drawn.RowTop));
	CHECK(drawn.Advance == doctest::Approx(ImGui::GetFrameHeightWithSpacing()));
}

TEST_CASE("A bar icon button is a square one frame high from the row's top, filled out to its corners")
{
	constexpr int kVerticesInAGlyph = 4;
	constexpr int kVerticesInASquareCorneredFill = 4;
	constexpr float kRoundedFrames = 3.0f;

	ButtonWindow window;
	ImGui::GetStyle().FrameRounding = kRoundedFrames;

	const bool engaged = true;
	const ButtonWindow::Drawn drawn = window.Draw(Core::IconButtonFit::Bar, engaged);

	CHECK(drawn.Button.GetHeight() == doctest::Approx(ImGui::GetFrameHeight()));
	CHECK(drawn.Button.GetWidth() == doctest::Approx(drawn.Button.GetHeight()));
	CHECK(drawn.Button.Min.y == doctest::Approx(drawn.RowTop));
	CHECK(drawn.Advance == doctest::Approx(ImGui::GetFrameHeightWithSpacing()));
	CHECK(drawn.Vertices == kVerticesInAGlyph + kVerticesInASquareCorneredFill);
}

TEST_CASE("An icon button at rest draws only its glyph, and an engaged one draws a fill behind it")
{
	ButtonWindow window;

	const int atRest = window.Draw(Core::IconButtonFit::Line).Vertices;
	const int engaged = window.Draw(Core::IconButtonFit::Line, true).Vertices;

	CHECK(engaged > atRest);
}

namespace {

	constexpr float kBoxSide = 24.0f;
	constexpr float kBoxGlyphSize = 15.0f;
	constexpr float kBoxRounding = 5.0f;
	constexpr float kFringe = 1.0f;
	constexpr float kOnTheTopEdge = 0.01f;
	constexpr int kFramesPastTheHoverFade = 30;
	constexpr const char* kGlyph = "x";
	constexpr const char* kBoxWindowName = "Box";

	struct Ink
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Ink> InkInColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<Ink> ink;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			if (!ink.has_value())
			{
				ink = Ink{};
			}

			ink->Left = std::min(ink->Left, vertex.pos.x);
			ink->Right = std::max(ink->Right, vertex.pos.x);
			ink->Top = std::min(ink->Top, vertex.pos.y);
			ink->Bottom = std::max(ink->Bottom, vertex.pos.y);
		}

		return ink;
	}

	// One icon button in a window of its own, drawn frame after frame with the pointer where it was last put.
	class BoxWindow
	{
	public:
		BoxWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();
		}

		~BoxWindow() { ImGui::DestroyContext(m_Context); }

		BoxWindow(const BoxWindow&) = delete;
		BoxWindow& operator=(const BoxWindow&) = delete;

		ImRect Draw(const Core::IconButtonOptions& options, bool engaged = false)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::Begin(kBoxWindowName, nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			Core::DrawIconButton("##box", kGlyph, engaged, Tooltip, options);
			const ImRect button(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImGui::End();
			ImGui::EndFrame();

			return button;
		}

		ImRect DrawHovered(const Core::IconButtonOptions& options, bool engaged = false)
		{
			const ImRect atRest = Draw(options, engaged);
			ImGui::GetIO().AddMousePosEvent(atRest.GetCenter().x, atRest.GetCenter().y);

			ImRect button = atRest;

			for (int frame = 0; frame < kFramesPastTheHoverFade; ++frame)
			{
				button = Draw(options, engaged);
			}

			return button;
		}

		static const ImDrawList& Drawing()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(kBoxWindowName);
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}

		static bool ShowsATooltip()
		{
			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				if ((window->Flags & ImGuiWindowFlags_Tooltip) != 0 && window->Active)
				{
					return true;
				}
			}

			return false;
		}

		const char* Tooltip = "Do it";

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("A box icon button is a twenty-four pixel square, whatever the font")
{
	BoxWindow window;

	const ImRect button = window.Draw({.Fit = Core::IconButtonFit::Box});

	CHECK(Core::IconButtonSize(Core::IconButtonFit::Box) == doctest::Approx(kBoxSide));
	CHECK(button.GetWidth() == doctest::Approx(kBoxSide));
	CHECK(button.GetHeight() == doctest::Approx(kBoxSide));
}

TEST_CASE("A box icon button draws its glyph at fifteen pixels in the middle of its square")
{
	constexpr double kWithinAPixelInTwenty = 0.05;

	BoxWindow window;

	window.Draw({.Fit = Core::IconButtonFit::Frame});
	const std::optional<Ink> atTheFontsSize = InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text));
	const float fontSize = ImGui::GetFontSize();

	const ImRect button = window.Draw({.Fit = Core::IconButtonFit::Box});
	const std::optional<Ink> glyph = InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text));

	REQUIRE(atTheFontsSize.has_value());
	REQUIRE(glyph.has_value());
	CHECK(glyph->Bottom - glyph->Top ==
		  doctest::Approx((atTheFontsSize->Bottom - atTheFontsSize->Top) * kBoxGlyphSize / fontSize).epsilon(kWithinAPixelInTwenty));
	CHECK(glyph->Right - glyph->Left ==
		  doctest::Approx((atTheFontsSize->Right - atTheFontsSize->Left) * kBoxGlyphSize / fontSize).epsilon(kWithinAPixelInTwenty));
	CHECK(std::abs((glyph->Left + glyph->Right) * 0.5f - button.GetCenter().x) <= kFringe);
	CHECK(std::abs((glyph->Top + glyph->Bottom) * 0.5f - button.GetCenter().y) <= kFringe + kFringe);
}

TEST_CASE("A box icon button under the pointer is filled in the hover colour, rounded five pixels whatever the frames")
{
	BoxWindow window;
	ImGui::GetStyle().FrameRounding = 0.0f;

	const ImRect button = window.DrawHovered({.Fit = Core::IconButtonFit::Box});
	const std::optional<Ink> fill = InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_ButtonHovered));

	REQUIRE(fill.has_value());
	CHECK(std::abs(fill->Left - button.Min.x) <= kFringe);
	CHECK(std::abs(fill->Right - button.Max.x) <= kFringe);

	float topEdgeLeft = FLT_MAX;

	for (const ImDrawVert& vertex : BoxWindow::Drawing().VtxBuffer)
	{
		if (vertex.col == ImGui::GetColorU32(ImGuiCol_ButtonHovered) && vertex.pos.y <= fill->Top + kOnTheTopEdge)
		{
			topEdgeLeft = std::min(topEdgeLeft, vertex.pos.x);
		}
	}

	CHECK(std::abs(topEdgeLeft - (button.Min.x + kBoxRounding)) <= kFringe);
}

TEST_CASE("An icon button asked to rest dim draws its glyph in the disabled-text colour until the pointer is over it")
{
	BoxWindow window;
	const Core::IconButtonOptions quiet{.Fit = Core::IconButtonFit::Box, .Rest = Core::IconButtonRest::Dim};

	window.Draw(quiet);
	CHECK(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_TextDisabled)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text)).has_value());

	window.DrawHovered(quiet);
	CHECK(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_TextDisabled)).has_value());
}

TEST_CASE("An icon button rests in the text colour unless it is asked to rest dim")
{
	BoxWindow window;

	window.Draw({.Fit = Core::IconButtonFit::Frame});

	CHECK(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_TextDisabled)).has_value());
}

TEST_CASE("An engaged icon button draws its glyph in the accent though it is asked to rest dim")
{
	constexpr bool kEngaged = true;

	BoxWindow window;

	window.Draw({.Fit = Core::IconButtonFit::Box, .Rest = Core::IconButtonRest::Dim}, kEngaged);

	CHECK(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_CheckMark)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_TextDisabled)).has_value());
}

TEST_CASE("A dim icon button's glyph brightens as its fill eases in under the pointer")
{
	constexpr int kFramesIntoTheFade = 2;

	BoxWindow window;
	const Core::IconButtonOptions quiet{.Fit = Core::IconButtonFit::Box, .Rest = Core::IconButtonRest::Dim};

	const ImRect button = window.Draw(quiet);
	window.Draw(quiet);
	ImGui::GetIO().AddMousePosEvent(button.GetCenter().x, button.GetCenter().y);

	for (int frame = 0; frame < kFramesIntoTheFade; ++frame)
	{
		window.Draw(quiet);
	}

	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_ButtonHovered)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_TextDisabled)).has_value());
	CHECK_FALSE(InkInColour(BoxWindow::Drawing(), ImGui::GetColorU32(ImGuiCol_Text)).has_value());
}

TEST_CASE("An icon button given no tooltip shows none under the pointer, and one given a tooltip does")
{
	const Core::IconButtonOptions box{.Fit = Core::IconButtonFit::Box};

	{
		BoxWindow window;
		window.DrawHovered(box);

		CHECK(BoxWindow::ShowsATooltip());
	}

	{
		BoxWindow window;
		window.Tooltip = nullptr;
		window.DrawHovered(box);

		CHECK_FALSE(BoxWindow::ShowsATooltip());
	}
}
