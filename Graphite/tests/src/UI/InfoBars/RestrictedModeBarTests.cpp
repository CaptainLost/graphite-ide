#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/InfoBars/RestrictedModeBar.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <string>

namespace {
	// The close button sits at the bar's right end, a frame high and centred in the bar.
	ImVec2 CloseButtonOf(const char* host)
	{
		const ImGuiWindow* editor = ImGui::FindWindowByName(host);
		REQUIRE(editor != nullptr);
		REQUIRE(!editor->DC.ChildWindows.empty());
		const ImGuiWindow& bar = *editor->DC.ChildWindows.back();
		return ImVec2(bar.InnerRect.Max.x - bar.WindowPadding.x - ImGui::GetFrameHeight() * 0.5f, bar.InnerRect.GetCenter().y);
	}
}

TEST_CASE("A click on the restricted mode bar's close button asks to hide it")
{
	ImGuiFrames frames;
	Graphite::RestrictedBarChoice answered = Graphite::RestrictedBarChoice::None;

	const auto draw = [&answered] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f));
		ImGui::Begin("Editor area", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		const Graphite::RestrictedBarChoice choice =
			Graphite::DrawRestrictedModeBar(std::filesystem::path("D:/Repos"), ImVec4(0.9f, 0.7f, 0.1f, 1.0f));

		if (choice != Graphite::RestrictedBarChoice::None)
		{
			answered = choice;
		}

		ImGui::End();
	};

	frames.Next(draw);
	frames.Click(CloseButtonOf("Editor area"), draw);

	CHECK(answered == Graphite::RestrictedBarChoice::Close);
}

TEST_CASE("The Trust All confirmation names the whole parent path and what trusting it means")
{
	const std::string message = Graphite::TrustAllMessage("D:/Downloads");

	CHECK(message.find("D:/Downloads") != std::string::npos);
	CHECK(message.find("every folder under") != std::string::npos);
	CHECK(message.find("language server") != std::string::npos);
	CHECK(message.find("background analysis") != std::string::npos);
}
