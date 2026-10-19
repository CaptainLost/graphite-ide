#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"

#include <cstddef>
#include <imgui.h>

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
		{"Border", &Core::ChromePalette::Border, 0.149f, 0.161f, 0.180f, 1.0f},
		{"Content", &Core::ChromePalette::Content, 0.106f, 0.114f, 0.129f, 1.0f},
	};
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

TEST_CASE("Apply draws tab bars a shade below the chrome and the selected and hovered tab in the chrome")
{
	IsolatedImGuiContext context;
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();
	Core::Theme::Apply(palette);
	const ImVec4* colours = ImGui::GetStyle().Colors;

	for (const ImGuiCol slot : {ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_Tab, ImGuiCol_TabDimmed})
	{
		CAPTURE(slot);
		CHECK(colours[slot].x == doctest::Approx(palette.Background.R - 0.03f));
		CHECK(colours[slot].z == doctest::Approx(palette.Background.B - 0.03f));
	}
	CHECK(colours[ImGuiCol_TabSelected].x == doctest::Approx(palette.Background.R));
	CHECK(colours[ImGuiCol_TabDimmedSelected].x == doctest::Approx(palette.Background.R));
	CHECK(colours[ImGuiCol_TabHovered].x == doctest::Approx(palette.Background.R));
	CHECK(colours[ImGuiCol_TabHovered].z == doctest::Approx(palette.Background.B));
}

TEST_CASE("Apply draws the edges of windows and popups in the active surface and separators in the border colour")
{
	IsolatedImGuiContext context;
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();
	Core::Theme::Apply(palette);
	const ImVec4* colours = ImGui::GetStyle().Colors;

	CHECK(colours[ImGuiCol_Border].x == doctest::Approx(palette.SurfaceActive.R));
	CHECK(colours[ImGuiCol_Border].y == doctest::Approx(palette.SurfaceActive.G));
	CHECK(colours[ImGuiCol_Border].z == doctest::Approx(palette.SurfaceActive.B));
	CHECK(colours[ImGuiCol_Separator].x == doctest::Approx(palette.Border.R));
	CHECK(colours[ImGuiCol_Separator].y == doctest::Approx(palette.Border.G));
	CHECK(colours[ImGuiCol_Separator].z == doctest::Approx(palette.Border.B));
}

TEST_CASE("The tab bar shade never goes below black")
{
	IsolatedImGuiContext context;
	Core::ChromePalette palette = Core::Theme::DefaultPalette();
	palette.Background = Core::Color{0.01f, 0.01f, 0.01f, 1.0f};
	Core::Theme::Apply(palette);

	CHECK(ImGui::GetStyle().Colors[ImGuiCol_TitleBg].x == doctest::Approx(0.0f));
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
