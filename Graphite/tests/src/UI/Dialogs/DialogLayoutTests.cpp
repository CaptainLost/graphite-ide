#include <doctest/doctest.h>

#include "UI/Dialogs/DialogLayout.h"
#include "UI/FileIcons.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include <imgui.h>

#include <optional>
#include <string>

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

TEST_CASE("A dialog's file row draws its glyph in its kind's colour and its path in the text colour past the icon")
{
	constexpr bool kDarkScheme = false;
	constexpr float kOnePixel = 1.0f;

	ImGuiFrames frames;

	float rowLeft = 0.0f;
	float nameOffset = 0.0f;
	std::optional<HorizontalExtent> glyph;
	std::optional<HorizontalExtent> path;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
		ImGui::Begin("##fileRowDialog", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

		const std::string noTag;
		rowLeft = ImGui::GetCursorScreenPos().x;
		Graphite::DialogLayout::FileRow(Graphite::FileKind::Json, "graphite.json", noTag);

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		glyph = HorizontalExtentInColour(drawing, Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme));
		path = HorizontalExtentInColour(drawing, ImGui::GetColorU32(ImGuiCol_Text));
		nameOffset = Graphite::IconToNameOffset();

		ImGui::End();
	});

	REQUIRE(glyph.has_value());
	REQUIRE(path.has_value());
	CHECK(glyph->Left >= rowLeft - kOnePixel);
	CHECK(path->Left >= rowLeft + nameOffset - kOnePixel);
	CHECK(path->Left > glyph->Right);
}
