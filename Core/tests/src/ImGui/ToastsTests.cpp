#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Toasts.h"

#include <imgui.h>

#include <chrono>
#include <thread>

namespace {
	class Frames
	{
	public:
		Frames()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.Fonts->Build();
		}

		~Frames() { ImGui::DestroyContext(m_Context); }

		Frames(const Frames&) = delete;
		Frames& operator=(const Frames&) = delete;

		void Next(Core::Toasts& toasts)
		{
			ImGui::NewFrame();
			toasts.Render(Core::ToastPalette{});
			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("Drawing a toast of every kind completes a frame without an ImGui assertion")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Success, "Title", "done");
	toasts.Show(Core::ToastKind::Warning, "Title", "careful");
	toasts.Show(Core::ToastKind::Error, "Title", std::string(2000, 'x'));

	for (int i = 0; i < 30; ++i)
	{
		frames.Next(toasts);
	}

	CHECK(toasts.GetStack().Get().size() == 3);
}

TEST_CASE("A title long enough to wrap, under a count, completes a frame without an ImGui assertion")
{
	Frames frames;
	Core::Toasts toasts;

	// Without the renderer there is no bold face, so this also covers drawing the title in
	// the current font.
	toasts.Show(Core::ToastKind::Error, std::string(300, 't'), "same");
	toasts.Show(Core::ToastKind::Error, std::string(300, 't'), "same");

	for (int i = 0; i < 30; ++i)
	{
		frames.Next(toasts);
	}

	REQUIRE(toasts.GetStack().Get().size() == 1);
	CHECK(toasts.GetStack().Get()[0].Count == 2);
}

TEST_CASE("A toast still fading in asks for the next frame")
{
	Frames frames;
	Core::Toasts toasts;
	(void)Core::Redraw::TakeDeadline();

	toasts.Show(Core::ToastKind::Error, "Title", "e");
	frames.Next(toasts);

	const auto deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now());
}

TEST_CASE("A shown toast asks for a frame no later than its expiry")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Warning, "Title", "w");
	frames.Next(toasts);

	// Past the fade-in, wall-clock: the renderer reads the real clock.
	std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));
	(void)Core::Redraw::TakeDeadline();
	frames.Next(toasts);

	const auto deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline > Core::Redraw::Clock::now() + std::chrono::seconds(4));
	CHECK(*deadline <= Core::Redraw::Clock::now() + Core::ToastStack::kLifetime);
}

TEST_CASE("A toast pushed up by a newer one asks for frames until it arrives")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Error, "Title", "older");
	frames.Next(toasts);

	// Errors never expire, so once a toast has faded in nothing but a move asks for a frame.
	std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));
	frames.Next(toasts);
	(void)Core::Redraw::TakeDeadline();
	frames.Next(toasts);
	REQUIRE_FALSE(Core::Redraw::TakeDeadline().has_value());

	// The newer one takes the bottom; the older one starts moving up to make room.
	toasts.Show(Core::ToastKind::Error, "Title", "newer");
	frames.Next(toasts);

	// Past the newer one's fade-in, so only the move is left to ask. ImAnim's clock moves only
	// with its own frames, which this test has not run yet, so the move is still under way.
	std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));
	(void)Core::Redraw::TakeDeadline();
	frames.Next(toasts);
	const auto moving = Core::Redraw::TakeDeadline();
	REQUIRE(moving.has_value());
	CHECK(*moving <= Core::Redraw::Clock::now());

	// A second of ImAnim's frames takes the move to its end.
	for (int i = 0; i < 60; ++i)
	{
		Core::Animation::BeginFrame();
		frames.Next(toasts);
	}

	(void)Core::Redraw::TakeDeadline();
	Core::Animation::BeginFrame();
	frames.Next(toasts);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("Nothing shown asks for no frame")
{
	Frames frames;
	Core::Toasts toasts;
	(void)Core::Redraw::TakeDeadline();

	frames.Next(toasts);

	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
