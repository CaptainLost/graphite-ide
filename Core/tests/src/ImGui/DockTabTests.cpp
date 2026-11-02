#include <doctest/doctest.h>

#include "Core/ImGui/ContentWindowColours.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/DockTab.h"
#include "Core/ImGui/EmbeddedFonts.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/UI/PanelManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

	constexpr const char* kFloatingAlpha = ICON_LC_FILE "  Alpha###alpha";
	constexpr const char* kFloatingBeta = ICON_LC_FILE "  Beta###beta";
	const ImVec2 kFloatingNodePos(560.0f, 380.0f);
	const ImVec2 kFloatingNodeSize(300.0f, 160.0f);

	struct ScreenOptions
	{
		Core::ChromePalette Palette = Core::Theme::DefaultPalette();
		float Scale = 1.0f;
		// The monitor's scale, which ImGui gives the fonts of the windows on it, as the application has it do.
		float MonitorScale = 1.0f;
		bool FloatingPair = false;
		// More documents docked beside the first in the centre node.
		std::vector<std::string> MoreDocuments;
		// The share of the dock space the two panels' node takes; the document's node has the rest.
		float LeftShare = kLeftNodeShare;
	};

	// A window of the floating pair, and every close its owner was told of.
	struct FloatingTab
	{
		const char* Name = nullptr;
		bool Open = true;
		int Closes = 0;
	};

	// A dock space with two panels side by side in its left node and a document in its centre, under the theme, at
	// the interface size; with the floating pair, two more windows docked together in a node of their own.
	class TabBarScreen
	{
	public:
		explicit TabBarScreen(const ScreenOptions& options = {})
			: m_Context(ImGui::CreateContext()), m_FloatingPair(options.FloatingPair), m_MoreDocuments(options.MoreDocuments)
		{
			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(1000.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			inputOutput.ConfigDpiScaleFonts = true;
			ImGui::GetMainViewport()->DpiScale = options.MonitorScale;

			inputOutput.Fonts->AddFontDefault();
			ImFontConfig borrowed;
			borrowed.FontDataOwnedByAtlas = false;
			const Core::EmbeddedFont italic = Core::InterItalic();
			Italic = inputOutput.Fonts->AddFontFromMemoryCompressedTTF(italic.CompressedData, italic.CompressedSize,
																	   Core::kInterfaceFontSize, &borrowed);
			inputOutput.Fonts->Build();

			Core::Theme::Apply(options.Palette);
			Core::Theme::SetMetrics(ImGui::GetStyle());
			ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
			ImGui::GetStyle().FontScaleMain = options.Scale;

			Explorer = m_Panels.Add(std::make_unique<TitledPanel>("explorer", "Explorer"));
			Changes = m_Panels.Add(std::make_unique<TitledPanel>("changes", "Changes"));
			m_Layout.Left = {Explorer->GetWindowName(), Changes->GetWindowName()};
			m_Layout.LeftRatio = options.LeftShare;
			m_Layout.Center = {kDocumentWindow};
			m_Layout.Center.insert(m_Layout.Center.end(), m_MoreDocuments.begin(), m_MoreDocuments.end());

			Settle();
		}

		~TabBarScreen() { ImGui::DestroyContext(m_Context); }

		TabBarScreen(const TabBarScreen&) = delete;
		TabBarScreen& operator=(const TabBarScreen&) = delete;

		void Frame()
		{
			ImGui::NewFrame();

			if (m_FloatingPair && !m_FloatingNodeBuilt)
			{
				BuildFloatingNode();
			}

			m_Host.Begin(m_Layout);
			m_Host.End();
			m_Panels.OnImGuiRender(m_CloseRequests);

			if (m_FloatingPair)
			{
				DrawFloatingPair();
			}

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

			for (const std::string& name : m_MoreDocuments)
			{
				Core::BeginDockedWindow(name.c_str(), nullptr, ImGuiWindowFlags_None);
				Core::DrawDockTab(Core::DockTabLookOf(name), m_CloseRequests);
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

		static const ImGuiWindow& WindowNamed(const char* name)
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(name);
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
		FloatingTab Alpha{.Name = kFloatingAlpha};
		FloatingTab Beta{.Name = kFloatingBeta};

	private:
		void BuildFloatingNode()
		{
			const ImGuiID node = ImGui::DockBuilderAddNode(0, ImGuiDockNodeFlags_None);
			ImGui::DockBuilderSetNodePos(node, kFloatingNodePos);
			ImGui::DockBuilderSetNodeSize(node, kFloatingNodeSize);
			ImGui::DockBuilderDockWindow(kFloatingAlpha, node);
			ImGui::DockBuilderDockWindow(kFloatingBeta, node);
			ImGui::DockBuilderFinish(node);
			m_FloatingNodeBuilt = true;
		}

		// As an owner keeps its tabs: it asks after every one, closed or not, so a second close of the same tab is
		// counted rather than missed.
		void DrawFloatingPair()
		{
			for (FloatingTab* tab : {&Alpha, &Beta})
			{
				bool open = true;

				if (tab->Open)
				{
					Core::BeginDockedWindow(tab->Name, &open, ImGuiWindowFlags_None);
					Core::DrawDockTab(Core::DockTabLookOf(tab->Name), m_CloseRequests);
					ImGui::End();
				}

				const bool asked = m_CloseRequests.Take(tab->Name);
				tab->Closes += (open ? 0 : 1) + (asked ? 1 : 0);
				tab->Open = tab->Open && open && !asked;
			}
		}

		ImGuiContext* m_Context = nullptr;
		bool m_FloatingPair = false;
		bool m_FloatingNodeBuilt = false;
		std::vector<std::string> m_MoreDocuments;
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

TEST_CASE("In a bar too narrow for its tabs each tab keeps the start of its name and the bar scrolls")
{
	constexpr int kDocuments = 30;
	constexpr std::string_view kNameStartKept = "Controllin";

	ScreenOptions options;

	for (int document = 0; document < kDocuments; ++document)
	{
		options.MoreDocuments.push_back(std::string{ICON_LC_FILE} + "  ControllingDateResolver.cs###crowded" + std::to_string(document));
	}

	TabBarScreen screen(options);
	const ImGuiDockNode& node = TabBarScreen::NodeOf(TabBarScreen::DocumentWindow());
	const std::string& crowded = options.MoreDocuments.front();

	Core::DockTabLook nameless = Core::DockTabLookOf(crowded);
	nameless.Name.clear();

	float aroundTheName = 0.0f;
	float nameStartWidth = 0.0f;
	float fullWidth = 0.0f;
	screen.DuringFrame = [&] {
		aroundTheName = Core::DockTabWidth(nameless);
		nameStartWidth = ImGui::CalcTextSize(kNameStartKept.data(), kNameStartKept.data() + kNameStartKept.size()).x;
		fullWidth = Core::DockTabWidth(Core::DockTabLookOf(crowded));
	};
	screen.Frame();

	REQUIRE(fullWidth * static_cast<float>(kDocuments) > node.Size.x);
	CHECK(node.TabBar->ScrollButtonEnabled);

	int crowdedTabs = 0;

	for (const ImGuiTabItem& tab : node.TabBar->Tabs)
	{
		if (tab.Window == nullptr || std::string_view{tab.Window->Name}.find("###crowded") == std::string_view::npos)
		{
			continue;
		}

		++crowdedTabs;
		CHECK(tab.Width < fullWidth);
		CHECK(tab.Width - aroundTheName >= nameStartWidth);
	}

	CHECK(crowdedTabs == kDocuments);
}

TEST_CASE("A lone tab in a node narrower than the shrink floor keeps its close button inside the node")
{
	constexpr float kNarrowCentreShare = 0.08f;
	constexpr float kShrinkFloor = 160.0f;

	ScreenOptions options;
	options.LeftShare = 1.0f - kNarrowCentreShare;
	TabBarScreen screen(options);

	float fullWidth = 0.0f;
	screen.DuringFrame = [&] { fullWidth = Core::DockTabWidth(screen.DocumentLook); };
	screen.Frame();

	const ImGuiDockNode& node = TabBarScreen::NodeOf(TabBarScreen::DocumentWindow());
	REQUIRE(node.TabBar->Tabs.Size == 1);
	REQUIRE(node.Size.x < kShrinkFloor);
	REQUIRE(fullWidth > node.Size.x);

	const float barRight = node.TabBar->BarRect.Max.x;
	const ImVec2 closeCentre(barRight - 8.0f - 8.0f, node.TabBar->BarRect.GetCenter().y);

	screen.Click(closeCentre, ImGuiMouseButton_Left);

	CHECK_FALSE(screen.DocumentOpen);
}

TEST_CASE("On a monitor at 150 % a floating node's tabs shrink to the floor the dock space's do, at the monitor's scale")
{
	constexpr float kShrinkFloor = 160.0f;
	constexpr float kMonitorScale = 1.5f;

	TabBarScreen screen({.MonitorScale = kMonitorScale, .FloatingPair = true});

	// Registered after the dock space's own hook, so it reads the style the floating bars are laid out in.
	float floatingFloor = 0.0f;
	ImGuiContextHook reader;
	reader.Type = ImGuiContextHookType_NewFramePre;
	reader.UserData = &floatingFloor;
	reader.Callback = [](ImGuiContext*, ImGuiContextHook* hook) {
		*static_cast<float*>(hook->UserData) = ImGui::GetStyle().TabMinWidthShrink;
	};
	ImGui::AddContextHook(ImGui::GetCurrentContext(), &reader);

	float dockSpaceFloor = 0.0f;
	screen.DuringFrame = [&] { dockSpaceFloor = Core::Scaled(kShrinkFloor); };
	screen.Settle();

	CHECK(dockSpaceFloor == doctest::Approx(kShrinkFloor * kMonitorScale));
	CHECK(floatingFloor == doctest::Approx(dockSpaceFloor));
}

TEST_CASE("Tab bars are laid out with borderless overflow arrows, no gaps and no close button of ImGui's, and the style is put back")
{
	TabBarScreen screen;
	ImGuiStyle& style = ImGui::GetStyle();
	const ImGuiStyle before = style;

	for (const Core::DockTabBarEdge edge : {Core::DockTabBarEdge::AtTheNode, Core::DockTabBarEdge::InsideTheWindowBorder})
	{
		CAPTURE(static_cast<int>(edge));
		{
			const Core::DockTabBarStyle flat(edge);

			CHECK(style.FrameBorderSize == doctest::Approx(0.0f));
			CHECK(style.FramePadding.x == doctest::Approx(0.0f));
			CHECK(style.ItemInnerSpacing.x == doctest::Approx(0.0f));
			CHECK(style.TabCloseButtonMinWidthSelected == FLT_MAX);
			CHECK(style.TabCloseButtonMinWidthUnselected == FLT_MAX);
			CHECK(style.WindowBorderSize == doctest::Approx(edge == Core::DockTabBarEdge::AtTheNode ? 0.0f : before.WindowBorderSize));
		}

		CHECK(style.FrameBorderSize == doctest::Approx(before.FrameBorderSize));
		CHECK(style.FramePadding.x == doctest::Approx(before.FramePadding.x));
		CHECK(style.FramePadding.y == doctest::Approx(before.FramePadding.y));
		CHECK(style.ItemInnerSpacing.x == doctest::Approx(before.ItemInnerSpacing.x));
		CHECK(style.WindowBorderSize == doctest::Approx(before.WindowBorderSize));
		CHECK(style.TabCloseButtonMinWidthSelected == doctest::Approx(before.TabCloseButtonMinWidthSelected));
		CHECK(style.TabCloseButtonMinWidthUnselected == doctest::Approx(before.TabCloseButtonMinWidthUnselected));
		CHECK(style.TabMinWidthShrink == doctest::Approx(before.TabMinWidthShrink));
	}
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
		TabBarScreen screen({.Palette = palette});
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

TEST_CASE("At one and a half times the scale the close button's cross is drawn at its scaled size, inside its box")
{
	constexpr float kOneAndAHalf = 1.5f;
	constexpr float kCloseGlyphSize = 11.0f;
	constexpr float kCloseBoxSize = 16.0f;
	constexpr float kLabelPaddingRight = 8.0f;
	constexpr float kWithinAPixel = 0.5f;

	TabBarScreen screen({.Scale = kOneAndAHalf});

	float expectedHeight = 0.0f;
	screen.DuringFrame = [&] {
		ImGui::PushFont(nullptr, kCloseGlyphSize);
		unsigned int cross = 0;
		ImTextCharFromUtf8(&cross, ICON_LC_X, nullptr);
		// A face may hand back a bake of another size than the one asked for, which AddText scales to the size it is given.
		ImFontBaked* const baked = ImGui::GetFontBaked();
		const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(cross));
		REQUIRE(glyph != nullptr);
		expectedHeight = (glyph->Y1 - glyph->Y0) * ImGui::GetFontSize() / baked->Size;
		ImGui::PopFont();
	};
	screen.PointAt(ImVec2(990.0f, 590.0f));

	const ImGuiWindow& document = TabBarScreen::DocumentWindow();
	const ImRect tab = TabBarScreen::TabOf(document);
	const float boxSize = kCloseBoxSize * kOneAndAHalf;
	const float boxLeft = tab.Max.x - kLabelPaddingRight * kOneAndAHalf - boxSize;
	const float boxTop = std::floor(tab.Min.y + (tab.GetHeight() - boxSize) * 0.5f);
	const ImRect box(ImVec2(boxLeft, boxTop), ImVec2(boxLeft + boxSize, boxTop + boxSize));

	const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	ImRect cross(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
	const ImRect closeEnd(ImVec2(boxLeft - 4.0f, tab.Min.y), tab.Max);

	for (const ImDrawVert& vertex : TabBarScreen::BarDrawing(document).VtxBuffer)
	{
		if (vertex.col == dim && closeEnd.Contains(vertex.pos))
		{
			cross.Add(vertex.pos);
		}
	}

	REQUIRE(cross.Min.x <= cross.Max.x);
	CHECK(cross.GetHeight() == doctest::Approx(expectedHeight).epsilon(0.05));
	CHECK(cross.Min.x >= box.Min.x - kWithinAPixel);
	CHECK(cross.Max.x <= box.Max.x + kWithinAPixel);
	CHECK(cross.Min.y >= box.Min.y - kWithinAPixel);
	CHECK(cross.Max.y <= box.Max.y + kWithinAPixel);
}

TEST_CASE("Two windows docked together in a floating node get a 32 pixel bar, with tabs flat inside its border")
{
	TabBarScreen screen({.FloatingPair = true});
	screen.Settle();

	const ImGuiWindow& alpha = TabBarScreen::WindowNamed(kFloatingAlpha);
	const ImGuiWindow& beta = TabBarScreen::WindowNamed(kFloatingBeta);
	const ImGuiDockNode& node = TabBarScreen::NodeOf(alpha);
	REQUIRE(node.IsFloatingNode());
	REQUIRE(beta.DockNode == alpha.DockNode);

	CHECK(node.TabBar->BarRect.GetHeight() == doctest::Approx(kBarHeight));
	CHECK(alpha.TitleBarHeight == doctest::Approx(kBarHeight));

	const ImRect first = TabBarScreen::TabOf(alpha).Min.x < TabBarScreen::TabOf(beta).Min.x ? TabBarScreen::TabOf(alpha)
																							  : TabBarScreen::TabOf(beta);
	const ImRect second = TabBarScreen::TabOf(alpha).Min.x < TabBarScreen::TabOf(beta).Min.x ? TabBarScreen::TabOf(beta)
																							   : TabBarScreen::TabOf(alpha);
	CHECK(first.Min.x == doctest::Approx(node.Pos.x + node.HostWindow->WindowBorderSize));
	CHECK(second.Min.x == doctest::Approx(first.Max.x));
}

TEST_CASE("At an interface scale of 125 % or 150 %, or on a monitor at 150 %, a floating node's bar is as tall as the title bars of its windows")
{
	struct Scales
	{
		float Interface = 1.0f;
		float Monitor = 1.0f;
	};

	for (const Scales scales : {Scales{.Interface = 1.25f}, Scales{.Interface = 1.5f}, Scales{.Monitor = 1.5f}})
	{
		CAPTURE(scales.Interface);
		CAPTURE(scales.Monitor);

		TabBarScreen screen({.Scale = scales.Interface, .MonitorScale = scales.Monitor, .FloatingPair = true});
		float scaledBar = 0.0f;
		screen.DuringFrame = [&] { scaledBar = std::round(Core::Scaled(kBarHeight)); };
		screen.Settle();

		const ImGuiWindow& alpha = TabBarScreen::WindowNamed(kFloatingAlpha);
		const ImGuiWindow& beta = TabBarScreen::WindowNamed(kFloatingBeta);
		const ImGuiDockNode& node = TabBarScreen::NodeOf(alpha);
		REQUIRE(node.IsFloatingNode());
		REQUIRE(beta.DockNode == alpha.DockNode);

		const ImGuiWindow& selected = node.TabBar->VisibleTabId == alpha.TabId ? alpha : beta;

		CHECK(scaledBar == doctest::Approx(std::round(kBarHeight * scales.Interface * scales.Monitor)));
		CHECK(node.TabBar->BarRect.GetHeight() == doctest::Approx(scaledBar));
		CHECK(selected.TitleBarHeight == doctest::Approx(scaledBar));
	}
}

TEST_CASE("In a floating node ImGui's own close button never takes the pointer from the tab's")
{
	TabBarScreen screen({.FloatingPair = true});
	screen.Settle();

	const ImGuiWindow& alpha = TabBarScreen::WindowNamed(kFloatingAlpha);
	const ImGuiWindow& beta = TabBarScreen::WindowNamed(kFloatingBeta);
	const bool alphaSelected = alpha.DockNode->TabBar->VisibleTabId == alpha.TabId;
	const ImGuiWindow& selected = alphaSelected ? alpha : beta;
	const ImRect tab = TabBarScreen::TabOf(selected);

	screen.PointAt(ImVec2(tab.Max.x - 8.0f - 8.0f, tab.GetCenter().y));

	const ImGuiID imguiClose = ImGui::GetIDWithSeed("#CLOSE", nullptr, selected.ID);
	const ImGuiID ownClose = ImGui::GetIDWithSeed("#DockTabClose", nullptr, selected.ID);
	CHECK(ImGui::GetCurrentContext()->HoveredId != imguiClose);
	CHECK(ImGui::GetCurrentContext()->HoveredId == ownClose);
}

TEST_CASE("One middle click on a tab in a floating node that is not selected closes that tab, and only once")
{
	TabBarScreen screen({.FloatingPair = true});
	screen.Settle();

	const ImGuiWindow& alpha = TabBarScreen::WindowNamed(kFloatingAlpha);
	const bool alphaSelected = alpha.DockNode->TabBar->VisibleTabId == alpha.TabId;
	FloatingTab& clicked = alphaSelected ? screen.Beta : screen.Alpha;
	const FloatingTab& other = alphaSelected ? screen.Alpha : screen.Beta;
	const ImRect tab = TabBarScreen::TabOf(TabBarScreen::WindowNamed(clicked.Name));

	screen.Click(ImVec2(tab.Min.x + 14.0f, tab.GetCenter().y), ImGuiMouseButton_Middle);
	screen.Settle();

	CHECK_FALSE(clicked.Open);
	CHECK(clicked.Closes == 1);
	CHECK(other.Open);
	CHECK(other.Closes == 0);
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
