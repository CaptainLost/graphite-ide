#include <doctest/doctest.h>

#include "Core/ImGui/IconButton.h"

#include <imgui.h>
#include <imgui_internal.h>

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
