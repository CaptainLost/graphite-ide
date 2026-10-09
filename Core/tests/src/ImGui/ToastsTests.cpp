#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Toasts.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
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

		// With `before` drawn first, as the application draws its panels before the toasts.
		void Next(Core::Toasts& toasts, const std::function<void()>& before)
		{
			ImGui::NewFrame();
			before();
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

TEST_CASE("A toast counting down asks for frames often enough to drain its border smoothly")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Warning, "Title", "w");
	frames.Next(toasts);

	// Past the fade-in, wall-clock: the renderer reads the real clock.
	std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));
	(void)Core::Redraw::TakeDeadline();
	const Core::Redraw::Clock::time_point beforeFrame = Core::Redraw::Clock::now();
	frames.Next(toasts);

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	const std::chrono::milliseconds justUnderASixtieth{16};
	const std::chrono::milliseconds justOverASixtieth{17};
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= beforeFrame + justUnderASixtieth);
	CHECK(*deadline <= Core::Redraw::Clock::now() + justOverASixtieth);
}

TEST_CASE("A toast pushed up by a newer one asks for frames until it arrives")
{
	Frames frames;
	Core::Toasts toasts;

	// Once the toasts have faded in, their countdowns ask for a frame no sooner than a sixtieth
	// of a second away, while a move asks for the very next one.
	const std::chrono::milliseconds justUnderASixtieth{16};
	const auto asksOnlyForTheCountdown = [&] {
		(void)Core::Redraw::TakeDeadline();
		const Core::Redraw::Clock::time_point beforeFrame = Core::Redraw::Clock::now();
		frames.Next(toasts);
		const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
		return deadline.has_value() && *deadline >= beforeFrame + justUnderASixtieth;
	};

	toasts.Show(Core::ToastKind::Error, "Title", "older");
	frames.Next(toasts);

	std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));
	frames.Next(toasts);
	REQUIRE(asksOnlyForTheCountdown());

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
	for (int frameIndex = 0; frameIndex < 60; ++frameIndex)
	{
		Core::Animation::BeginFrame();
		frames.Next(toasts);
	}

	Core::Animation::BeginFrame();
	CHECK(asksOnlyForTheCountdown());
}

TEST_CASE("Nothing shown asks for no frame")
{
	Frames frames;
	Core::Toasts toasts;
	(void)Core::Redraw::TakeDeadline();

	frames.Next(toasts);

	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}

namespace {
	// A window that holds the keyboard, as the editor does, and the toasts drawn over it. The
	// first frame focuses it; ImGui then keeps it focused until something else is clicked.
	class EditorAndToasts
	{
	public:
		EditorAndToasts() { Settle(); }

		Core::Toasts Toasts;

		void Next()
		{
			m_Frames.Next(Toasts, [this] {
				if (m_FocusEditor)
				{
					ImGui::SetNextWindowFocus();
					m_FocusEditor = false;
				}

				ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
				ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
				ImGui::Begin("Editor");
				ImGui::TextUnformatted("text");
				ImGui::End();
			});
		}

		// Past the fade-in and its rise, so every toast stands where it rests.
		void Settle()
		{
			Next();
			std::this_thread::sleep_for(Core::ToastStack::kEnter + std::chrono::milliseconds(50));

			for (int i = 0; i < 3; ++i)
			{
				Next();
			}
		}

		// One event a frame, as a person clicks: over the point, down, up.
		void Click(ImVec2 at, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddMousePosEvent(at.x, at.y);
			Next();
			io.AddMouseButtonEvent(button, true);
			Next();
			io.AddMouseButtonEvent(button, false);
			Next();
		}

		static const ImGuiWindow& ToastWindow(std::uint64_t id)
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(std::format("##toast{}", id).c_str());
			REQUIRE(window != nullptr);
			return *window;
		}

		static std::string Focused()
		{
			const ImGuiWindow* window = ImGui::GetCurrentContext()->NavWindow;
			return window != nullptr ? std::string(window->Name) : std::string();
		}

	private:
		Frames m_Frames;
		bool m_FocusEditor = true;
	};
}

TEST_CASE("A click on the close button closes the toast and hands the keyboard back")
{
	EditorAndToasts screen;
	screen.Toasts.Show(Core::ToastKind::Error, "Title", "message");
	screen.Settle();
	REQUIRE(EditorAndToasts::Focused() == "Editor");

	// Inside the close button: the card's right padding, then the button, on the title's row.
	const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
	screen.Click(ImVec2(card.Pos.x + card.Size.x - 14.0f - 2.0f, card.Pos.y + 12.0f + 6.0f));

	REQUIRE(screen.Toasts.GetStack().Get().size() == 1);
	CHECK(screen.Toasts.GetStack().Get()[0].LeavingSince.has_value());
	CHECK(EditorAndToasts::Focused() == "Editor");
}

TEST_CASE("A click on a toast's message hands the keyboard back and leaves the toast up")
{
	EditorAndToasts screen;
	screen.Toasts.Show(Core::ToastKind::Error, "Title", "a message long enough to be clicked on");
	screen.Settle();

	// Inside the message, under the title and to the right of the icon.
	const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
	screen.Click(ImVec2(card.Pos.x + 80.0f, card.Pos.y + card.Size.y - 12.0f - 4.0f));

	REQUIRE(screen.Toasts.GetStack().Get().size() == 1);
	CHECK_FALSE(screen.Toasts.GetStack().Get()[0].LeavingSince.has_value());
	CHECK(EditorAndToasts::Focused() == "Editor");
}

namespace {
	// Right-clicks the toast's title and picks Copy from the menu that opens.
	std::string CopyFromTitle(EditorAndToasts& screen)
	{
		std::string copied;
		screen.Toasts.OnCopied = [&](std::string_view text) { copied = std::string(text); };

		const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
		screen.Click(ImVec2(card.Pos.x + 60.0f, card.Pos.y + 12.0f + 6.0f), ImGuiMouseButton_Right);

		// A popup sizes itself on the frame it appears and shows from the next.
		screen.Next();

		const ImGuiContext& g = *ImGui::GetCurrentContext();
		REQUIRE(g.OpenPopupStack.Size == 1);
		const ImGuiWindow* menu = g.OpenPopupStack[0].Window;
		REQUIRE(menu != nullptr);

		screen.Click(ImVec2(menu->Pos.x + menu->WindowPadding.x + 4.0f, menu->Pos.y + menu->WindowPadding.y + 4.0f));

		return copied;
	}
}

TEST_CASE("Copy copies the title and the message, one line each")
{
	EditorAndToasts screen;
	screen.Toasts.Show(Core::ToastKind::Error, "Title", "message");
	screen.Settle();

	CHECK(CopyFromTitle(screen) == "Title\nmessage");
}

TEST_CASE("Copy copies the copy text a toast was shown with instead of its message")
{
	EditorAndToasts screen;
	screen.Toasts.Show(Core::ToastKind::Error, "Title", "src/a.cpp", "Title\nD:/Repos/IDE/src/a.cpp");
	screen.Settle();

	CHECK(CopyFromTitle(screen) == "Title\nD:/Repos/IDE/src/a.cpp");
}

TEST_CASE("A toast drawn with a bold face for its quoted names and a path completes a frame without an ImGui assertion")
{
	Frames frames;
	Core::Toasts toasts;

	// Long enough to scroll, so the body is laid out short of its scrollbar too.
	std::string message = "'side' is checked out in D:/Repos/IDE/.claude/worktrees/side";

	for (int i = 0; i < 20; ++i)
	{
		message += " and more words";
	}

	toasts.Show(Core::ToastKind::Success, "Title", message);
	toasts.Show(Core::ToastKind::Warning, "Title", "");

	for (int i = 0; i < 30; ++i)
	{
		frames.Next(toasts);
	}

	CHECK(toasts.GetStack().Get().size() == 2);
}

TEST_CASE("Toasts::Clear with a group drops only the toasts shown in that group")
{
	Core::Toasts toasts;
	toasts.Show(Core::ToastKind::Error, "Checkout failed", "locked", {}, 1);
	toasts.Show(Core::ToastKind::Success, "Copied", "path: a.cpp");

	toasts.Clear(1);

	REQUIRE(toasts.GetStack().Get().size() == 1);
	CHECK(toasts.GetStack().Get()[0].Title == "Copied");

	toasts.Clear();
	CHECK(toasts.GetStack().Get().empty());
}

TEST_CASE("Toasts::Show returns the id the stack gave the toast")
{
	Core::Toasts toasts;
	const std::uint64_t shownId = toasts.Show(Core::ToastKind::Error, "Title", "failed");

	REQUIRE(toasts.GetStack().Get().size() == 1);
	CHECK(shownId == toasts.GetStack().Get()[0].Id);
}

TEST_CASE("Toasts hands on the departures of the toasts it cleared")
{
	Core::Toasts toasts;
	const std::uint64_t shownId = toasts.Show(Core::ToastKind::Error, "Title", "failed");
	toasts.Clear();

	const std::vector<Core::ToastDeparture> departures = toasts.TakeDepartures();
	REQUIRE(departures.size() == 1);
	CHECK(departures[0].Id == shownId);
	CHECK(departures[0].Reason == Core::ToastDepartureReason::Cleared);
}
