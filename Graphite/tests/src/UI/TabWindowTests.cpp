#include <doctest/doctest.h>

#include "UI/TabWindow.h"

#include "ImGuiFrames.h"

#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace {

	const std::string kTabName = "Tab###tab";
	constexpr const char* kOtherWindow = "Other";

	struct TabScreen
	{
		Graphite::TabWindowFrame Draw()
		{
			Graphite::TabWindowFrame frame;

			Frames.Next([this, &frame] {
				ImGui::Begin(kOtherWindow);
				ImGui::End();

				frame = Graphite::BeginTabWindow(kTabName, State, Place, CloseRequests);
				ImGui::End();
			});

			CloseRequests.EndFrame();
			return frame;
		}

		ImGuiFrames Frames;
		Graphite::TabWindowState State;
		Core::TabCloseRequests CloseRequests;
		int PlaceCount = 0;
		Graphite::PlaceNewTab Place = [this](const std::string&) { ++PlaceCount; };
	};

	bool IsFocused(const std::string& name)
	{
		const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
		return focused != nullptr && std::string{focused->Name} == name;
	}
}

TEST_CASE("A new tab is placed on the frame it appears and never again")
{
	TabScreen screen;

	const Graphite::TabWindowFrame first = screen.Draw();
	const Graphite::TabWindowFrame second = screen.Draw();

	CHECK(screen.PlaceCount == 1);
	CHECK(first.Appearing);
	CHECK_FALSE(second.Appearing);
}

TEST_CASE("A restored tab keeps the place imgui.ini remembers for it and is not placed again")
{
	TabScreen screen;
	screen.Frames.Next([] {
		ImGui::Begin(kTabName.c_str());
		ImGui::End();
	});
	static_cast<void>(ImGui::SaveIniSettingsToMemory());

	screen.State = Graphite::TabWindowState{.Fresh = true, .Restored = true};
	screen.Draw();

	CHECK(screen.PlaceCount == 0);
}

TEST_CASE("A restored tab with no remembered place is placed like a new one")
{
	TabScreen screen;
	screen.State = Graphite::TabWindowState{.Fresh = true, .Restored = true};

	screen.Draw();

	CHECK(screen.PlaceCount == 1);
}

TEST_CASE("A tab already open that is asked for focus takes it from the window in front, once")
{
	TabScreen screen;
	screen.Draw();
	screen.Frames.Next([] {
		ImGui::Begin(kTabName.c_str());
		ImGui::End();

		ImGui::SetNextWindowFocus();
		ImGui::Begin(kOtherWindow);
		ImGui::End();
	});
	REQUIRE_FALSE(IsFocused(kTabName));

	screen.State.Focus = true;
	screen.Draw();

	CHECK(IsFocused(kTabName));
	CHECK_FALSE(screen.State.Focus);
}

TEST_CASE("A new tab that was not restored takes focus as it appears")
{
	TabScreen screen;
	screen.Frames.Next([] {
		ImGui::SetNextWindowFocus();
		ImGui::Begin(kOtherWindow);
		ImGui::End();
	});

	screen.Draw();

	CHECK(IsFocused(kTabName));
}

TEST_CASE("A restored tab does not take focus as it appears, so it leaves the tab imgui.ini put in front")
{
	TabScreen screen;
	screen.Frames.Next([] {
		ImGui::Begin(kOtherWindow);
		ImGui::End();
	});
	screen.Frames.Next([] {
		ImGui::SetNextWindowFocus();
		ImGui::Begin(kOtherWindow);
		ImGui::End();
	});

	screen.State = Graphite::TabWindowState{.Fresh = true, .Restored = true};
	screen.Draw();

	CHECK_FALSE(IsFocused(kTabName));
}

TEST_CASE("A close asked from the tab's menu closes the tab")
{
	TabScreen screen;
	screen.Draw();

	screen.CloseRequests.Request(kTabName);
	screen.CloseRequests.EndFrame();

	CHECK_FALSE(screen.Draw().Open);
}
