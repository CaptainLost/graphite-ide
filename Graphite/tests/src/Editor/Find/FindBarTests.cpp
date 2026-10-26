#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Editor/Find/FindBar.h"

#include "Core/ImGui/FontSet.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {
	constexpr float kEditorFontSize = 20.0f;
	constexpr bool kEscapeNotTaken = false;
}

TEST_CASE("The find bar is filled with the popup surface rather than the menu bar's chrome")
{
	ImGuiFrames frames;
	Graphite::FindBar bar;
	const Core::TextBuffer buffer("alpha beta alpha");
	Core::OpenItem item;
	item.Find.Open = true;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(900.0f, 300.0f));
		ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		bar.Draw(item, buffer, true, kEscapeNotTaken);
		ImGui::End();
	});

	// WARNING: ImGui draws a child window's background into its parent's draw list, not the child's own.
	const ImGuiWindow* document = ImGui::FindWindowByName("Document");
	REQUIRE(document != nullptr);
	REQUIRE(!document->DC.ChildWindows.empty());

	CHECK(DrawsInColour(*document->DrawList, ImGui::GetColorU32(ImGuiCol_PopupBg)));
	CHECK_FALSE(DrawsInColour(*document->DrawList, ImGui::GetColorU32(ImGuiCol_MenuBarBg)));
}

TEST_CASE("The find bar is measured and drawn at the interface size inside an editor at another size")
{
	ImGuiFrames frames;
	Graphite::FindBar bar;
	const Core::TextBuffer buffer("alpha beta alpha");
	Core::OpenItem item;
	item.Find.Open = true;
	item.Find.Query = "gamma";

	float interfaceFrameHeight = 0.0f;
	int stackBefore = 0;
	int stackAfter = 0;

	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(900.0f, 300.0f));
		ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		Core::PushInterfaceFont();
		interfaceFrameHeight = ImGui::GetFrameHeight();
		ImGui::PopFont();

		Core::PushCodeFont(kEditorFontSize);
		stackBefore = ImGui::GetCurrentContext()->FontStack.Size;
		bar.Draw(item, buffer, true, kEscapeNotTaken);
		stackAfter = ImGui::GetCurrentContext()->FontStack.Size;
		ImGui::PopFont();

		ImGui::End();
	};

	frames.Next(draw);
	frames.Next(draw);

	const ImGuiWindow* document = ImGui::FindWindowByName("Document");
	REQUIRE(document != nullptr);
	REQUIRE(!document->DC.ChildWindows.empty());

	CHECK(stackAfter == stackBefore);
	CHECK(document->DC.ChildWindows[0]->Size.y == interfaceFrameHeight);
}
