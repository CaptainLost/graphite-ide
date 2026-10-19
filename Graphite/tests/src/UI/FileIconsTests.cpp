#include <doctest/doctest.h>

#include "UI/FileIcons.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>

namespace {

	using Graphite::FileKind;

	constexpr float kHalf = 0.5f;
	constexpr float kOnePixel = 1.0f;
	constexpr ImVec2 kWindowSize{300.0f, 200.0f};
	constexpr ImVec2 kIconPoint{40.0f, 50.0f};

	struct ExtensionCase
	{
		const char* Path;
		FileKind Kind;
	};

	constexpr ExtensionCase kExtensionCases[] = {
		{"Main.cpp", FileKind::CppSource},
		{"Main.cc", FileKind::CppSource},
		{"Main.cxx", FileKind::CppSource},
		{"main.c", FileKind::CppSource},
		{"Main.h", FileKind::Header},
		{"Main.hpp", FileKind::Header},
		{"Main.hxx", FileKind::Header},
		{"Program.cs", FileKind::CSharp},
		{"graphite.json", FileKind::Json},
		{"README.md", FileKind::Markdown},
		{"premake5.lua", FileKind::Lua},
		{"New-Adr.ps1", FileKind::Shell},
		{"Module.psm1", FileKind::Shell},
		{"Module.psd1", FileKind::Shell},
		{"GenerateProjects.bat", FileKind::Shell},
		{"build.cmd", FileKind::Shell},
		{"Graphite.sln", FileKind::Project},
		{"Graphite.slnx", FileKind::Project},
		{"Core.vcxproj", FileKind::Project},
		{"Tools.csproj", FileKind::Project},
		{"logo.png", FileKind::Image},
		{"photo.jpg", FileKind::Image},
		{"photo.jpeg", FileKind::Image},
		{"spinner.gif", FileKind::Image},
		{"splash.bmp", FileKind::Image},
		{"Graphite.ico", FileKind::Image},
		{"logo.svg", FileKind::Image},
		{"banner.webp", FileKind::Image},
	};

	struct GlyphCase
	{
		FileKind Kind;
		const char* Glyph;
	};

	constexpr GlyphCase kGlyphCases[] = {
		{FileKind::CppSource, ICON_MDI_LANGUAGE_CPP},
		{FileKind::Header, ICON_MDI_ALPHA_H_BOX_OUTLINE},
		{FileKind::CSharp, ICON_MDI_LANGUAGE_CSHARP},
		{FileKind::Json, ICON_MDI_CODE_JSON},
		{FileKind::Markdown, ICON_MDI_LANGUAGE_MARKDOWN_OUTLINE},
		{FileKind::Lua, ICON_MDI_LANGUAGE_LUA},
		{FileKind::Shell, ICON_MDI_POWERSHELL},
		{FileKind::Project, ICON_MDI_MICROSOFT_VISUAL_STUDIO},
		{FileKind::Image, ICON_MDI_FILE_IMAGE_OUTLINE},
		{FileKind::Other, ICON_MDI_FILE_OUTLINE},
	};

	struct ColourCase
	{
		FileKind Kind;
		ImU32 Dark;
		ImU32 Light;
	};

	constexpr ColourCase kColourCases[] = {
		{FileKind::CppSource, IM_COL32(0x6A, 0xA7, 0xE6, 0xFF), IM_COL32(0x1E, 0x64, 0xB4, 0xFF)},
		{FileKind::Header, IM_COL32(0xB3, 0x92, 0xE8, 0xFF), IM_COL32(0x7A, 0x46, 0xC8, 0xFF)},
		{FileKind::CSharp, IM_COL32(0x7F, 0xBF, 0x6A, 0xFF), IM_COL32(0x2E, 0x7D, 0x32, 0xFF)},
		{FileKind::Json, IM_COL32(0xD8, 0xB2, 0x5A, 0xFF), IM_COL32(0x9A, 0x74, 0x00, 0xFF)},
		{FileKind::Markdown, IM_COL32(0x8F, 0xA3, 0xB8, 0xFF), IM_COL32(0x55, 0x69, 0x7E, 0xFF)},
		{FileKind::Lua, IM_COL32(0x6C, 0x8C, 0xE0, 0xFF), IM_COL32(0x2F, 0x4F, 0xB0, 0xFF)},
		{FileKind::Shell, IM_COL32(0x5F, 0xA9, 0xD6, 0xFF), IM_COL32(0x1F, 0x6F, 0x9E, 0xFF)},
		{FileKind::Project, IM_COL32(0xA7, 0x7F, 0xE0, 0xFF), IM_COL32(0x6B, 0x3F, 0xB0, 0xFF)},
		{FileKind::Image, IM_COL32(0xD0, 0xA0, 0x60, 0xFF), IM_COL32(0x9A, 0x6A, 0x20, 0xFF)},
	};

	bool SameGlyph(const char* left, const char* right)
	{
		return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
	}

	struct DrawnIcon
	{
		ImU32 Expected = 0;
		float FontSize = 0.0f;
		std::optional<HorizontalExtent> Extent;
	};

	// WARNING: The expected colour is asked for inside the frame, after the drawing, because it is derived from the
	// style that the drawing may have changed.
	DrawnIcon DrawOneIcon(const std::function<void()>& drawIcon, const std::function<ImU32()>& expectedColour)
	{
		ImGuiFrames frames;
		DrawnIcon drawn;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("##iconWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);
			drawIcon();
			drawn.Expected = expectedColour();
			drawn.FontSize = ImGui::GetFontSize();
			drawn.Extent = HorizontalExtentInColour(*ImGui::GetWindowDrawList(), drawn.Expected);
			ImGui::End();
		});

		return drawn;
	}
}

TEST_CASE("KindOfFile tells each kind of file by its extension, a header apart from a source")
{
	for (const ExtensionCase& expected : kExtensionCases)
	{
		CAPTURE(expected.Path);
		CHECK(Graphite::KindOfFile(expected.Path) == expected.Kind);
	}
}

TEST_CASE("KindOfFile reads an extension in any case and calls anything it does not know Other")
{
	CHECK(Graphite::KindOfFile("C:/src/MAIN.CPP") == FileKind::CppSource);
	CHECK(Graphite::KindOfFile("C:/src/Main.H") == FileKind::Header);
	CHECK(Graphite::KindOfFile("notes.txt") == FileKind::Other);
	CHECK(Graphite::KindOfFile("tool.py") == FileKind::Other);
	CHECK(Graphite::KindOfFile("Core.vcxproj.filters") == FileKind::Other);
	CHECK(Graphite::KindOfFile("LICENSE") == FileKind::Other);
	CHECK(Graphite::KindOfFile("") == FileKind::Other);
}

TEST_CASE("GlyphOf gives each kind its glyph, and a header another glyph than a source")
{
	for (const GlyphCase& expected : kGlyphCases)
	{
		CAPTURE(static_cast<int>(expected.Kind));
		CHECK(SameGlyph(Graphite::GlyphOf(expected.Kind), expected.Glyph));
	}

	CHECK_FALSE(SameGlyph(Graphite::GlyphOf(FileKind::Header), Graphite::GlyphOf(FileKind::CppSource)));
}

TEST_CASE("Folders are drawn in outline, open and closed")
{
	constexpr bool kOpen = true;
	constexpr bool kClosed = false;

	CHECK(SameGlyph(Graphite::FolderGlyphOf(kOpen), ICON_MDI_FOLDER_OPEN_OUTLINE));
	CHECK(SameGlyph(Graphite::FolderGlyphOf(kClosed), ICON_MDI_FOLDER_OUTLINE));
}

TEST_CASE("ColourOf returns the table's colour for each kind, dark and light")
{
	constexpr bool kLightScheme = true;
	constexpr bool kDarkScheme = false;

	for (const ColourCase& expected : kColourCases)
	{
		CAPTURE(static_cast<int>(expected.Kind));
		CHECK(Graphite::ColourOf(expected.Kind, kDarkScheme) == expected.Dark);
		CHECK(Graphite::ColourOf(expected.Kind, kLightScheme) == expected.Light);
	}
}

TEST_CASE("Any other file takes the dim folder colour, in either scheme")
{
	constexpr bool kLightScheme = true;
	constexpr bool kDarkScheme = false;

	ImGuiFrames frames;

	CHECK(Graphite::ColourOf(FileKind::Other, kDarkScheme) == Graphite::DimFolderColour());
	CHECK(Graphite::ColourOf(FileKind::Other, kLightScheme) == Graphite::DimFolderColour());
}

TEST_CASE("The dim folder colour is the text colour mixed 45 % toward the disabled-text colour")
{
	ImGuiFrames frames;

	ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

	const ImVec4 dimFolder = ImGui::ColorConvertU32ToFloat4(Graphite::DimFolderColour());

	constexpr float kChannelTolerance = 1.0f / 255.0f;
	CHECK(dimFolder.x == doctest::Approx(0.55f).epsilon(kChannelTolerance));
	CHECK(dimFolder.y == doctest::Approx(0.55f).epsilon(kChannelTolerance));
	CHECK(dimFolder.z == doctest::Approx(0.55f).epsilon(kChannelTolerance));
	CHECK(dimFolder.w == doctest::Approx(1.0f));
}

TEST_CASE("The scheme is light when the window background is past half luminance")
{
	ImGuiFrames frames;

	ImGui::StyleColorsLight();
	CHECK(Graphite::IsLightScheme());

	ImGui::StyleColorsDark();
	CHECK_FALSE(Graphite::IsLightScheme());
}

TEST_CASE("A dimmed icon keeps its colour at half its alpha")
{
	constexpr bool kDarkScheme = false;
	constexpr bool kOpen = true;

	ImGuiFrames frames;

	const Graphite::RowIcon normal = Graphite::FileIconOf(FileKind::Json);
	const Graphite::RowIcon dimmed = Graphite::FileIconOf(FileKind::Json, Graphite::IconTone::Dimmed);

	CHECK(SameGlyph(normal.Glyph, ICON_MDI_CODE_JSON));
	CHECK(normal.Colour == Graphite::ColourOf(FileKind::Json, kDarkScheme));
	CHECK(SameGlyph(dimmed.Glyph, ICON_MDI_CODE_JSON));
	CHECK(dimmed.Colour == Core::WithAlphaScaled(normal.Colour, kHalf));

	const Graphite::RowIcon folder = Graphite::FolderIconOf(kOpen);
	const Graphite::RowIcon dimmedFolder = Graphite::FolderIconOf(kOpen, Graphite::IconTone::Dimmed);

	CHECK(SameGlyph(folder.Glyph, ICON_MDI_FOLDER_OPEN_OUTLINE));
	CHECK(folder.Colour == Graphite::DimFolderColour());
	CHECK(dimmedFolder.Colour == Core::WithAlphaScaled(folder.Colour, kHalf));
}

TEST_CASE("A file icon is drawn at the point it is given, in its kind's colour")
{
	constexpr bool kDarkScheme = false;

	const DrawnIcon drawn = DrawOneIcon([] { Graphite::DrawFileIcon(FileKind::Json, kIconPoint); },
										 [] { return Graphite::ColourOf(FileKind::Json, kDarkScheme); });

	REQUIRE(drawn.Extent.has_value());
	CHECK(drawn.Extent->Left >= kIconPoint.x - kOnePixel);
	CHECK(drawn.Extent->Right <= kIconPoint.x + drawn.FontSize + kOnePixel);
}

TEST_CASE("Under the light scheme a file icon takes its kind's light colour")
{
	constexpr bool kLightScheme = true;

	const DrawnIcon drawn = DrawOneIcon(
		[] {
			ImGui::StyleColorsLight();
			Graphite::DrawFileIcon(FileKind::Json, kIconPoint);
		},
		[] { return Graphite::ColourOf(FileKind::Json, kLightScheme); });

	CHECK(drawn.Extent.has_value());
}

TEST_CASE("A folder icon is drawn in the dim folder colour")
{
	constexpr bool kClosed = false;

	const DrawnIcon drawn =
		DrawOneIcon([] { Graphite::DrawFolderIcon(kClosed, kIconPoint); }, [] { return Graphite::DimFolderColour(); });

	CHECK(drawn.Extent.has_value());
}

TEST_CASE("A dimmed file icon is drawn at half its kind's alpha")
{
	constexpr bool kDarkScheme = false;

	const DrawnIcon drawn = DrawOneIcon(
		[] { Graphite::DrawFileIcon(FileKind::Json, kIconPoint, Graphite::IconTone::Dimmed); },
		[] { return Core::WithAlphaScaled(Graphite::ColourOf(FileKind::Json, kDarkScheme), kHalf); });

	CHECK(drawn.Extent.has_value());
}

TEST_CASE("A name follows its icon by the icon's width and a gap of 8 px")
{
	constexpr float kLargeFontSize = 30.0f;
	constexpr float kGapInPixels = 8.0f;

	ImGuiFrames frames;

	frames.Next([] {
		CHECK(Graphite::FileIconWidth() == doctest::Approx(ImGui::GetFontSize()));
		CHECK(Graphite::IconToNameOffset() == doctest::Approx(ImGui::GetFontSize() + Core::Scaled(kGapInPixels)));

		ImGui::PushFont(nullptr, kLargeFontSize);
		CHECK(Graphite::IconToNameOffset() == doctest::Approx(kLargeFontSize + Core::Scaled(kGapInPixels)));
		ImGui::PopFont();
	});
}
