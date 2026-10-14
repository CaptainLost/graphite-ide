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
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
	constexpr int kFramesToSettle = 30;
	constexpr int kFramesInASecondOfAnimation = 60;
	constexpr int kFramesAfterFadeIn = 3;
	const std::chrono::milliseconds kFadeInSafetyMargin{50};
	const std::chrono::milliseconds kJustUnderASixtieth{16};
	const std::chrono::milliseconds kJustOverASixtieth{17};

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

		void Next(Core::Toasts& toasts, const std::function<void()>& drawBeforeToasts)
		{
			ImGui::NewFrame();
			drawBeforeToasts();
			toasts.Render(Core::ToastPalette{});
			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	bool AsksOnlyForTheCountdown(Frames& frames, Core::Toasts& toasts)
	{
		(void)Core::Redraw::TakeDeadline();
		const Core::Redraw::Clock::time_point beforeFrame = Core::Redraw::Clock::now();
		frames.Next(toasts);
		const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
		return deadline.has_value() && *deadline >= beforeFrame + kJustUnderASixtieth;
	}
}

TEST_CASE("Drawing a toast of every kind completes a frame without an ImGui assertion")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Success, "Title", "done");
	toasts.Show(Core::ToastKind::Warning, "Title", "careful");
	toasts.Show(Core::ToastKind::Error, "Title", std::string(2000, 'x'));

	for (int frameIndex = 0; frameIndex < kFramesToSettle; ++frameIndex)
	{
		frames.Next(toasts);
	}

	CHECK(toasts.GetStack().Get().size() == 3);
}

TEST_CASE("A title long enough to wrap, under a count, completes a frame without an ImGui assertion")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Error, std::string(300, 't'), "same");
	toasts.Show(Core::ToastKind::Error, std::string(300, 't'), "same");

	for (int frameIndex = 0; frameIndex < kFramesToSettle; ++frameIndex)
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

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now());
}

TEST_CASE("A toast counting down asks for frames often enough to drain its border smoothly")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Warning, "Title", "w");
	frames.Next(toasts);

	std::this_thread::sleep_for(Core::ToastStack::kEnter + kFadeInSafetyMargin);
	(void)Core::Redraw::TakeDeadline();
	const Core::Redraw::Clock::time_point beforeFrame = Core::Redraw::Clock::now();
	frames.Next(toasts);

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= beforeFrame + kJustUnderASixtieth);
	CHECK(*deadline <= Core::Redraw::Clock::now() + kJustOverASixtieth);
}

TEST_CASE("A toast pushed up by a newer one asks for frames until it arrives")
{
	Frames frames;
	Core::Toasts toasts;

	toasts.Show(Core::ToastKind::Error, "Title", "older");
	frames.Next(toasts);

	std::this_thread::sleep_for(Core::ToastStack::kEnter + kFadeInSafetyMargin);
	frames.Next(toasts);
	REQUIRE(AsksOnlyForTheCountdown(frames, toasts));

	toasts.Show(Core::ToastKind::Error, "Title", "newer");
	frames.Next(toasts);

	std::this_thread::sleep_for(Core::ToastStack::kEnter + kFadeInSafetyMargin);
	(void)Core::Redraw::TakeDeadline();
	frames.Next(toasts);
	const std::optional<Core::Redraw::Clock::time_point> deadlineWhileMoving = Core::Redraw::TakeDeadline();
	REQUIRE(deadlineWhileMoving.has_value());
	CHECK(*deadlineWhileMoving <= Core::Redraw::Clock::now());

	for (int frameIndex = 0; frameIndex < kFramesInASecondOfAnimation; ++frameIndex)
	{
		Core::Animation::BeginFrame();
		frames.Next(toasts);
	}

	Core::Animation::BeginFrame();
	CHECK(AsksOnlyForTheCountdown(frames, toasts));
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
	constexpr float kCardRightPadding = 14.0f;
	constexpr float kInsideCloseButton = 2.0f;
	constexpr float kCardTopPadding = 12.0f;
	constexpr float kHalfALineDown = 6.0f;
	constexpr float kInsideMessageLeft = 80.0f;
	constexpr float kCardBottomPadding = 12.0f;
	constexpr float kInsideMessageRow = 4.0f;
	constexpr float kInsideTitleLeft = 60.0f;
	constexpr float kInsideMenuItem = 4.0f;

	class EditorAndToasts
	{
	public:
		EditorAndToasts() { Settle(); }

		Core::Toasts Toasts;

		void Next()
		{
			m_Frames.Next(Toasts, [this] { DrawEditor(); });
		}

		void Settle()
		{
			Next();
			std::this_thread::sleep_for(Core::ToastStack::kEnter + kFadeInSafetyMargin);

			for (int frameIndex = 0; frameIndex < kFramesAfterFadeIn; ++frameIndex)
			{
				Next();
			}
		}

		void Click(ImVec2 clickPosition, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			ImGuiIO& io = ImGui::GetIO();
			constexpr bool kButtonDown = true;
			constexpr bool kButtonUp = false;

			io.AddMousePosEvent(clickPosition.x, clickPosition.y);
			Next();
			io.AddMouseButtonEvent(button, kButtonDown);
			Next();
			io.AddMouseButtonEvent(button, kButtonUp);
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
		void DrawEditor()
		{
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
		}

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

	const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
	screen.Click(ImVec2(card.Pos.x + card.Size.x - kCardRightPadding - kInsideCloseButton,
						card.Pos.y + kCardTopPadding + kHalfALineDown));

	REQUIRE(screen.Toasts.GetStack().Get().size() == 1);
	CHECK(screen.Toasts.GetStack().Get()[0].LeavingSince.has_value());
	CHECK(EditorAndToasts::Focused() == "Editor");
}

TEST_CASE("A click on a toast's message hands the keyboard back and leaves the toast up")
{
	EditorAndToasts screen;
	screen.Toasts.Show(Core::ToastKind::Error, "Title", "a message long enough to be clicked on");
	screen.Settle();

	const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
	screen.Click(ImVec2(card.Pos.x + kInsideMessageLeft,
						card.Pos.y + card.Size.y - kCardBottomPadding - kInsideMessageRow));

	REQUIRE(screen.Toasts.GetStack().Get().size() == 1);
	CHECK_FALSE(screen.Toasts.GetStack().Get()[0].LeavingSince.has_value());
	CHECK(EditorAndToasts::Focused() == "Editor");
}

namespace {
	std::string CopyFromTitle(EditorAndToasts& screen)
	{
		std::string copied;
		screen.Toasts.OnCopied = [&](std::string_view text) { copied = std::string(text); };

		const ImGuiWindow& card = EditorAndToasts::ToastWindow(screen.Toasts.GetStack().Get()[0].Id);
		screen.Click(ImVec2(card.Pos.x + kInsideTitleLeft, card.Pos.y + kCardTopPadding + kHalfALineDown),
					 ImGuiMouseButton_Right);

		screen.Next();

		const ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
		REQUIRE(imguiContext.OpenPopupStack.Size == 1);
		const ImGuiWindow* menu = imguiContext.OpenPopupStack[0].Window;
		REQUIRE(menu != nullptr);

		screen.Click(ImVec2(menu->Pos.x + menu->WindowPadding.x + kInsideMenuItem,
							menu->Pos.y + menu->WindowPadding.y + kInsideMenuItem));

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

	std::string message = "'side' is checked out in D:/Repos/IDE/.claude/worktrees/side";

	for (int repeatIndex = 0; repeatIndex < 20; ++repeatIndex)
	{
		message += " and more words";
	}

	toasts.Show(Core::ToastKind::Success, "Title", message);
	toasts.Show(Core::ToastKind::Warning, "Title", "");

	for (int frameIndex = 0; frameIndex < kFramesToSettle; ++frameIndex)
	{
		frames.Next(toasts);
	}

	CHECK(toasts.GetStack().Get().size() == 2);
}

TEST_CASE("Toasts::Clear with a group drops only the toasts shown in that group")
{
	constexpr std::uint32_t kCheckoutGroup = 1;
	const std::string noCopyText;

	Core::Toasts toasts;
	toasts.Show(Core::ToastKind::Error, "Checkout failed", "locked", noCopyText, kCheckoutGroup);
	toasts.Show(Core::ToastKind::Success, "Copied", "path: a.cpp");

	toasts.Clear(kCheckoutGroup);

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
