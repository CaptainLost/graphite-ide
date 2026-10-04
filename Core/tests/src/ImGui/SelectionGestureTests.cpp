#include <doctest/doctest.h>

#include "Core/ImGui/SelectionGesture.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

using namespace Core;

TEST_CASE("A single press over a line begins a selection")
{
	CHECK(DecidePress({1, false, true, false}) == SelectionAction::Begin);
}

TEST_CASE("Shift with a selection extends it instead of starting again")
{
	CHECK(DecidePress({1, true, true, true}) == SelectionAction::Extend);
	CHECK(DecidePress({1, true, true, false}) == SelectionAction::Begin);
}

TEST_CASE("A double press selects a word and a triple press a line")
{
	CHECK(DecidePress({2, false, true, false}) == SelectionAction::SelectWord);
	CHECK(DecidePress({3, false, true, true}) == SelectionAction::SelectLine);
	CHECK(DecidePress({4, false, true, true}) == SelectionAction::SelectLine);
}

TEST_CASE("A press off any line clears without starting a sweep")
{
	CHECK(DecidePress({1, false, false, true}) == SelectionAction::Clear);
	CHECK(DecidePress({2, false, false, true}) == SelectionAction::Clear);
}

TEST_CASE("No press does nothing")
{
	CHECK(DecidePress({0, true, true, true}) == SelectionAction::None);
}

TEST_CASE("A word or a line press acts on the character under the pointer, not the nearest boundary")
{
	SelectionSource source;
	source.CharacterAt = [](ImVec2) { return std::optional<TextPoint>(TextPoint{4, 1}); };

	const std::optional<TextPoint> boundary = TextPoint{4, 2};

	CHECK(PressPoint(SelectionAction::SelectWord, source, ImVec2(0.0f, 0.0f), boundary) == TextPoint{4, 1});
	CHECK(PressPoint(SelectionAction::SelectLine, source, ImVec2(0.0f, 0.0f), boundary) == TextPoint{4, 1});

	// A single press and a Shift+click keep the boundary.
	CHECK(PressPoint(SelectionAction::Begin, source, ImVec2(0.0f, 0.0f), boundary) == boundary);
	CHECK(PressPoint(SelectionAction::Extend, source, ImVec2(0.0f, 0.0f), boundary) == boundary);
}

TEST_CASE("A word press falls back to the boundary when the owner names no character")
{
	const std::optional<TextPoint> boundary = TextPoint{2, 3};

	SelectionSource unset;
	CHECK(PressPoint(SelectionAction::SelectWord, unset, ImVec2(0.0f, 0.0f), boundary) == boundary);

	SelectionSource silent;
	silent.CharacterAt = [](ImVec2) { return std::optional<TextPoint>{}; };
	CHECK(PressPoint(SelectionAction::SelectWord, silent, ImVec2(0.0f, 0.0f), boundary) == boundary);
}

namespace {
	// Kept off the system clipboard: the gesture's copy would otherwise replace whatever the
	// person running the tests had on it.
	void KeepClipboard(ImGuiContext*, const char*) {}

	SelectionGestureResult PressCtrlC(bool ctrl)
	{
		ImGuiContext* context = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(800.0f, 600.0f);
		io.DeltaTime = 1.0f / 60.0f;
		io.Fonts->Build();
		ImGui::GetPlatformIO().Platform_SetClipboardTextFn = &KeepClipboard;

		TextSelection selection;
		selection.Select({0, 0}, {0, 5});

		SelectionSource source;
		source.SelectedText = [] { return std::string("hello"); };

		SelectionGesture gesture;
		SelectionGestureResult result;

		for (int frame = 0; frame < 3; ++frame)
		{
			// Ctrl+C goes down on the last frame only, after the window has been focused.
			if (frame == 2)
			{
				io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
				io.AddKeyEvent(ImGuiKey_C, true);
			}

			ImGui::NewFrame();
			ImGui::Begin("Gesture");

			if (frame == 0)
			{
				ImGui::SetWindowFocus();
			}

			result = gesture.Update(selection, source, SelectionGestureOptions{});
			ImGui::End();
			ImGui::EndFrame();
		}

		ImGui::DestroyContext(context);
		return result;
	}
}

TEST_CASE("Ctrl+C copies the selection and says it came from the key, not the menu")
{
	const SelectionGestureResult result = PressCtrlC(true);

	CHECK(result.Copied);
	CHECK(result.CopiedText == "hello");
	CHECK_FALSE(result.FromMenu);
}

TEST_CASE("C without Ctrl copies nothing")
{
	const SelectionGestureResult result = PressCtrlC(false);

	CHECK_FALSE(result.Copied);
	CHECK_FALSE(result.FromMenu);
}

TEST_CASE("The context menu's Copy copies the selection and says it came from the menu")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->Build();
	ImGui::GetPlatformIO().Platform_SetClipboardTextFn = &KeepClipboard;

	TextSelection selection;
	selection.Select({0, 0}, {0, 5});

	SelectionSource source;
	source.SelectedText = [] { return std::string("hello"); };

	SelectionGestureOptions options;
	options.MenuId = "##gestureMenu";

	SelectionGesture gesture;
	SelectionGestureResult copied;
	ImVec2 copyItem(-1.0f, -1.0f);

	// Frame 0 opens the menu as a right-click would; frames 1-2 let it appear and find where
	// its first item, Copy, is; frame 3 moves onto it, 4 presses, 5 releases.
	for (int frame = 0; frame < 8; ++frame)
	{
		if (frame == 3)
		{
			io.AddMousePosEvent(copyItem.x, copyItem.y);
		}
		else if (frame == 4)
		{
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		}
		else if (frame == 5)
		{
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		}

		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
		ImGui::Begin("Gesture");

		if (frame == 0)
		{
			// The id BeginPopupContextWindow derives from MenuId in this same window.
			ImGui::OpenPopup(options.MenuId);
		}

		const SelectionGestureResult result = gesture.Update(selection, source, options);

		if (result.Copied)
		{
			copied = result;
		}

		ImGui::End();

		if (frame == 2)
		{
			ImGuiContext& g = *GImGui;
			REQUIRE(g.OpenPopupStack.Size == 1);
			const ImGuiWindow* menu = g.OpenPopupStack[0].Window;
			REQUIRE(menu != nullptr);
			copyItem = ImVec2(menu->Pos.x + menu->WindowPadding.x + 4.0f,
							  menu->Pos.y + menu->WindowPadding.y + ImGui::GetFontSize() * 0.5f);
		}

		ImGui::EndFrame();
	}

	ImGui::DestroyContext(context);

	CHECK(copied.Copied);
	CHECK(copied.CopiedText == "hello");
	CHECK(copied.FromMenu);
}
