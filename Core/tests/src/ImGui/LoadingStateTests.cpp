#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/LoadingState.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <optional>

namespace {

	constexpr ImVec2 kWindowSize{400.0f, 300.0f};

	// At 60 frames a second a fifth of a second is twelve frames; fifteen are past it.
	constexpr int kFramesPastTheWait = 15;

	class LoaderWindow
	{
	public:
		LoaderWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();
		}

		~LoaderWindow() { ImGui::DestroyContext(m_Context); }

		LoaderWindow(const LoaderWindow&) = delete;
		LoaderWindow& operator=(const LoaderWindow&) = delete;

		// Nothing while the loader still waits: it drew no item and left the cursor where it was.
		std::optional<ImRect> DrawLoader(const char* text, ImVec2 size = kWindowSize)
		{
			BeginFrame("Loader", size);

			m_Region = ImRect(ImGui::GetCursorScreenPos(),
							  ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x,
									 ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y));

			const ImVec2 cursorBefore = ImGui::GetCursorPos();
			Core::LoadingState(text);
			const bool drewSomething = ImGui::GetCursorPos().y != cursorBefore.y;
			const ImRect drawn(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			EndFrame();

			return drewSomething ? std::optional<ImRect>{drawn} : std::nullopt;
		}

		std::optional<ImRect> DrawLoaderPastItsWait(const char* text, ImVec2 size = kWindowSize)
		{
			std::optional<ImRect> drawn;

			for (int frameIndex = 0; frameIndex < kFramesPastTheWait; ++frameIndex)
			{
				drawn = DrawLoader(text, size);
			}

			return drawn;
		}

		void DrawWithoutLoader()
		{
			BeginFrame("Loader", kWindowSize);
			ImGui::TextUnformatted("the data arrived");
			EndFrame();
		}

		struct LineDrawn
		{
			float Advance = 0.0f;
			float LineHeightWithSpacing = 0.0f;
			bool SpinnerDrawn = false;
			int Vertices = 0;
			float LastItemLeft = 0.0f;
		};

		LineDrawn DrawLineLoader(const char* text, ImVec2 size = kWindowSize,
								 Core::WhileLoaderWaits whileWaiting = Core::WhileLoaderWaits::ShowNothing)
		{
			BeginFrame("LineLoader", size);

			const float before = ImGui::GetCursorPosY();
			const int verticesBefore = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			Core::LoadingLine(text, whileWaiting);
			const float after = ImGui::GetCursorPosY();
			const float lineHeightWithSpacing = ImGui::GetTextLineHeightWithSpacing();
			const int vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - verticesBefore;
			const float lastItemLeft = ImGui::GetItemRectMin().x;

			EndFrame();

			return LineDrawn{.Advance = after - before,
							 .LineHeightWithSpacing = lineHeightWithSpacing,
							 .SpinnerDrawn = vertices > 0,
							 .Vertices = vertices,
							 .LastItemLeft = lastItemLeft};
		}

		const ImRect& GetRegion() const { return m_Region; }

	private:
		static void BeginFrame(const char* name, ImVec2 size)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(size);
			ImGui::Begin(name, nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);
		}

		static void EndFrame()
		{
			ImGui::End();
			ImGui::EndFrame();
		}

		ImGuiContext* m_Context = nullptr;
		ImRect m_Region;
	};
}

TEST_CASE("LoadingState shows nothing as a wait starts, and asks for the frame on which it will show")
{
	LoaderWindow window;
	static_cast<void>(Core::Redraw::TakeDeadline());

	CHECK_FALSE(window.DrawLoader("Searching").has_value());

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline > Core::Redraw::Clock::now());
}

TEST_CASE("LoadingState shows once its wait has lasted a fifth of a second")
{
	LoaderWindow window;
	constexpr int kFramesInAFifthOfASecond = 12;

	int framesWaited = 0;

	while (!window.DrawLoader("Searching").has_value() && framesWaited < kFramesPastTheWait)
	{
		++framesWaited;
	}

	CHECK(framesWaited >= kFramesInAFifthOfASecond);
	CHECK(framesWaited < kFramesPastTheWait);
}

TEST_CASE("A frame that draws no loader ends the wait, so the next one waits again")
{
	LoaderWindow window;

	REQUIRE(window.DrawLoaderPastItsWait("Searching").has_value());

	window.DrawWithoutLoader();

	CHECK_FALSE(window.DrawLoader("Searching").has_value());
}

TEST_CASE("LoadingState stands in the middle of what is left of the window")
{
	LoaderWindow window;

	const std::optional<ImRect> drawn = window.DrawLoaderPastItsWait("Searching");

	REQUIRE(drawn.has_value());
	CHECK(drawn->GetWidth() > 0.0f);
	CHECK(drawn->GetCenter().x == doctest::Approx(window.GetRegion().GetCenter().x).epsilon(0.01));
	CHECK(drawn->GetCenter().y == doctest::Approx(window.GetRegion().GetCenter().y).epsilon(0.01));
}

TEST_CASE("LoadingState is as wide as its text in a window with room for it")
{
	LoaderWindow window;

	const std::optional<ImRect> drawn = window.DrawLoaderPastItsWait("Searching");

	REQUIRE(drawn.has_value());
	CHECK(drawn->GetWidth() == doctest::Approx(ImGui::CalcTextSize("Searching").x).epsilon(0.01));
}

TEST_CASE("LoadingState in a window narrower than its text stays inside the window")
{
	LoaderWindow window;
	const ImVec2 narrow{60.0f, 300.0f};

	const std::optional<ImRect> drawn = window.DrawLoaderPastItsWait("Searching for every file in the folder", narrow);

	REQUIRE(drawn.has_value());
	CHECK(drawn->Min.x >= window.GetRegion().Min.x);
	CHECK(drawn->Max.x <= window.GetRegion().Max.x + 0.5f);
}

TEST_CASE("LoadingState asks for the next frame so its text keeps moving")
{
	LoaderWindow window;

	REQUIRE(window.DrawLoaderPastItsWait("Searching").has_value());
	static_cast<void>(Core::Redraw::TakeDeadline());

	window.DrawLoader("Searching");

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now());
}

TEST_CASE("LoadingState draws frame after frame without an ImGui assertion")
{
	LoaderWindow window;
	constexpr int kFramesOfOneSweep = 120;

	for (int frameIndex = 0; frameIndex < kFramesOfOneSweep; ++frameIndex)
	{
		window.DrawLoader("Searching");
	}

	CHECK(ImGui::GetFrameCount() == kFramesOfOneSweep);
}

TEST_CASE("LoadingLine takes exactly one row of text while it waits and once it shows, so a list does not jump")
{
	LoaderWindow window;

	const LoaderWindow::LineDrawn waiting = window.DrawLineLoader("Reading authors");

	LoaderWindow::LineDrawn shown;
	for (int frameIndex = 0; frameIndex < kFramesPastTheWait; ++frameIndex)
	{
		shown = window.DrawLineLoader("Reading authors");
	}

	CHECK_FALSE(waiting.SpinnerDrawn);
	CHECK(waiting.Advance == doctest::Approx(waiting.LineHeightWithSpacing));
	CHECK(shown.SpinnerDrawn);
	CHECK(shown.Advance == doctest::Approx(shown.LineHeightWithSpacing));
}

TEST_CASE("LoadingLine that shows its text while it waits draws the text at once, where it stays once the spinner shows")
{
	LoaderWindow window;
	const Core::WhileLoaderWaits showText = Core::WhileLoaderWaits::ShowText;

	const LoaderWindow::LineDrawn waiting = window.DrawLineLoader("Loading 500 more", kWindowSize, showText);

	LoaderWindow::LineDrawn shown;
	for (int frameIndex = 0; frameIndex < kFramesPastTheWait; ++frameIndex)
	{
		shown = window.DrawLineLoader("Loading 500 more", kWindowSize, showText);
	}

	CHECK(waiting.Vertices > 0);
	CHECK(waiting.Advance == doctest::Approx(waiting.LineHeightWithSpacing));
	CHECK(shown.Vertices > waiting.Vertices);
	CHECK(shown.LastItemLeft == doctest::Approx(waiting.LastItemLeft));
	CHECK(shown.Advance == doctest::Approx(shown.LineHeightWithSpacing));
}

TEST_CASE("LoadingLine asks for the next frame so its spinner keeps turning")
{
	LoaderWindow window;

	for (int frameIndex = 0; frameIndex < kFramesPastTheWait; ++frameIndex)
	{
		window.DrawLineLoader("Reading authors");
	}

	static_cast<void>(Core::Redraw::TakeDeadline());

	window.DrawLineLoader("Reading authors");

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now());
}

TEST_CASE("LoadingLine draws frame after frame without an ImGui assertion")
{
	LoaderWindow window;
	constexpr int kFramesOfSeveralTurns = 120;
	const ImVec2 narrow{40.0f, 300.0f};

	for (int frameIndex = 0; frameIndex < kFramesOfSeveralTurns; ++frameIndex)
	{
		window.DrawLineLoader("Reading authors", narrow);
	}

	CHECK(ImGui::GetFrameCount() == kFramesOfSeveralTurns);
}
