#include <doctest/doctest.h>

#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"

#include <cstddef>
#include <cstdlib>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace {

	class IsolatedImGuiContext
	{
	public:
		IsolatedImGuiContext()
			: m_Context(ImGui::CreateContext())
		{
			ImGui::GetIO().IniFilename = nullptr;
		}

		~IsolatedImGuiContext()
		{
			ImGui::DestroyContext(m_Context);
		}

		IsolatedImGuiContext(const IsolatedImGuiContext&) = delete;
		IsolatedImGuiContext& operator=(const IsolatedImGuiContext&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};

	struct PaletteEntry
	{
		const char* Name;
		Core::Color Core::ChromePalette::* Member;
		float R;
		float G;
		float B;
		float A;
	};

	constexpr PaletteEntry kExpected[] = {
		{"Background", &Core::ChromePalette::Background, 0.078f, 0.082f, 0.094f, 1.0f},
		{"Surface", &Core::ChromePalette::Surface, 0.145f, 0.157f, 0.176f, 1.0f},
		{"SurfaceHovered", &Core::ChromePalette::SurfaceHovered, 0.196f, 0.212f, 0.239f, 1.0f},
		{"SurfaceActive", &Core::ChromePalette::SurfaceActive, 0.243f, 0.263f, 0.298f, 1.0f},
		{"Accent", &Core::ChromePalette::Accent, 0.259f, 0.522f, 0.957f, 1.0f},
		{"Text", &Core::ChromePalette::Text, 0.839f, 0.859f, 0.886f, 1.0f},
		{"TextDisabled", &Core::ChromePalette::TextDisabled, 0.482f, 0.510f, 0.553f, 1.0f},
		{"Border", &Core::ChromePalette::Border, 0.141f, 0.153f, 0.173f, 1.0f},
		{"Content", &Core::ChromePalette::Content, 0.106f, 0.114f, 0.129f, 1.0f},
	};

	Core::ChromePalette LightPalette()
	{
		Core::ChromePalette palette = Core::Theme::DefaultPalette();
		palette.Background = Core::Color{0.953f, 0.953f, 0.953f, 1.0f};
		palette.Surface = Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
		palette.SurfaceHovered = Core::Color{0.898f, 0.898f, 0.898f, 1.0f};
		palette.SurfaceActive = Core::Color{0.839f, 0.839f, 0.839f, 1.0f};
		palette.Accent = Core::Color{0.039f, 0.384f, 0.816f, 1.0f};
		palette.Text = Core::Color{0.141f, 0.141f, 0.141f, 1.0f};
		palette.TextDisabled = Core::Color{0.431f, 0.431f, 0.431f, 1.0f};
		palette.Border = Core::Color{0.784f, 0.784f, 0.784f, 1.0f};
		palette.Content = Core::Color{1.0f, 1.0f, 1.0f, 1.0f};

		return palette;
	}

	void CheckSameColour(const ImVec4& actual, const Core::Color& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.R));
		CHECK(actual.y == doctest::Approx(expected.G));
		CHECK(actual.z == doctest::Approx(expected.B));
		CHECK(actual.w == doctest::Approx(expected.A));
	}

	bool SameColour(const ImVec4& first, const Core::Color& second)
	{
		return first.x == doctest::Approx(second.R) && first.y == doctest::Approx(second.G) &&
			   first.z == doctest::Approx(second.B) && first.w == doctest::Approx(second.A);
	}

	struct ColourBytes
	{
		int R = 0;
		int G = 0;
		int B = 0;
	};

	ColourBytes BytesOf(ImU32 colour)
	{
		constexpr ImU32 kByteMask = 0xFFu;

		return ColourBytes{.R = static_cast<int>((colour >> IM_COL32_R_SHIFT) & kByteMask),
						   .G = static_cast<int>((colour >> IM_COL32_G_SHIFT) & kByteMask),
						   .B = static_cast<int>((colour >> IM_COL32_B_SHIFT) & kByteMask)};
	}

	// A docking context with fonts, so a dock space can be laid out and its splitters drawn.
	class DockingFrames
	{
	public:
		DockingFrames()
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.Fonts->Build();
		}

		~DockingFrames()
		{
			ImGui::DestroyContext(m_Context);
		}

		DockingFrames(const DockingFrames&) = delete;
		DockingFrames& operator=(const DockingFrames&) = delete;

		void Frame(Core::DockSpaceHost& host, const Core::DockLayout& layout)
		{
			ImGui::NewFrame();

			host.Begin(layout, {}, {});
			host.End();

			for (const std::string& panelName : layout.Left)
			{
				ImGui::Begin(panelName.c_str());
				ImGui::End();
			}

			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	struct SplitterDrawing
	{
		float Width = 0.0f;
		int VerticesInLineColour = 0;
	};

	// The splitter between the panel docked on the left and the rest of the dock space.
	SplitterDrawing DrawSplitterBesideAPanel(const Core::ChromePalette& palette)
	{
		constexpr int kSettleFrames = 3;

		DockingFrames frames;
		Core::Theme::Apply(palette);
		Core::Theme::SetMetrics(ImGui::GetStyle());

		Core::DockSpaceHost host;
		Core::DockLayout layout;
		layout.Left = {"Explorer"};

		for (int frame = 0; frame < kSettleFrames; ++frame)
		{
			frames.Frame(host, layout);
		}

		const ImGuiDockNode* root = ImGui::DockBuilderGetNode(host.GetCenterNodeId());
		REQUIRE(root != nullptr);
		while (root->ParentNode != nullptr)
		{
			root = root->ParentNode;
		}

		const ImGuiDockNode* panelSide = root->ChildNodes[0];
		const ImGuiDockNode* restSide = root->ChildNodes[1];
		REQUIRE(panelSide != nullptr);
		REQUIRE(restSide != nullptr);
		REQUIRE(root->HostWindow != nullptr);

		const float splitterLeft = panelSide->Pos.x + panelSide->Size.x;
		const float splitterRight = restSide->Pos.x;
		const ImU32 lineColour = ImGui::GetColorU32(Core::ToImVec4(palette.Border));

		SplitterDrawing drawing;
		drawing.Width = splitterRight - splitterLeft;

		for (const ImDrawVert& vertex : root->HostWindow->DrawList->VtxBuffer)
		{
			const bool onTheSplitter = vertex.pos.x >= splitterLeft && vertex.pos.x <= splitterRight;
			drawing.VerticesInLineColour += onTheSplitter && vertex.col == lineColour ? 1 : 0;
		}

		return drawing;
	}
}

TEST_CASE("The default chrome palette is the one the application shipped with")
{
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();

	for (const PaletteEntry& expected : kExpected)
	{
		CAPTURE(expected.Name);

		const Core::Color& color = palette.*expected.Member;

		CHECK(color.R == doctest::Approx(expected.R).epsilon(0.01));
		CHECK(color.G == doctest::Approx(expected.G).epsilon(0.01));
		CHECK(color.B == doctest::Approx(expected.B).epsilon(0.01));
		CHECK(color.A == doctest::Approx(expected.A).epsilon(0.01));
	}
}

TEST_CASE("The border accessor answers from the default palette")
{
	const Core::Color border = Core::Theme::GetBorderColor();
	const Core::Color expected = Core::Theme::DefaultPalette().Border;

	CHECK(border.R == doctest::Approx(expected.R));
	CHECK(border.G == doctest::Approx(expected.G));
	CHECK(border.B == doctest::Approx(expected.B));
	CHECK(border.A == doctest::Approx(expected.A));
}

TEST_CASE("Apply paints the title bar in the chrome and the empty dock space in the content colour")
{
	IsolatedImGuiContext context;
	Core::ChromePalette palette = Core::Theme::DefaultPalette();
	palette.Content = Core::Color{0.9f, 0.2f, 0.1f, 1.0f};

	Core::Theme::Apply(palette);

	const ImVec4* colours = ImGui::GetStyle().Colors;
	CHECK(colours[ImGuiCol_DockingEmptyBg].x == doctest::Approx(0.9f));
	CHECK(colours[ImGuiCol_DockingEmptyBg].z == doctest::Approx(0.1f));
	CHECK(colours[ImGuiCol_MenuBarBg].x == doctest::Approx(palette.Background.R));
	CHECK(colours[ImGuiCol_WindowBg].x == doctest::Approx(palette.Background.R));
}

TEST_CASE("The tab bar takes the chrome colour, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		for (const ImGuiCol slot : {ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_Tab,
									ImGuiCol_TabDimmed, ImGuiCol_TabSelected, ImGuiCol_TabDimmedSelected, ImGuiCol_TabHovered})
		{
			CAPTURE(slot);
			CheckSameColour(colours[slot], palette.Background);
		}

		CheckSameColour(colours[ImGuiCol_WindowBg], palette.Background);
	}
}

TEST_CASE("Hovered frames, headers and buttons take the hover surface, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		for (const ImGuiCol slot : {ImGuiCol_FrameBgHovered, ImGuiCol_HeaderHovered, ImGuiCol_ButtonHovered})
		{
			CAPTURE(slot);
			CheckSameColour(colours[slot], palette.SurfaceHovered);
		}
	}
}

TEST_CASE("Pressed frames, headers and buttons show the hover surface, never the accent, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		for (const ImGuiCol slot : {ImGuiCol_FrameBgActive, ImGuiCol_HeaderActive, ImGuiCol_ButtonActive})
		{
			CAPTURE(slot);
			CheckSameColour(colours[slot], palette.SurfaceHovered);
			CHECK_FALSE(SameColour(colours[slot], palette.Accent));
		}

		// A menu fills its open item, and a combo its current item, in Header: it keeps a fill.
		CheckSameColour(colours[ImGuiCol_Header], palette.SurfaceHovered);
	}
}

TEST_CASE("Borders and dock splitters take the line colour, and popups are told apart by their fill, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		CheckSameColour(colours[ImGuiCol_Border], palette.Border);
		CheckSameColour(colours[ImGuiCol_Separator], palette.Border);
		CheckSameColour(colours[ImGuiCol_PopupBg], palette.Surface);
	}
}

TEST_CASE("A dock splitter is one pixel wide and drawn in the line colour, in dark and in light")
{
	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);

		const SplitterDrawing splitter = DrawSplitterBesideAPanel(palette);

		CHECK(splitter.Width == doctest::Approx(1.0f));
		CHECK(splitter.VerticesInLineColour > 0);
	}
}

TEST_CASE("The focus ring and a held splitter take the accent, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		CheckSameColour(colours[ImGuiCol_NavCursor], palette.Accent);
		CheckSameColour(colours[ImGuiCol_ResizeGripActive], palette.Accent);
	}
}

TEST_CASE("Emphasis text is brighter than text in dark and darker in light")
{
	IsolatedImGuiContext context;

	Core::Theme::Apply(Core::Theme::DefaultPalette());
	const ColourBytes darkText = BytesOf(ImGui::GetColorU32(ImGuiCol_Text));
	const ColourBytes darkEmphasis = BytesOf(Core::EmphasisText());

	CHECK(darkEmphasis.R > darkText.R);
	CHECK(darkEmphasis.G > darkText.G);
	CHECK(darkEmphasis.B > darkText.B);

	constexpr int kMockupBrightRed = 0xEE;
	constexpr int kMockupBrightGreen = 0xF1;
	constexpr int kMockupBrightBlue = 0xF5;
	constexpr int kBytesOff = 2;
	CHECK(std::abs(darkEmphasis.R - kMockupBrightRed) <= kBytesOff);
	CHECK(std::abs(darkEmphasis.G - kMockupBrightGreen) <= kBytesOff);
	CHECK(std::abs(darkEmphasis.B - kMockupBrightBlue) <= kBytesOff);

	Core::Theme::Apply(LightPalette());
	const ColourBytes lightText = BytesOf(ImGui::GetColorU32(ImGuiCol_Text));
	const ColourBytes lightEmphasis = BytesOf(Core::EmphasisText());

	CHECK(lightEmphasis.R < lightText.R);
	CHECK(lightEmphasis.G < lightText.G);
	CHECK(lightEmphasis.B < lightText.B);

	constexpr int kLightTextPushedTowardBlack = 14;
	CHECK(std::abs(lightEmphasis.R - kLightTextPushedTowardBlack) <= kBytesOff);
}

TEST_CASE("Apply draws table headers in the surface colour and table borders in the border colour, in dark and in light")
{
	IsolatedImGuiContext context;

	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		const ImVec4* colours = ImGui::GetStyle().Colors;

		CheckSameColour(colours[ImGuiCol_TableHeaderBg], palette.Surface);
		CheckSameColour(colours[ImGuiCol_TableBorderStrong], palette.Border);
		CheckSameColour(colours[ImGuiCol_TableBorderLight], palette.Border);
	}
}

TEST_CASE("Apply clears the scrollbar track and draws its grab in the text colour")
{
	IsolatedImGuiContext context;
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();
	Core::Theme::Apply(palette);
	const ImVec4* colours = ImGui::GetStyle().Colors;

	CHECK(colours[ImGuiCol_ScrollbarBg].w == doctest::Approx(0.0f));
	CHECK(colours[ImGuiCol_ScrollbarGrab].x == doctest::Approx(palette.Text.R));
	CHECK(colours[ImGuiCol_ScrollbarGrab].w == doctest::Approx(0.12f));
	CHECK(colours[ImGuiCol_ScrollbarGrabHovered].w == doctest::Approx(0.20f));
	CHECK(colours[ImGuiCol_ScrollbarGrabActive].w == doctest::Approx(0.28f));
}

TEST_CASE("A background past half luminance is light and the default chrome's background is dark")
{
	constexpr Core::Color kLightSchemeBackground{0.953f, 0.953f, 0.953f, 1.0f};
	constexpr Core::Color kJustBelowHalf{0.49f, 0.49f, 0.49f, 1.0f};

	CHECK(Core::IsLightBackground(kLightSchemeBackground));
	CHECK_FALSE(Core::IsLightBackground(kJustBelowHalf));
	CHECK_FALSE(Core::IsLightBackground(Core::Theme::DefaultPalette().Background));
}
