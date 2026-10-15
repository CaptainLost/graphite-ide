#include <doctest/doctest.h>

#include "UI/Dialogs/DialogLayout.h"

#include "ImGuiFrames.h"

#include <imgui.h>

namespace {

	constexpr const char* kDialogWindow = "Dialog";

	struct AcknowledgeScreen
	{
		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
			ImGui::SetNextWindowFocus();
			ImGui::Begin(kDialogWindow);

			Acknowledged = Acknowledged || Graphite::DialogLayout::AcknowledgeRow("Close");
			ButtonCentre = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
								  (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);

			ImGui::End();
		}

		void Next()
		{
			Frames.Next([this] { Draw(); });
		}

		ImGuiFrames Frames;
		bool Acknowledged = false;
		ImVec2 ButtonCentre;
	};
}

TEST_CASE("An acknowledge row answers when its one button is clicked")
{
	AcknowledgeScreen screen;
	screen.Next();
	screen.Next();
	REQUIRE_FALSE(screen.Acknowledged);

	screen.Frames.Click(screen.ButtonCentre, [&screen] { screen.Draw(); });

	CHECK(screen.Acknowledged);
}

TEST_CASE("An acknowledge row answers Enter once its dialog has the keyboard")
{
	AcknowledgeScreen screen;
	screen.Next();
	screen.Next();
	REQUIRE_FALSE(screen.Acknowledged);

	screen.Frames.PressKey(ImGuiKey_Enter, [&screen] { screen.Draw(); });

	CHECK(screen.Acknowledged);
}

TEST_CASE("An acknowledge row answers Escape once its dialog has the keyboard")
{
	AcknowledgeScreen screen;
	screen.Next();
	screen.Next();
	REQUIRE_FALSE(screen.Acknowledged);

	screen.Frames.PressKey(ImGuiKey_Escape, [&screen] { screen.Draw(); });

	CHECK(screen.Acknowledged);
}
