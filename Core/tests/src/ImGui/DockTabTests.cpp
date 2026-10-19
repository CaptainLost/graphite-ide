#include <doctest/doctest.h>

#include "Core/ImGui/ContentWindowColours.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/DockTab.h"
#include "Core/ImGui/EmbeddedFonts.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/ImGui/Theme.h"
#include "Core/UI/PanelManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
	constexpr float kBarHeight = 32.0f;
	constexpr int kSettleFrames = 6;
	// Wide enough for both panels' tabs at their full width, so the bar need not shrink them.
	constexpr float kLeftNodeShare = 0.5f;
	constexpr ImU32 kModifiedColour = IM_COL32(229, 192, 123, 255);
	constexpr const char* kDocumentWindow = ICON_LC_FILE "  Report.md###document";

	class TitledPanel final : public Core::Panel
	{
	public:
		TitledPanel(std::string id, std::string title)
			: Core::Panel(std::move(id), std::move(title), "tools", ICON_LC_FOLDER)
		{
		}
	};

	// A dock space with two panels side by side in its left node and a document in its centre, under the theme, at
	// the interface size.
	Core::ChromePalette LightPalette()
	{
		Core::ChromePalette palette = Core::Theme::DefaultPalette();
		palette.Background = Core::Color{0.953f, 0.953f, 0.953f, 1.0f};
		palette.Surface = Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
		palette.SurfaceHovered = Core::Color{0.898f, 0.898f, 0.898f, 1.0f};
		palette.SurfaceActive = Core::Color{0.839f, 0.839f, 0.839f, 1.0f};
		palette.Text = Core::Color{0.141f, 0.141f, 0.141f, 1.0f};
		palette.TextDisabled = Core::Color{0.431f, 0.431f, 0.431f, 1.0f};
		palette.Border = Core::Color{0.784f, 0.784f, 0.784f, 1.0f};
		palette.Content = Core::Color{1.0f, 1.0f, 1.0f, 1.0f};

		return palette;
	}

	class TabBarScreen
	{
	public:
		explicit TabBarScreen(const Core::ChromePalette& palette = Core::Theme::DefaultPalette())
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(1000.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

			inputOutput.Fonts->AddFontDefault();
			ImFontConfig borrowed;
			borrowed.FontDataOwnedByAtlas = false;
			const Core::EmbeddedFont italic = Core::InterItalic();
			Italic = inputOutput.Fonts->AddFontFromMemoryCompressedTTF(italic.CompressedData, italic.CompressedSize,
																	   Core::kInterfaceFontSize, &borrowed);
			inputOutput.Fonts->Build();

			Core::Theme::Apply(palette);
			Core::Theme::SetMetrics(ImGui::GetStyle());
			ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

			Explorer = m_Panels.Add(std::make_unique<TitledPanel>("explorer", "Explorer"));
			Changes = m_Panels.Add(std::make_unique<TitledPanel>("changes", "Changes"));
			m_Layout.Left = {Explorer->GetWindowName(), Changes->GetWindowName()};
			m_Layout.LeftRatio = kLeftNodeShare;
			m_Layout.Center = {kDocumentWindow};

			Settle();
		}

		~TabBarScreen() { ImGui::DestroyContext(m_Context); }

		TabBarScreen(const TabBarScreen&) = delete;
		TabBarScreen& operator=(const TabBarScreen&) = delete;

		void Frame()
		{
			ImGui::NewFrame();
			m_Host.Begin(m_Layout);
			m_Host.End();
			m_Panels.OnImGuiRender(m_CloseRequests);

			if (DocumentOpen)
			{
				bool documentOpen = true;
				Core::PushContentWindowColours();
				Core::BeginDockedWindow(kDocumentWindow, &documentOpen, ImGuiWindowFlags_None);
				Core::PopContentWindowColours();
				Core::DrawDockTab(DocumentLook, m_CloseRequests);
				DocumentTabHovered = ImGui::IsItemHovered();
				Core::DrawTabContextMenu(m_CloseRequests);
				DocumentOpen = documentOpen && !m_CloseRequests.Take(kDocumentWindow);
				ImGui::End();
			}

			if (DuringFrame)
			{
				DuringFrame();
			}

			m_CloseRequests.EndFrame();
			ImGui::EndFrame();
		}

		void Settle()
		{
			for (int frame = 0; frame < kSettleFrames; ++frame)
			{
				Frame();
			}
		}

		void PointAt(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Frame();
			Frame();
		}

		void Click(ImVec2 at, ImGuiMouseButton button)
		{
			PointAt(at);
			ImGui::GetIO().AddMouseButtonEvent(button, true);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(button, false);
			Frame();
			Frame();
		}

		static const ImGuiWindow& WindowOf(const Core::Panel& panel)
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(panel.GetWindowName().c_str());
			REQUIRE(window != nullptr);
			return *window;
		}

		static const ImGuiWindow& DocumentWindow()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(kDocumentWindow);
			REQUIRE(window != nullptr);
			return *window;
		}

		static ImRect TabOf(const ImGuiWindow& window) { return window.DC.DockTabItemRect; }

		static const ImGuiDockNode& NodeOf(const ImGuiWindow& window)
		{
			REQUIRE(window.DockNode != nullptr);
			REQUIRE(window.DockNode->TabBar != nullptr);
			return *window.DockNode;
		}

		static const ImDrawList& BarDrawing(const ImGuiWindow& window)
		{
			const ImGuiDockNode& node = NodeOf(window);
			REQUIRE(node.HostWindow != nullptr);
			return *node.HostWindow->DrawList;
		}

		Core::Panel* Explorer = nullptr;
		Core::Panel* Changes = nullptr;
		ImFont* Italic = nullptr;
		bool DocumentOpen = true;
		bool DocumentTabHovered = false;
		Core::DockTabLook DocumentLook = Core::DockTabLookOf(kDocumentWindow);
		std::function<void()> DuringFrame;

	private:
		ImGuiContext* m_Context = nullptr;
		Core::DockSpaceHost m_Host;
		Core::DockLayout m_Layout;
		Core::PanelManager m_Panels;
		Core::TabCloseRequests m_CloseRequests;
	};

	int VerticesInColour(const ImDrawList& drawing, ImU32 colour, const ImRect& within)
	{
		int count = 0;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			count += vertex.col == colour && within.Contains(vertex.pos) ? 1 : 0;
		}

		return count;
	}

	bool Covers(const ImVec2& first, const ImVec2& second, const ImVec2& third, const ImVec2& point)
	{
		const auto side = [](const ImVec2& from, const ImVec2& to, const ImVec2& at) {
			return (to.x - from.x) * (at.y - from.y) - (to.y - from.y) * (at.x - from.x);
		};
		const float a = side(first, second, point);
		const float b = side(second, third, point);
		const float c = side(third, first, point);

		return (a >= 0.0f && b >= 0.0f && c >= 0.0f) || (a <= 0.0f && b <= 0.0f && c <= 0.0f);
	}

	// The colour of the last triangle drawn over the point, which is what shows there when every fill is opaque.
	std::optional<ImU32> ColourShownAt(const ImDrawList& drawing, ImVec2 point)
	{
		std::optional<ImU32> shown;

		for (const ImDrawCmd& command : drawing.CmdBuffer)
		{
			for (unsigned int element = 0; element + 2 < command.ElemCount; element += 3)
			{
				const unsigned int start = command.IdxOffset + element;
				const ImDrawVert& first = drawing.VtxBuffer[drawing.IdxBuffer[start] + command.VtxOffset];
				const ImDrawVert& second = drawing.VtxBuffer[drawing.IdxBuffer[start + 1] + command.VtxOffset];
				const ImDrawVert& third = drawing.VtxBuffer[drawing.IdxBuffer[start + 2] + command.VtxOffset];

				if (Covers(first.pos, second.pos, third.pos, point))
				{
					shown = first.col;
				}
			}
		}

		return shown;
	}

	// The tab's left part, clear of its close button: where its glyph and name are drawn.
	ImRect LabelOf(const ImRect& tab)
	{
		constexpr float kCloseAndPadding = 30.0f;
		return ImRect(tab.Min, ImVec2(tab.Max.x - kCloseAndPadding, tab.Max.y));
	}

	ImU32 LineColour()
	{
		return ImGui::GetColorU32(ImGuiCol_Border);
	}
}

TEST_CASE("A dock tab bar is 32 pixels tall and its window's content starts below it")
{
	TabBarScreen screen;
	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const ImGuiDockNode& node = TabBarScreen::NodeOf(explorer);

	CHECK(node.TabBar->BarRect.GetHeight() == doctest::Approx(kBarHeight));
	CHECK(TabBarScreen::TabOf(explorer).GetHeight() == doctest::Approx(kBarHeight));
	CHECK(explorer.TitleBarHeight == doctest::Approx(kBarHeight));
	CHECK(explorer.InnerRect.Min.y >= node.Pos.y + kBarHeight);
	CHECK(explorer.InnerRect.Min.y <= node.Pos.y + kBarHeight + 1.0f);
}

TEST_CASE("Tabs touch and the first starts at the node's left edge")
{
	TabBarScreen screen;
	const ImRect explorer = TabBarScreen::TabOf(TabBarScreen::WindowOf(*screen.Explorer));
	const ImRect changes = TabBarScreen::TabOf(TabBarScreen::WindowOf(*screen.Changes));
	const ImGuiDockNode& node = TabBarScreen::NodeOf(TabBarScreen::WindowOf(*screen.Explorer));

	const ImRect& first = explorer.Min.x < changes.Min.x ? explorer : changes;
	const ImRect& second = explorer.Min.x < changes.Min.x ? changes : explorer;

	CHECK(first.Min.x == doctest::Approx(node.Pos.x));
	CHECK(second.Min.x == doctest::Approx(first.Max.x));
}

TEST_CASE("A tab is as wide as its padding, glyph, name, gaps and close button")
{
	TabBarScreen screen;
	const Core::DockTabLook look = Core::DockTabLookOf(screen.Explorer->GetWindowName());

	float expected = 0.0f;
	float measured = 0.0f;
	screen.DuringFrame = [&] {
		expected = std::ceil(12.0f + ImGui::CalcTextSize(look.Glyph.c_str()).x + 6.0f +
							 ImGui::CalcTextSize(look.Name.c_str()).x + 6.0f + 16.0f + 8.0f);
		measured = Core::DockTabWidth(look);
	};
	screen.Frame();

	CHECK(measured == doctest::Approx(expected));
	CHECK(TabBarScreen::TabOf(TabBarScreen::WindowOf(*screen.Explorer)).GetWidth() == doctest::Approx(expected));
}

TEST_CASE("A window's name gives its tab the glyph before the name and the words after it")
{
	const Core::DockTabLook look = Core::DockTabLookOf(ICON_LC_FOLDER "  Explorer###explorer");
	CHECK(look.Glyph == ICON_LC_FOLDER);
	CHECK(look.Name == "Explorer");

	const Core::DockTabLook plain = Core::DockTabLookOf("Notes##second");
	CHECK(plain.Glyph.empty());
	CHECK(plain.Name == "Notes");
}

TEST_CASE("An inactive tab's label is dim and the active one bright")
{
	TabBarScreen screen;
	screen.PointAt(ImVec2(900.0f, 500.0f));

	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const ImGuiWindow& changes = TabBarScreen::WindowOf(*screen.Changes);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	const ImGuiWindow& active = explorerSelected ? explorer : changes;
	const ImGuiWindow& inactive = explorerSelected ? changes : explorer;
	const ImDrawList& drawing = TabBarScreen::BarDrawing(explorer);
	const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 bright = Core::EmphasisText();

	CHECK(VerticesInColour(drawing, dim, LabelOf(TabBarScreen::TabOf(inactive))) > 0);
	CHECK(VerticesInColour(drawing, bright, LabelOf(TabBarScreen::TabOf(active))) > 0);
	CHECK(VerticesInColour(drawing, bright, LabelOf(TabBarScreen::TabOf(inactive))) == 0);
}

TEST_CASE("An inactive tab under the pointer takes the text colour and no fill of its own")
{
	TabBarScreen screen;
	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const ImGuiWindow& changes = TabBarScreen::WindowOf(*screen.Changes);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	const ImGuiWindow& inactive = explorerSelected ? changes : explorer;

	const ImRect tab = TabBarScreen::TabOf(inactive);
	screen.PointAt(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y));

	const ImDrawList& drawing = TabBarScreen::BarDrawing(inactive);
	const ImRect label = LabelOf(TabBarScreen::TabOf(inactive));
	const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 hoverSurface = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

	CHECK(VerticesInColour(drawing, text, label) > 0);
	CHECK(VerticesInColour(drawing, hoverSurface, label) == 0);
}

TEST_CASE("A preview tab's label is italic")
{
	TabBarScreen screen;
	REQUIRE(screen.Italic != nullptr);
	screen.DocumentLook.Face = screen.Italic;
	screen.Settle();

	const ImFontGlyph* const italicR = screen.Italic->GetFontBaked(Core::kInterfaceFontSize)->FindGlyphNoFallback('R');
	REQUIRE(italicR != nullptr);

	const ImGuiWindow& document = TabBarScreen::DocumentWindow();
	const ImRect tab = TabBarScreen::TabOf(document);
	bool drawnItalic = false;

	for (const ImDrawVert& vertex : TabBarScreen::BarDrawing(document).VtxBuffer)
	{
		const bool inTheTab = tab.Contains(vertex.pos);
		const bool italicCorner = vertex.uv.x == italicR->U0 && vertex.uv.y == italicR->V0;
		drawnItalic = drawnItalic || (inTheTab && italicCorner);
	}

	CHECK(drawnItalic);
}

TEST_CASE("A modified file's tab shows a dot in the modified colour after its name")
{
	TabBarScreen screen;
	screen.DocumentLook.Dot = kModifiedColour;
	screen.Settle();

	float nameWidth = 0.0f;
	screen.DuringFrame = [&] { nameWidth = ImGui::CalcTextSize(ICON_LC_FILE).x + 6.0f + ImGui::CalcTextSize("Report.md").x; };
	screen.Frame();

	const ImGuiWindow& document = TabBarScreen::DocumentWindow();
	const ImRect tab = TabBarScreen::TabOf(document);
	const ImRect afterTheName(ImVec2(tab.Min.x + 12.0f + nameWidth, tab.Min.y), tab.Max);
	CHECK(VerticesInColour(TabBarScreen::BarDrawing(document), kModifiedColour, afterTheName) > 0);
}

TEST_CASE("A line separates tabs and runs under the bar except beneath the active tab")
{
	TabBarScreen screen;
	screen.PointAt(ImVec2(900.0f, 500.0f));

	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const ImGuiWindow& changes = TabBarScreen::WindowOf(*screen.Changes);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	const ImRect active = TabBarScreen::TabOf(explorerSelected ? explorer : changes);
	const ImRect inactive = TabBarScreen::TabOf(explorerSelected ? changes : explorer);
	const ImGuiDockNode& node = TabBarScreen::NodeOf(explorer);
	const ImDrawList& drawing = TabBarScreen::BarDrawing(explorer);
	const ImU32 line = LineColour();

	constexpr float kInsideTheHairline = 0.5f;

	for (const ImRect& tab : {active, inactive})
	{
		CHECK(ColourShownAt(drawing, ImVec2(tab.Max.x - kInsideTheHairline, tab.GetCenter().y)) == line);
	}

	const float lineMiddle = active.Max.y - kInsideTheHairline;
	const float barRight = node.Pos.x + node.Size.x;
	const float lastTabRight = std::max(active.Max.x, inactive.Max.x);

	CHECK(ColourShownAt(drawing, ImVec2(inactive.GetCenter().x, lineMiddle)) == line);
	CHECK(ColourShownAt(drawing, ImVec2((lastTabRight + barRight) * 0.5f, lineMiddle)) == line);
	CHECK(ColourShownAt(drawing, ImVec2(active.GetCenter().x, lineMiddle)) != line);
}

TEST_CASE("A selected chrome-window tab and a selected document tab both stand out from the bar, in dark and in light")
{
	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		TabBarScreen screen(palette);
		screen.PointAt(ImVec2(900.0f, 500.0f));

		const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
		const ImGuiWindow& changes = TabBarScreen::WindowOf(*screen.Changes);
		const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
		const ImGuiWindow& selectedPanel = explorerSelected ? explorer : changes;
		const ImGuiWindow& document = TabBarScreen::DocumentWindow();

		// A point in a tab's top-left corner, clear of its glyph, its name and the line under it.
		const auto tabFill = [](const ImGuiWindow& window) {
			const ImRect tab = TabBarScreen::TabOf(window);
			return ColourShownAt(TabBarScreen::BarDrawing(window), ImVec2(tab.Min.x + 4.0f, tab.Min.y + 4.0f));
		};

		const ImGuiDockNode& panelNode = TabBarScreen::NodeOf(explorer);
		const float panelBarRight = panelNode.Pos.x + panelNode.Size.x;
		const ImRect panelTab = TabBarScreen::TabOf(selectedPanel);
		const std::optional<ImU32> bar = ColourShownAt(TabBarScreen::BarDrawing(explorer),
													   ImVec2(panelBarRight - 4.0f, panelTab.Min.y + 4.0f));

		REQUIRE(bar.has_value());
		CHECK(bar == std::optional<ImU32>{ImGui::GetColorU32(ImGuiCol_TitleBgActive)});
		CHECK(tabFill(selectedPanel) == std::optional<ImU32>{ImGui::GetColorU32(ImGuiCol_WindowBg)});
		CHECK(tabFill(selectedPanel) != bar);
		CHECK(tabFill(document) == std::optional<ImU32>{ImGui::GetColorU32(ImGuiCol_DockingEmptyBg)});
		CHECK(tabFill(document) != bar);
	}
}

TEST_CASE("An inactive tab is the bar's colour")
{
	TabBarScreen screen;
	screen.PointAt(ImVec2(900.0f, 500.0f));

	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const ImGuiWindow& changes = TabBarScreen::WindowOf(*screen.Changes);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	const ImRect inactive = TabBarScreen::TabOf(explorerSelected ? changes : explorer);

	CHECK(ColourShownAt(TabBarScreen::BarDrawing(explorer), ImVec2(inactive.Min.x + 4.0f, inactive.Min.y + 4.0f)) ==
		  std::optional<ImU32>{ImGui::GetColorU32(ImGuiCol_Tab)});
}

TEST_CASE("The close button on the active tab closes it, through the close requests")
{
	TabBarScreen screen;
	const ImGuiWindow& document = TabBarScreen::DocumentWindow();
	const ImRect tab = TabBarScreen::TabOf(document);
	const ImVec2 closeCentre(tab.Max.x - 8.0f - 8.0f, tab.GetCenter().y);

	screen.Click(closeCentre, ImGuiMouseButton_Left);

	CHECK_FALSE(screen.DocumentOpen);
}

TEST_CASE("A middle click on a tab that is not selected closes it")
{
	TabBarScreen screen;
	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	Core::Panel& inactivePanel = explorerSelected ? *screen.Changes : *screen.Explorer;
	const ImRect tab = TabBarScreen::TabOf(TabBarScreen::WindowOf(inactivePanel));

	screen.Click(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y), ImGuiMouseButton_Middle);

	CHECK_FALSE(inactivePanel.IsVisible());
}

TEST_CASE("After its tab is drawn, a window's tab is still the item under the pointer, for a double click to read")
{
	TabBarScreen screen;
	const ImRect tab = TabBarScreen::TabOf(TabBarScreen::DocumentWindow());

	screen.PointAt(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y));

	CHECK(screen.DocumentTabHovered);
}

TEST_CASE("A right click on a drawn tab still opens the tab's menu")
{
	TabBarScreen screen;
	const ImRect tab = TabBarScreen::TabOf(TabBarScreen::DocumentWindow());

	screen.Click(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y), ImGuiMouseButton_Right);

	CHECK(ImGui::IsPopupOpen("##tabContextMenu", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
}

TEST_CASE("A click on a tab's name still selects the tab")
{
	TabBarScreen screen;
	const ImGuiWindow& explorer = TabBarScreen::WindowOf(*screen.Explorer);
	const bool explorerSelected = explorer.DockNode->TabBar->VisibleTabId == explorer.TabId;
	const ImGuiWindow& inactive = explorerSelected ? TabBarScreen::WindowOf(*screen.Changes) : explorer;
	const ImRect tab = TabBarScreen::TabOf(inactive);

	screen.Click(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y), ImGuiMouseButton_Left);

	CHECK(inactive.DockNode->TabBar->VisibleTabId == inactive.TabId);
}
