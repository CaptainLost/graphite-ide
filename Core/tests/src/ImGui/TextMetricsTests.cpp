#include <doctest/doctest.h>

#include "Core/ImGui/TextMetrics.h"

#include <imgui.h>

#include <string_view>

TEST_CASE("TextWidth measures exactly the bytes of the view, and nothing for an empty one")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.IniFilename = nullptr;
	io.Fonts->Build();
	ImGui::NewFrame();

	CHECK(Core::TextWidth(std::string_view{}) == 0.0f);
	CHECK(Core::TextWidth("") == 0.0f);

	const std::string_view text = "ab##cd";
	CHECK(Core::TextWidth(text) == ImGui::CalcTextSize(text.data(), text.data() + text.size()).x);
	CHECK(Core::TextWidth(text.substr(0, 2)) == ImGui::CalcTextSize("ab").x);

	ImGui::EndFrame();
	ImGui::DestroyContext(context);
}
