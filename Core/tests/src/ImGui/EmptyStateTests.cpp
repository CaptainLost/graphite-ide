#include <doctest/doctest.h>

#include "Core/ImGui/EmptyState.h"

#include <imgui.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using Core::CentredLine;
using Core::CentreLines;

namespace {
	class Frame
	{
	public:
		Frame()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();

			ImGui::NewFrame();
		}

		~Frame()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(m_Context);
		}

		Frame(const Frame&) = delete;
		Frame& operator=(const Frame&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};

	float WidthOf(std::string_view text)
	{
		return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
	}
}

TEST_CASE("CentreLines puts a short message in the middle of the region both ways")
{
	Frame frame;
	const ImVec2 region(400.0f, 300.0f);

	const std::vector<CentredLine> lines = CentreLines("No commits yet.", region);

	REQUIRE(lines.size() == 1);
	CHECK(lines[0].Text == "No commits yet.");
	CHECK(lines[0].Offset.x == doctest::Approx((region.x - WidthOf("No commits yet.")) * 0.5f));
	CHECK(lines[0].Offset.y == doctest::Approx((region.y - ImGui::GetTextLineHeight()) * 0.5f));
}

TEST_CASE("CentreLines wraps a message wider than the region and centres every line on its own")
{
	Frame frame;
	const std::string message = "Select a commit or a file to see what changed.";
	const ImVec2 region(WidthOf(message) * 0.6f, 300.0f);

	const std::vector<CentredLine> lines = CentreLines(message, region);

	REQUIRE(lines.size() == 2);

	std::string joined;

	for (const CentredLine& line : lines)
	{
		CHECK(WidthOf(line.Text) <= region.x);
		CHECK(line.Text.front() != ' ');
		CHECK(line.Text.back() != ' ');
		CHECK(line.Offset.x == doctest::Approx((region.x - WidthOf(line.Text)) * 0.5f));

		joined += joined.empty() ? "" : " ";
		joined += line.Text;
	}

	CHECK(joined == message);

	const float lineHeight = ImGui::GetTextLineHeight();
	CHECK(lines[1].Offset.y - lines[0].Offset.y == doctest::Approx(lineHeight));
	CHECK(lines[0].Offset.y == doctest::Approx((region.y - lineHeight * 2.0f) * 0.5f));
}

TEST_CASE("CentreLines keeps room for what the caller draws under the message")
{
	Frame frame;
	const ImVec2 region(400.0f, 300.0f);
	const float below = 40.0f;

	const std::vector<CentredLine> lines = CentreLines("No folder open.", region, below);

	REQUIRE(lines.size() == 1);
	CHECK(lines[0].Offset.y == doctest::Approx((region.y - ImGui::GetTextLineHeight() - below) * 0.5f));
}

TEST_CASE("CentreLines starts a block bigger than the region at its top-left edge")
{
	Frame frame;
	const ImVec2 region(WidthOf("No"), 2.0f);

	const std::vector<CentredLine> lines = CentreLines("No commits yet.", region);

	REQUIRE(!lines.empty());
	CHECK(lines[0].Offset.y == 0.0f);

	for (const CentredLine& line : lines)
	{
		CHECK(line.Offset.x >= 0.0f);
	}
}

TEST_CASE("CentreLines breaks at a newline and returns nothing for no text")
{
	Frame frame;

	const std::vector<CentredLine> lines = CentreLines("First line.\nSecond.", ImVec2(400.0f, 300.0f));

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].Text == "First line.");
	CHECK(lines[1].Text == "Second.");

	CHECK(CentreLines("", ImVec2(400.0f, 300.0f)).empty());
}

TEST_CASE("EmptyState claims the whole region left in the window, so the window closes cleanly")
{
	Frame frame;

	ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
	ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
	ImGui::Begin("Empty");

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 available = ImGui::GetContentRegionAvail();

	CHECK_FALSE(Core::EmptyState("Nothing here.", "Open"));

	CHECK(ImGui::GetItemRectMin().y == doctest::Approx(origin.y));
	CHECK(ImGui::GetItemRectMax().y == doctest::Approx(origin.y + available.y));
	CHECK(ImGui::GetItemRectMax().x == doctest::Approx(origin.x + available.x));

	ImGui::End();
}

TEST_CASE("EmptyState claims past a region too short and too narrow for its message beside its button")
{
	Frame frame;

	ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
	ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
	ImGui::Begin("Short");

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImGuiStyle& style = ImGui::GetStyle();

	CHECK_FALSE(Core::EmptyState("Nothing here.", "Open", ImVec2(WidthOf("Nothing here."), 4.0f)));

	const float block = ImGui::GetTextLineHeight() + style.ItemSpacing.y + ImGui::GetFrameHeight();
	CHECK(ImGui::GetItemRectMax().y == doctest::Approx(origin.y + block));

	ImGui::End();
}

TEST_CASE("EmptyState puts its button beside the message in a region too short to stack them")
{
	Frame frame;

	ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
	ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
	ImGui::Begin("Low");

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float rowHeight = std::max(ImGui::GetTextLineHeight(), ImGui::GetFrameHeight());
	const ImVec2 region(360.0f, rowHeight + 2.0f);

	CHECK_FALSE(Core::EmptyState("File too large to preview (20 MB)", "Open", region));

	CHECK(ImGui::GetItemRectMax().y == doctest::Approx(origin.y + region.y));
	CHECK(ImGui::GetItemRectMax().x == doctest::Approx(origin.x + region.x));

	ImGui::End();
}
