#include <doctest/doctest.h>

#include "App/Logo.h"
#include "App/TitleBar.h"
#include "App/Version.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/MenuItem.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/CaptionLayout.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/Caption.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"

#include "DrawnColours.h"
#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

using Graphite::TitleContext;

namespace {
	constexpr float kBarWidth = 1200.0f;
	constexpr float kMenusEnd = 300.0f;
	constexpr float kNarrowBarWidth = 520.0f;
	constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
											 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
	constexpr ImVec2 kWindowSize(kBarWidth, Graphite::kCaptionHeight);
	constexpr int kFramesPastTheHoverFade = 30;
	constexpr float kHalf = 0.5f;
	constexpr float kChannelBytes = 255.0f;
	constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
	constexpr ImVec2 kReferencePosition(0.0f, 100.0f);
	constexpr ImVec2 kReferenceSize(400.0f, 200.0f);

	// The mockup's tokens, in the dark scheme.
	constexpr ImU32 kMockupIconGrey = IM_COL32(0xc3, 0xc8, 0xcf, 0xff);
	constexpr ImU32 kMockupCloseRed = IM_COL32(0xc4, 0x2b, 0x1c, 0xff);
	constexpr ImU32 kMockupHover = IM_COL32(0x32, 0x36, 0x3d, 0xff);

	// The sRGB transfer curve and the contrast ratio as WCAG 2 defines them.
	constexpr float kLinearSegmentEnd = 0.04045f;
	constexpr float kLinearSegmentSlope = 12.92f;
	constexpr float kCurveOffset = 0.055f;
	constexpr float kCurveScale = 1.055f;
	constexpr float kCurveExponent = 2.4f;
	constexpr float kRedWeight = 0.2126f;
	constexpr float kGreenWeight = 0.7152f;
	constexpr float kBlueWeight = 0.0722f;
	constexpr float kFlare = 0.05f;

	float LinearChannel(int byte)
	{
		const float channel = static_cast<float>(byte) / kChannelBytes;
		return channel <= kLinearSegmentEnd ? channel / kLinearSegmentSlope
											: std::pow((channel + kCurveOffset) / kCurveScale, kCurveExponent);
	}

	float RelativeLuminance(ImU32 colour)
	{
		return kRedWeight * LinearChannel(ChannelOf(colour, IM_COL32_R_SHIFT)) +
			   kGreenWeight * LinearChannel(ChannelOf(colour, IM_COL32_G_SHIFT)) +
			   kBlueWeight * LinearChannel(ChannelOf(colour, IM_COL32_B_SHIFT));
	}

	float ContrastRatio(ImU32 first, ImU32 second)
	{
		const float lighter = std::max(RelativeLuminance(first), RelativeLuminance(second));
		const float darker = std::min(RelativeLuminance(first), RelativeLuminance(second));

		return (lighter + kFlare) / (darker + kFlare);
	}

	Core::CaptionLayout ButtonsAcrossTheBar()
	{
		return Core::LayOutCaption(kBarWidth, Core::Scaled(Graphite::kCaptionHeight), Core::Scaled(Graphite::kWindowButtonWidth),
								   kMenusEnd, {}, {});
	}

	ImVec2 CentreOf(const Core::CaptionRect& rect)
	{
		return ImVec2(rect.X + rect.Width * kHalf, rect.Y + rect.Height * kHalf);
	}

	bool Inside(const ImRect& box, const Core::CaptionRect& rect)
	{
		return box.Min.x >= rect.X && box.Max.x <= rect.X + rect.Width;
	}

	// The window buttons as the title bar draws them, in a window of their own across the top of the screen.
	class WindowButtonsScreen
	{
	public:
		explicit WindowButtonsScreen(const Graphite::ResolvedScheme& scheme) : m_Palette(Graphite::ChromePaletteFor(scheme))
		{
			Core::Theme::Apply(m_Palette);
		}

		void Draw(Core::WindowState state = Core::WindowState::Restored, const std::function<void()>& before = {})
		{
			m_Frames.Next([&] {
				Core::Animation::BeginFrame();

				if (before)
				{
					before();
				}

				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(kWindowSize);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
				ImGui::Begin("##windowButtonsTest", nullptr, kBareWindow);
				ImGui::PopStyleVar();

				if (m_Face != nullptr)
				{
					ImGui::PushFont(m_Face, Core::kInterfaceFontSize);
				}

				m_Pressed = Graphite::DrawWindowButtons(ButtonsAcrossTheBar(), state, false);
				m_GlyphColour = Graphite::WindowButtonGlyphColour();
				m_HoverFill = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
				m_Glyphs = DrawnGlyphs(*ImGui::GetWindowDrawList());

				if (m_Face != nullptr)
				{
					ImGui::PopFont();
				}

				ImGui::End();
			});
		}

		void HoverAt(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);

			for (int frame = 0; frame < kFramesPastTheHoverFade; ++frame)
			{
				Draw();
			}
		}

		// WARNING: Fonts added to a built atlas leave it unbuilt, and with no renderer backend a frame asserts on an
		// unbuilt atlas, so the atlas is built again once the font set is in it.
		void UseTheInterfaceFace()
		{
			m_Face = Core::LoadFontSet(*ImGui::GetIO().Fonts).Interface;
			ImGui::GetIO().Fonts->Build();
		}

		static const ImDrawList& Drawing()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName("##windowButtonsTest");
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}

		ImFont* GetFace() const { return m_Face; }
		const Core::ChromePalette& GetPalette() const { return m_Palette; }
		const std::vector<DrawnGlyph>& GetGlyphs() const { return m_Glyphs; }
		ImU32 GetGlyphColour() const { return m_GlyphColour; }
		ImU32 GetHoverFill() const { return m_HoverFill; }
		Graphite::WindowButton GetPressed() const { return m_Pressed; }

	private:
		ImGuiFrames m_Frames;
		Core::ChromePalette m_Palette;
		ImFont* m_Face = nullptr;
		Graphite::WindowButton m_Pressed = Graphite::WindowButton::None;
		std::vector<DrawnGlyph> m_Glyphs;
		ImU32 m_GlyphColour = 0;
		ImU32 m_HoverFill = 0;
	};

	// The logo as the title bar draws it at the start of its row, with anti-aliasing off so its vertices are exactly
	// the outline's corners.
	std::vector<ImDrawVert> DrawTheTitleLogo(const Core::ChromePalette& palette)
	{
		ImGuiFrames frames;
		Core::Theme::Apply(palette);
		ImGui::GetStyle().AntiAliasedFill = false;

		std::vector<ImDrawVert> vertices;
		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::Begin("##titleLogoTest", nullptr, kBareWindow);
			ImGui::PopStyleVar();

			const ImDrawList& drawing = *ImGui::GetWindowDrawList();
			const int before = drawing.VtxBuffer.Size;
			Graphite::DrawTitleLogo();
			vertices.assign(drawing.VtxBuffer.begin() + before, drawing.VtxBuffer.end());

			ImGui::End();
		});

		return vertices;
	}

	ImRect BoundsOf(const std::vector<ImDrawVert>& vertices)
	{
		ImRect bounds(vertices.front().pos, vertices.front().pos);

		for (const ImDrawVert& vertex : vertices)
		{
			bounds.Add(vertex.pos);
		}

		return bounds;
	}

	struct MenuFill
	{
		std::optional<VerticalExtent> Down;
		std::optional<HorizontalExtent> Across;
		float LabelWidth = 0.0f;
	};

	// A title-bar menu under the pointer, drawn by the menu bar with the title bar's heights, in a window that is
	// given its menu bar's height as the dock space host gives it. Anti-aliasing is off, so the rounded fill's
	// vertices lie on its edges rather than half a pixel inside them.
	MenuFill HoverATitleBarMenu()
	{
		constexpr const char* kLabel = "File";
		constexpr float kIntoTheLabel = 2.0f;
		constexpr const char* kWindowName = "##titleMenuTest";

		ImGuiFrames frames;
		Core::Theme::SetMetrics(ImGui::GetStyle());
		Core::Theme::Apply(Graphite::ChromePaletteFor(Graphite::DarkScheme()));
		ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
		ImGui::GetStyle().AntiAliasedFill = false;

		Core::CommandRegistry commands;
		commands.Add(Core::Command{.Id = "test.open", .Label = "Open"});
		const Core::Keymap keymap;
		const std::vector<Core::MenuItem> tree{Core::MenuItem::Submenu(kLabel, {Core::MenuItem::Command("test.open")})};
		Core::MenuBar bar;
		const Core::MenuBarHeights heights = Graphite::TitleMenuHeights();

		MenuFill fill;
		const auto draw = [&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kBarWidth, 200.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(ImGui::GetStyle().FramePadding.x, (heights.Row - ImGui::GetFontSize()) * kHalf));
			ImGui::Begin(kWindowName, nullptr, kBareWindow | ImGuiWindowFlags_MenuBar);
			ImGui::PopStyleVar();

			bar.Render(commands, keymap, tree, heights, nullptr, nullptr);
			fill.LabelWidth = ImGui::CalcTextSize(kLabel).x;

			const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
			fill.Down = VerticalExtentInColour(*ImGui::GetWindowDrawList(), hoverFill);
			fill.Across = HorizontalExtentInColour(*ImGui::GetWindowDrawList(), hoverFill);

			ImGui::End();
		};

		frames.Next(draw);
		const ImGuiStyle& style = ImGui::GetStyle();
		const float labelLeft = std::max(style.WindowPadding.x, style.ItemSpacing.x) + style.ItemSpacing.x * kHalf;
		ImGui::GetIO().AddMousePosEvent(labelLeft + kIntoTheLabel, heights.Row * kHalf);
		frames.Next(draw);
		frames.Next(draw);

		return fill;
	}

	struct Drawn
	{
		Core::CaptionLayout Layout;
		int Vertices = 0;
	};

	Drawn DrawTitleOnce(const TitleContext& context, float barWidth = kBarWidth)
	{
		ImGuiFrames frames;
		Drawn drawn;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(barWidth, kWindowSize.y));
			ImGui::Begin("##titleBarTest", nullptr, kBareWindow);

			const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			drawn.Layout = Graphite::DrawTitle(context, kMenusEnd, barWidth);
			drawn.Vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;

			ImGui::End();
		});

		return drawn;
	}
}

TEST_CASE("The title bar draws the folder and branch after the menus and no version")
{
	const Drawn none = DrawTitleOnce({});
	const Drawn folder = DrawTitleOnce({.Folder = "ab"});
	const Drawn longerFolder = DrawTitleOnce({.Folder = "abcd"});
	const Drawn both = DrawTitleOnce({.Folder = "ab", .Branch = "xy"});

	CHECK(folder.Layout.DividerVisible);
	CHECK(folder.Layout.FolderVisible);
	CHECK_FALSE(folder.Layout.BranchVisible);
	CHECK(both.Layout.BranchVisible);

	CHECK(folder.Layout.Divider.X >= kMenusEnd);
	CHECK(folder.Layout.Folder.X > folder.Layout.Divider.X);
	CHECK(both.Layout.Branch.X > both.Layout.Folder.X + both.Layout.Folder.Width);

	constexpr int kVerticesInAGlyph = 4;
	CHECK(longerFolder.Vertices - folder.Vertices == 2 * kVerticesInAGlyph);

	const int branchAndItsGlyph = both.Vertices - folder.Vertices;
	CHECK(branchAndItsGlyph == 3 * kVerticesInAGlyph);

	const std::string version = Graphite::Version::String();
	const int versionGlyphs = static_cast<int>(version.size()) + static_cast<int>(std::string("Graphite ").size());
	CHECK(folder.Vertices < none.Vertices + (versionGlyphs + 2) * kVerticesInAGlyph);
}

TEST_CASE("The title bar is twenty-eight pixels and its window buttons forty-two wide")
{
	constexpr float kTwentyEight = 28.0f;
	constexpr float kFortyTwo = 42.0f;

	const Drawn drawn = DrawTitleOnce({.Folder = "ab"});

	CHECK(Graphite::kCaptionHeight == kTwentyEight);

	for (const Core::CaptionRect& button : {drawn.Layout.Minimize, drawn.Layout.Maximize, drawn.Layout.Close})
	{
		CHECK(button.Height == doctest::Approx(kTwentyEight));
		CHECK(button.Width == doctest::Approx(kFortyTwo));
	}
}

TEST_CASE("With no folder, nothing follows the menus")
{
	const Drawn drawn = DrawTitleOnce({.Branch = "main"});

	CHECK_FALSE(drawn.Layout.DividerVisible);
	CHECK_FALSE(drawn.Layout.FolderVisible);
	CHECK_FALSE(drawn.Layout.BranchVisible);
	CHECK(drawn.Vertices == 0);
}

TEST_CASE("The labels are dropped before they would overlap the window buttons")
{
	const TitleContext context{.Folder = "a folder with a rather long name", .Branch = "feature/a-branch-with-a-long-name"};

	const Drawn roomy = DrawTitleOnce(context);
	const Drawn narrow = DrawTitleOnce(context, kNarrowBarWidth);

	CHECK(roomy.Layout.BranchVisible);
	CHECK_FALSE(narrow.Layout.BranchVisible);
	CHECK(narrow.Vertices < roomy.Vertices);

	if (narrow.Layout.FolderVisible)
	{
		CHECK(narrow.Layout.Folder.X + narrow.Layout.Folder.Width <= narrow.Layout.Minimize.X);
	}
}

TEST_CASE("The title-bar divider is drawn in the line colour, in dark and in light")
{
	for (const Graphite::ResolvedScheme* scheme : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		const Core::ChromePalette palette = Graphite::ChromePaletteFor(*scheme);
		CAPTURE(palette.Background.R);

		ImGuiFrames frames;
		Core::Theme::Apply(palette);

		Core::CaptionLayout layout;
		std::optional<HorizontalExtent> inLineColour;
		frames.Next([&] {
			// As the dock space host draws the title bar: with no border of its own, which is in the line colour too.
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::Begin("##titleBarDividerTest", nullptr, kBareWindow);
			ImGui::PopStyleVar();

			layout = Graphite::DrawTitle({.Folder = "ab"}, kMenusEnd, kBarWidth);
			inLineColour = HorizontalExtentInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(Core::ToImVec4(palette.Border)));

			ImGui::End();
		});

		REQUIRE(layout.DividerVisible);
		REQUIRE(inLineColour.has_value());
		const float dividerLeft = std::floor(layout.Divider.X);
		CHECK(inLineColour->Left == doctest::Approx(dividerLeft));
		CHECK(inLineColour->Right == doctest::Approx(dividerLeft + layout.Divider.Width));
	}
}

TEST_CASE("The title-bar divider is fourteen pixels tall, centred in the bar")
{
	constexpr float kFourteen = 14.0f;

	ImGuiFrames frames;
	Core::Theme::Apply(Graphite::ChromePaletteFor(Graphite::DarkScheme()));

	std::optional<VerticalExtent> inLineColour;
	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kWindowSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##titleBarDividerHeightTest", nullptr, kBareWindow);
		ImGui::PopStyleVar();

		Graphite::DrawTitle({.Folder = "ab"}, kMenusEnd, kBarWidth);
		inLineColour = VerticalExtentInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(ImGuiCol_Separator));

		ImGui::End();
	});

	REQUIRE(inLineColour.has_value());
	CHECK(inLineColour->Bottom - inLineColour->Top == doctest::Approx(kFourteen));
	CHECK(inLineColour->Top == doctest::Approx((Graphite::kCaptionHeight - kFourteen) * kHalf));
}

TEST_CASE("Title-bar menus fill twenty-two pixels under the pointer, centred, and eight past their labels")
{
	constexpr float kTwentyTwo = 22.0f;
	constexpr float kEight = 8.0f;

	const MenuFill fill = HoverATitleBarMenu();

	CHECK(Graphite::kMenuItemHeight == kTwentyTwo);
	REQUIRE(fill.Down.has_value());
	REQUIRE(fill.Across.has_value());
	CHECK(fill.Down->Bottom - fill.Down->Top == doctest::Approx(kTwentyTwo));
	CHECK(fill.Down->Top == doctest::Approx((Graphite::kCaptionHeight - kTwentyTwo) * kHalf));
	CHECK(fill.Across->Right - fill.Across->Left == doctest::Approx(fill.LabelWidth + kEight * 2.0f));
}

TEST_CASE("Window buttons draw in the icon grey, in dark and in light")
{
	constexpr float kIconGreyShare = 0.21f;
	constexpr float kShareTolerance = 0.03f;
	constexpr std::size_t kThreeGlyphs = 3;

	for (const Graphite::ResolvedScheme* scheme : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		WindowButtonsScreen screen(*scheme);
		CAPTURE(screen.GetPalette().Background.R);
		screen.Draw();

		REQUIRE(screen.GetGlyphs().size() == kThreeGlyphs);
		for (const DrawnGlyph& glyph : screen.GetGlyphs())
		{
			CHECK(glyph.Colour == screen.GetGlyphColour());
		}

		const ImU32 text = ImGui::GetColorU32(Core::ToImVec4(screen.GetPalette().Text));
		const ImU32 disabled = ImGui::GetColorU32(Core::ToImVec4(screen.GetPalette().TextDisabled));
		CHECK(ShareFromTo(screen.GetGlyphColour(), text, disabled) == doctest::Approx(kIconGreyShare).epsilon(kShareTolerance));
	}

	WindowButtonsScreen dark(Graphite::DarkScheme());
	dark.Draw();
	CHECK(WithinOneLevel(dark.GetGlyphColour(), kMockupIconGrey));
}

TEST_CASE("Window-button glyphs are fourteen pixels, whatever the interface size")
{
	constexpr float kFourteen = 14.0f;

	WindowButtonsScreen screen(Graphite::DarkScheme());
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

	std::vector<DrawnGlyph> references;
	screen.Draw(Core::WindowState::Restored, [&] {
		ImGui::SetNextWindowPos(kReferencePosition);
		ImGui::SetNextWindowSize(kReferenceSize);
		ImGui::Begin("##glyphReference", nullptr, kBareWindow);
		ImGui::PushFont(nullptr, kFourteen);
		ImGui::TextUnformatted(ICON_LC_MINUS);
		ImGui::PopFont();
		ImGui::TextUnformatted(ICON_LC_MINUS);
		references = DrawnGlyphs(*ImGui::GetWindowDrawList());
		ImGui::End();
	});

	REQUIRE(references.size() == 2);
	const float atFourteen = references[0].Box.GetHeight();
	const float atTheInterfaceSize = references[1].Box.GetHeight();
	REQUIRE(atFourteen < atTheInterfaceSize);
	REQUIRE_FALSE(screen.GetGlyphs().empty());
	for (const DrawnGlyph& glyph : screen.GetGlyphs())
	{
		CHECK(glyph.Box.GetHeight() == doctest::Approx(atFourteen));
	}
}

TEST_CASE("The close button hovers red with a white glyph, the others in the hover surface with their grey one")
{
	WindowButtonsScreen screen(Graphite::DarkScheme());
	const Core::CaptionLayout layout = ButtonsAcrossTheBar();

	screen.Draw();
	screen.HoverAt(CentreOf(layout.Close));

	CHECK(Graphite::kCloseHoveredFill == kMockupCloseRed);
	CHECK(DrawsInColour(WindowButtonsScreen::Drawing(), kMockupCloseRed));
	CHECK_FALSE(DrawsInColour(WindowButtonsScreen::Drawing(), screen.GetHoverFill()));

	for (const DrawnGlyph& glyph : screen.GetGlyphs())
	{
		const bool onClose = Inside(glyph.Box, layout.Close);
		CAPTURE(glyph.Box.Min.x);
		CHECK(glyph.Colour == (onClose ? kWhite : screen.GetGlyphColour()));
	}

	screen.HoverAt(CentreOf(layout.Minimize));

	CHECK(screen.GetHoverFill() == kMockupHover);
	CHECK(DrawsInColour(WindowButtonsScreen::Drawing(), kMockupHover));
	CHECK_FALSE(DrawsInColour(WindowButtonsScreen::Drawing(), kMockupCloseRed));
	for (const DrawnGlyph& glyph : screen.GetGlyphs())
	{
		CHECK(glyph.Colour == screen.GetGlyphColour());
	}
}

TEST_CASE("The middle window button shows the restore glyph on a maximised window and the maximise glyph otherwise")
{
	WindowButtonsScreen screen(Graphite::DarkScheme());
	screen.UseTheInterfaceFace();

	const Core::CaptionLayout layout = ButtonsAcrossTheBar();
	ImVec2 restoreGlyph;
	ImVec2 maximiseGlyph;
	const auto drawTheReferences = [&] {
		ImGui::SetNextWindowPos(kReferencePosition);
		ImGui::SetNextWindowSize(kReferenceSize);
		ImGui::Begin("##glyphReference", nullptr, kBareWindow);
		ImGui::PushFont(screen.GetFace(), Graphite::kWindowButtonGlyphSize);
		for (const char* glyph : {ICON_LC_MINUS, ICON_LC_X, ICON_LC_COPY, ICON_LC_SQUARE})
		{
			ImGui::TextUnformatted(glyph);
		}
		ImGui::PopFont();

		const std::vector<DrawnGlyph> references = DrawnGlyphs(*ImGui::GetWindowDrawList());
		REQUIRE(references.size() == 4);
		restoreGlyph = references[2].Uv;
		maximiseGlyph = references[3].Uv;
		ImGui::End();
	};

	const auto middleGlyph = [&]() -> std::optional<ImVec2> {
		for (const DrawnGlyph& glyph : screen.GetGlyphs())
		{
			if (Inside(glyph.Box, layout.Maximize))
			{
				return glyph.Uv;
			}
		}

		return std::nullopt;
	};

	// The first frame bakes every glyph into the atlas, so none moves in it while the second is read.
	screen.Draw(Core::WindowState::Maximized, drawTheReferences);
	screen.Draw(Core::WindowState::Maximized, drawTheReferences);

	REQUIRE(restoreGlyph.x != maximiseGlyph.x);
	REQUIRE(middleGlyph().has_value());
	CHECK(middleGlyph()->x == doctest::Approx(restoreGlyph.x));
	CHECK(middleGlyph()->y == doctest::Approx(restoreGlyph.y));

	screen.Draw(Core::WindowState::Restored, drawTheReferences);

	REQUIRE(middleGlyph().has_value());
	CHECK(middleGlyph()->x == doctest::Approx(maximiseGlyph.x));
	CHECK(middleGlyph()->y == doctest::Approx(maximiseGlyph.y));
}

TEST_CASE("The logo is seventeen pixels tall, centred in the bar")
{
	constexpr float kSeventeen = 17.0f;

	const std::vector<ImDrawVert> vertices = DrawTheTitleLogo(Graphite::ChromePaletteFor(Graphite::DarkScheme()));

	REQUIRE_FALSE(vertices.empty());
	const ImRect bounds = BoundsOf(vertices);
	CHECK(bounds.GetHeight() == doctest::Approx(kSeventeen).epsilon(0.01));
	CHECK(bounds.GetCenter().y == doctest::Approx(Graphite::kCaptionHeight * kHalf));
}

TEST_CASE("The logo is visible on the light scheme")
{
	constexpr float kLeastContrast = 1.5f;

	const Core::ChromePalette palette = Graphite::ChromePaletteFor(Graphite::LightScheme());
	const std::vector<ImDrawVert> vertices = DrawTheTitleLogo(palette);
	const ImU32 background = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(palette.Background));

	REQUIRE_FALSE(vertices.empty());
	float leastContrast = ContrastRatio(vertices.front().col, background);
	for (const ImDrawVert& vertex : vertices)
	{
		leastContrast = std::min(leastContrast, ContrastRatio(vertex.col, background));
	}

	CHECK(leastContrast >= kLeastContrast);
}

TEST_CASE("On a dark chrome the logo keeps its own colours")
{
	const std::vector<ImDrawVert> inTheBar = DrawTheTitleLogo(Graphite::ChromePaletteFor(Graphite::DarkScheme()));
	REQUIRE_FALSE(inTheBar.empty());

	ImGuiFrames frames;
	ImGui::GetStyle().AntiAliasedFill = false;

	std::vector<ImDrawVert> asDrawn;
	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kWindowSize);
		ImGui::Begin("##plainLogoTest", nullptr, kBareWindow);

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		const int before = drawing.VtxBuffer.Size;
		const ImRect bounds = BoundsOf(inTheBar);
		Graphite::DrawLogo(ImGui::GetWindowDrawList(), bounds.GetCenter(), bounds.GetHeight());
		asDrawn.assign(drawing.VtxBuffer.begin() + before, drawing.VtxBuffer.end());

		ImGui::End();
	});

	REQUIRE(asDrawn.size() == inTheBar.size());
	for (std::size_t vertex = 0; vertex < asDrawn.size(); ++vertex)
	{
		CAPTURE(vertex);
		CHECK(WithinOneLevel(inTheBar[vertex].col, asDrawn[vertex].col));
	}
}

TEST_CASE("The folder is named by the last part of its path")
{
	CHECK(Graphite::FolderLabel("D:/Repos/IDE") == "IDE");
	CHECK(Graphite::FolderLabel("D:/Repos/IDE/") == "IDE");
	CHECK(Graphite::FolderLabel("D:/") == "D:/");
}
