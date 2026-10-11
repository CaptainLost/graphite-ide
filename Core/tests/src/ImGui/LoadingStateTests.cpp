#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/LoadingState.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <optional>

namespace {

	constexpr ImVec2 kWindowSize{400.0f, 300.0f};

	// A context of its own with the default font built, and one window filling a known
	// rectangle, so where the loader lands can be measured against it.
	class LoaderWindow
	{
	public:
		LoaderWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~LoaderWindow() { ImGui::DestroyContext(m_Context); }

		LoaderWindow(const LoaderWindow&) = delete;
		LoaderWindow& operator=(const LoaderWindow&) = delete;

		// Draws the loader in a window of `size` and returns the rectangle it took.
		ImRect DrawLoader(const char* text, ImVec2 size = kWindowSize)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(size);
			ImGui::Begin("Loader", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			m_Region = ImRect(ImGui::GetCursorScreenPos(),
							  ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x,
									 ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y));

			Core::LoadingState(text);
			const ImRect drawn(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImGui::End();
			ImGui::EndFrame();

			return drawn;
		}

		const ImRect& GetRegion() const { return m_Region; }

	private:
		ImGuiContext* m_Context = nullptr;
		ImRect m_Region;
	};
}

TEST_CASE("LoadingState stands in the middle of what is left of the window")
{
	LoaderWindow window;

	const ImRect drawn = window.DrawLoader("Searching");

	CHECK(drawn.GetWidth() > 0.0f);
	CHECK(drawn.GetCenter().x == doctest::Approx(window.GetRegion().GetCenter().x).epsilon(0.01));
	CHECK(drawn.GetCenter().y == doctest::Approx(window.GetRegion().GetCenter().y).epsilon(0.01));
}

TEST_CASE("LoadingState is as wide as its text in a window with room for it")
{
	LoaderWindow window;

	const ImRect drawn = window.DrawLoader("Searching");

	CHECK(drawn.GetWidth() == doctest::Approx(ImGui::CalcTextSize("Searching").x).epsilon(0.01));
}

TEST_CASE("LoadingState in a window narrower than its text stays inside the window")
{
	LoaderWindow window;
	const ImVec2 narrow{60.0f, 300.0f};

	const ImRect drawn = window.DrawLoader("Searching for every file in the folder", narrow);

	CHECK(drawn.Min.x >= window.GetRegion().Min.x);
	CHECK(drawn.Max.x <= window.GetRegion().Max.x + 0.5f);
}

TEST_CASE("LoadingState asks for the next frame so its text keeps moving")
{
	LoaderWindow window;
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
