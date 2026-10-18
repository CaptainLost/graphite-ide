#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"
#include "Core/ImGui/ToolStrip.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using Core::LayOutToolStrip;
using Core::ToolStripContent;
using Core::ToolStripItem;

namespace {

	constexpr float kFrameSeconds = 1.0f / 60.0f;
	constexpr float kStripHeight = 600.0f;
	const ImVec2 kOrigin(0.0f, 0.0f);
	const ImVec2 kStripWindowSize(42.0f, 600.0f);
	const ImVec2 kEditorPosition(100.0f, 0.0f);
	const ImVec2 kEditorSize(300.0f, 300.0f);

	class StripWindow
	{
	public:
		StripWindow()
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = kFrameSeconds;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~StripWindow() { ImGui::DestroyContext(m_Context); }

		StripWindow(const StripWindow&) = delete;
		StripWindow& operator=(const StripWindow&) = delete;

		struct Drawn
		{
			std::optional<std::string> Clicked;
			int Vertices = 0;
			float GlyphHeight = 0.0f;
			std::vector<ImDrawVert> Mesh;
			std::vector<ImDrawVert> GlyphMesh;
		};

		Drawn Draw(const ToolStripContent& content, ImVec2 windowSize = kStripWindowSize)
		{
			ImGui::NewFrame();
			BeginStripWindow(windowSize);

			const ImDrawList* drawList = ImGui::GetWindowDrawList();
			const int verticesBefore = drawList->VtxBuffer.Size;

			Drawn drawn;
			drawn.Clicked = Core::DrawToolStrip(content, Core::Theme::DefaultPalette());
			drawn.Vertices = drawList->VtxBuffer.Size - verticesBefore;
			drawn.GlyphHeight = GlyphHeight(*drawList, verticesBefore);
			drawn.Mesh.assign(drawList->VtxBuffer.Data + verticesBefore, drawList->VtxBuffer.Data + drawList->VtxBuffer.Size);
			drawn.GlyphMesh = GlyphVertices(*drawList, drawn.Mesh);

			ImGui::End();
			ImGui::EndFrame();

			return drawn;
		}

		enum class EditorFocus
		{
			Leave,
			Take
		};

		struct DrawnBesideEditor
		{
			std::optional<std::string> Clicked;
			bool EditorFocused = false;
		};

		DrawnBesideEditor DrawBesideEditor(const ToolStripContent& content, EditorFocus focus = EditorFocus::Leave)
		{
			ImGui::NewFrame();

			if (focus == EditorFocus::Take)
			{
				ImGui::SetNextWindowFocus();
			}

			ImGui::SetNextWindowPos(kEditorPosition);
			ImGui::SetNextWindowSize(kEditorSize);
			ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoSavedSettings);

			DrawnBesideEditor drawn;
			drawn.EditorFocused = ImGui::IsWindowFocused();
			ImGui::End();

			BeginStripWindow(kStripWindowSize);
			drawn.Clicked = Core::DrawToolStrip(content, Core::Theme::DefaultPalette());
			ImGui::End();
			ImGui::EndFrame();

			return drawn;
		}

		void MoveMouse(ImVec2 position) { ImGui::GetIO().AddMousePosEvent(position.x, position.y); }

		void PressMouse() { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true); }

		void ReleaseMouse() { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); }

	private:
		static void BeginStripWindow(ImVec2 windowSize)
		{
			ImGui::SetNextWindowPos(kOrigin);
			ImGui::SetNextWindowSize(windowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("Strip", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar();
		}

		static float GlyphHeight(const ImDrawList& drawList, int firstVertex)
		{
			const ImVec2 whitePixel = drawList._Data->TexUvWhitePixel;
			float top = 0.0f;
			float bottom = 0.0f;
			bool found = false;

			for (int index = firstVertex; index < drawList.VtxBuffer.Size; ++index)
			{
				const ImDrawVert& vertex = drawList.VtxBuffer[index];
				const bool sampledFromFill = vertex.uv.x == whitePixel.x && vertex.uv.y == whitePixel.y;

				if (sampledFromFill)
				{
					continue;
				}

				top = found ? std::min(top, vertex.pos.y) : vertex.pos.y;
				bottom = found ? std::max(bottom, vertex.pos.y) : vertex.pos.y;
				found = true;
			}

			return bottom - top;
		}

		static std::vector<ImDrawVert> GlyphVertices(const ImDrawList& drawList, const std::vector<ImDrawVert>& mesh)
		{
			const ImVec2 whitePixel = drawList._Data->TexUvWhitePixel;
			std::vector<ImDrawVert> glyph;
			std::copy_if(mesh.begin(), mesh.end(), std::back_inserter(glyph), [whitePixel](const ImDrawVert& vertex)
						 { return vertex.uv.x != whitePixel.x || vertex.uv.y != whitePixel.y; });
			return glyph;
		}


		ImGuiContext* m_Context = nullptr;
	};

	ToolStripItem Item(const char* id)
	{
		return ToolStripItem{.Id = id, .Icon = "M", .Tooltip = id};
	}

	std::vector<ToolStripItem> Group(std::initializer_list<const char*> ids)
	{
		std::vector<ToolStripItem> group;

		for (const char* id : ids)
		{
			group.push_back(Item(id));
		}

		return group;
	}

	std::vector<ImDrawVert> InColour(const std::vector<ImDrawVert>& mesh, const Core::Color& colour)
	{
		const ImU32 packed = ImGui::ColorConvertFloat4ToU32(ImVec4(colour.R, colour.G, colour.B, colour.A));
		std::vector<ImDrawVert> found;
		std::copy_if(mesh.begin(), mesh.end(), std::back_inserter(found),
					 [packed](const ImDrawVert& vertex) { return vertex.col == packed; });
		return found;
	}

	void CheckRect(const ImRect& actual, float minX, float minY, float maxX, float maxY)
	{
		CHECK(actual.Min.x == doctest::Approx(minX));
		CHECK(actual.Min.y == doctest::Approx(minY));
		CHECK(actual.Max.x == doctest::Approx(maxX));
		CHECK(actual.Max.y == doctest::Approx(maxY));
	}
}

TEST_CASE("Items stack from 6 px down, 30 px tall, 4 px apart and centred in the strip")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a", "b"})};

	const std::vector<ImRect> rects = LayOutToolStrip(content, kOrigin, kStripHeight);

	REQUIRE(rects.size() == 2);
	CheckRect(rects[0], 6.0f, 6.0f, 36.0f, 36.0f);
	CheckRect(rects[1], 6.0f, 40.0f, 36.0f, 70.0f);
}

TEST_CASE("A new group starts below a separator with 4 px on either side")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a"}), Group({"b"})};

	const std::vector<ImRect> rects = LayOutToolStrip(content, kOrigin, kStripHeight);

	REQUIRE(rects.size() == 2);
	CheckRect(rects[1], 6.0f, 45.0f, 36.0f, 75.0f);
}

TEST_CASE("The bottom block ends 6 px above the strip's bottom")
{
	StripWindow window;
	ToolStripContent content;
	content.Bottom = {Group({"a", "b"})};

	const std::vector<ImRect> rects = LayOutToolStrip(content, kOrigin, kStripHeight);

	REQUIRE(rects.size() == 2);
	CheckRect(rects[0], 6.0f, 530.0f, 36.0f, 560.0f);
	CheckRect(rects[1], 6.0f, 564.0f, 36.0f, 594.0f);
}

TEST_CASE("On a strip too short for both blocks the bottom block follows the top one")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a", "b", "c", "d", "e"})};
	content.Bottom = {Group({"f", "g"})};

	const std::vector<ImRect> rects = LayOutToolStrip(content, kOrigin, 200.0f);

	REQUIRE(rects.size() == 7);
	CHECK(rects[5].Min.y == doctest::Approx(rects[4].Max.y + 9.0f));

	for (std::size_t first = 0; first < rects.size(); ++first)
	{
		for (std::size_t second = first + 1; second < rects.size(); ++second)
		{
			CHECK_FALSE(rects[first].Overlaps(rects[second]));
		}
	}
}

TEST_CASE("The strip is 42 px wide scaled by the interface scale")
{
	StripWindow window;
	ImGui::GetStyle().FontScaleMain = 1.5f;
	ToolStripContent content;
	content.Top = {Group({"a"})};

	const std::vector<ImRect> rects = LayOutToolStrip(content, kOrigin, kStripHeight);

	CHECK(Core::ToolStripWidth() == 63.0f);
	REQUIRE(rects.size() == 1);
	CheckRect(rects[0], 9.0f, 9.0f, 54.0f, 54.0f);
}

TEST_CASE("Clicking an item returns its id")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a", "b"})};

	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.MoveMouse(ImVec2(21.0f, 21.0f));
	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.PressMouse();
	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.ReleaseMouse();
	CHECK(window.Draw(content).Clicked == std::optional<std::string>("a"));
}

TEST_CASE("Clicking between items returns nothing")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a", "b"})};

	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.MoveMouse(ImVec2(21.0f, 38.0f));
	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.PressMouse();
	CHECK_FALSE(window.Draw(content).Clicked.has_value());

	window.ReleaseMouse();
	CHECK_FALSE(window.Draw(content).Clicked.has_value());
}

TEST_CASE("A click on an item leaves the keyboard focus with the window that had it")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a", "b"})};

	window.DrawBesideEditor(content);
	window.DrawBesideEditor(content, StripWindow::EditorFocus::Take);
	REQUIRE(window.DrawBesideEditor(content).EditorFocused);

	window.MoveMouse(ImVec2(21.0f, 21.0f));
	window.DrawBesideEditor(content);

	window.PressMouse();
	window.DrawBesideEditor(content);

	window.ReleaseMouse();
	CHECK(window.DrawBesideEditor(content).Clicked == std::optional<std::string>("a"));
	CHECK(window.DrawBesideEditor(content).EditorFocused);
}

TEST_CASE("An open item is marked by an accent bar at the strip's left edge, not by a fill")
{
	StripWindow window;
	ToolStripContent open;
	open.Top = {Group({"a"})};
	open.Top[0][0].Open = true;
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();

	const std::vector<ImDrawVert> mesh = window.Draw(open).Mesh;
	const std::vector<ImDrawVert> bar = InColour(mesh, palette.Accent);

	REQUIRE_FALSE(bar.empty());
	for (const ImDrawVert& vertex : bar)
	{
		CHECK(vertex.pos.x >= 0.0f);
		CHECK(vertex.pos.x <= 2.0f);
		CHECK(vertex.pos.y >= 6.0f + 5.0f);
		CHECK(vertex.pos.y <= 6.0f + 30.0f - 5.0f);
	}
	CHECK(InColour(mesh, palette.SurfaceActive).empty());
}

TEST_CASE("At 150 % the open bar is 3 whole pixels wide and stays at the strip's left edge")
{
	StripWindow window;
	ImGui::GetStyle().FontScaleMain = 1.5f;
	ToolStripContent open;
	open.Top = {Group({"a"})};
	open.Top[0][0].Open = true;

	const std::vector<ImDrawVert> bar =
		InColour(window.Draw(open, ImVec2(63.0f, kStripHeight)).Mesh, Core::Theme::DefaultPalette().Accent);

	REQUIRE_FALSE(bar.empty());
	for (const ImDrawVert& vertex : bar)
	{
		CHECK(vertex.pos.x >= 0.0f);
		CHECK(vertex.pos.x <= 3.0f);
	}
}

TEST_CASE("A badge draws a dot that an item without one does not")
{
	StripWindow window;
	ToolStripContent closed;
	closed.Top = {Group({"a"})};
	ToolStripContent badged = closed;
	badged.Top[0][0].Badge = IM_COL32(255, 0, 0, 255);

	const int closedVertices = window.Draw(closed).Vertices;
	const int badgedVertices = window.Draw(badged).Vertices;

	CHECK(badgedVertices > closedVertices);
}

TEST_CASE("A hovered closed item takes the hover fill and brightens its glyph to the text colour")
{
	StripWindow window;
	ToolStripContent closed;
	closed.Top = {Group({"a"})};
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();
	const ImU32 text = ImGui::ColorConvertFloat4ToU32(ImVec4(palette.Text.R, palette.Text.G, palette.Text.B, palette.Text.A));

	window.MoveMouse(ImVec2(21.0f, 21.0f));
	window.Draw(closed);
	const StripWindow::Drawn drawn = window.Draw(closed);

	CHECK_FALSE(InColour(drawn.Mesh, palette.SurfaceHovered).empty());
	REQUIRE_FALSE(drawn.GlyphMesh.empty());
	for (const ImDrawVert& vertex : drawn.GlyphMesh)
	{
		CHECK(vertex.col == text);
	}
}

TEST_CASE("The strip draws no border at its right edge")
{
	StripWindow window;
	ToolStripContent closed;
	closed.Top = {Group({"a"})};

	CHECK(InColour(window.Draw(closed).Mesh, Core::Theme::DefaultPalette().Border).empty());
}

TEST_CASE("The icon is scaled once by the interface scale and not twice")
{
	StripWindow window;
	ToolStripContent content;
	content.Top = {Group({"a"})};

	const float glyphAtOne = window.Draw(content).GlyphHeight;
	ImGui::GetStyle().FontScaleMain = 1.5f;
	const float glyphAtOneAndAHalf = window.Draw(content, ImVec2(63.0f, kStripHeight)).GlyphHeight;

	REQUIRE(glyphAtOne > 0.0f);
	CHECK(glyphAtOneAndAHalf / glyphAtOne == doctest::Approx(1.5f).epsilon(0.15));
}
