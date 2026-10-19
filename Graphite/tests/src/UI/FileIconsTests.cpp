#include <doctest/doctest.h>

#include "UI/FileIcons.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/Utf8.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace {

	using Graphite::FileKind;

	constexpr float kHalf = 0.5f;
	constexpr float kOnePixel = 1.0f;
	constexpr ImVec2 kWindowSize{300.0f, 200.0f};
	constexpr ImVec2 kIconPoint{40.0f, 50.0f};
	constexpr float kBadgeWidth = 16.0f;
	constexpr float kBadgeHeight = 14.0f;
	constexpr float kFolderSize = 15.0f;
	constexpr int kVerticesPerCharacter = 4;
	constexpr std::string_view kIdenticalTo = "\xE2\x89\xA1";

	struct ExtensionCase
	{
		const char* Path;
		FileKind Kind;
	};

	constexpr ExtensionCase kExtensionCases[] = {
		{"Main.cpp", FileKind::CppSource},
		{"Main.cc", FileKind::CppSource},
		{"Main.cxx", FileKind::CppSource},
		{"main.c", FileKind::CSource},
		{"Main.h", FileKind::Header},
		{"Main.hpp", FileKind::Header},
		{"Main.hxx", FileKind::Header},
		{"Main.inl", FileKind::Header},
		{"Main.ipp", FileKind::Header},
		{"Program.cs", FileKind::CSharp},
		{"graphite.json", FileKind::Json},
		{"README.md", FileKind::Markdown},
		{"premake5.lua", FileKind::Lua},
		{"New-Adr.ps1", FileKind::PowerShell},
		{"Module.psm1", FileKind::PowerShell},
		{"Module.psd1", FileKind::PowerShell},
		{"GenerateProjects.bat", FileKind::Batch},
		{"build.cmd", FileKind::Batch},
		{"Graphite.sln", FileKind::Solution},
		{"Graphite.slnx", FileKind::Solution},
		{"Core.vcxproj", FileKind::CppProject},
		{"Tools.csproj", FileKind::CSharpProject},
		{"logo.png", FileKind::Image},
		{"photo.jpg", FileKind::Image},
		{"photo.jpeg", FileKind::Image},
		{"spinner.gif", FileKind::Image},
		{"splash.bmp", FileKind::Image},
		{"Graphite.ico", FileKind::Image},
		{"banner.webp", FileKind::Image},
		{"logo.svg", FileKind::VectorImage},
		{"tool.py", FileKind::Python},
		{"notes.txt", FileKind::PlainText},
		{"imgui.ini", FileKind::Ini},
		{"Graphite.rc", FileKind::ResourceScript},
	};

	struct BadgeCase
	{
		FileKind Kind;
		std::string_view Badge;
	};

	constexpr BadgeCase kBadgeCases[] = {
		{FileKind::CppSource, "C++"},
		{FileKind::CSource, "C"},
		{FileKind::Header, "H"},
		{FileKind::CSharp, "C#"},
		{FileKind::Json, "{}"},
		{FileKind::Markdown, "MD"},
		{FileKind::Lua, "LUA"},
		{FileKind::PowerShell, "PS"},
		{FileKind::Batch, "BAT"},
		{FileKind::Solution, "SLN"},
		{FileKind::CppProject, "VCX"},
		{FileKind::CSharpProject, "CSP"},
		{FileKind::Image, "IMG"},
		{FileKind::VectorImage, "SVG"},
		{FileKind::Python, "PY"},
		{FileKind::PlainText, "TXT"},
		{FileKind::Ini, "INI"},
		{FileKind::ResourceScript, "RC"},
		{FileKind::Other, kIdenticalTo},
	};

	struct GlyphCase
	{
		FileKind Kind;
		const char* Glyph;
	};

	constexpr GlyphCase kGlyphCases[] = {
		{FileKind::CppSource, ICON_LC_FILE_CODE},
		{FileKind::CSource, ICON_LC_FILE_CODE},
		{FileKind::Header, ICON_LC_FILE_CODE_2},
		{FileKind::CSharp, ICON_LC_HASH},
		{FileKind::Json, ICON_LC_FILE_JSON},
		{FileKind::Markdown, ICON_LC_FILE_TEXT},
		{FileKind::Lua, ICON_LC_FILE_CODE},
		{FileKind::PowerShell, ICON_LC_TERMINAL},
		{FileKind::Batch, ICON_LC_TERMINAL},
		{FileKind::Solution, ICON_LC_APP_WINDOW},
		{FileKind::CppProject, ICON_LC_APP_WINDOW},
		{FileKind::CSharpProject, ICON_LC_APP_WINDOW},
		{FileKind::Image, ICON_LC_FILE_IMAGE},
		{FileKind::VectorImage, ICON_LC_FILE_IMAGE},
		{FileKind::Python, ICON_LC_FILE},
		{FileKind::PlainText, ICON_LC_FILE},
		{FileKind::Ini, ICON_LC_FILE},
		{FileKind::ResourceScript, ICON_LC_FILE},
		{FileKind::Other, ICON_LC_FILE},
	};

	struct ColourCase
	{
		FileKind Kind;
		ImU32 Dark;
		ImU32 Light;
	};

	constexpr ImU32 kCppDark = IM_COL32(0x6A, 0xA7, 0xE6, 0xFF);
	constexpr ImU32 kCppLight = IM_COL32(0x1E, 0x64, 0xB4, 0xFF);
	constexpr ImU32 kShellDark = IM_COL32(0x5F, 0xA9, 0xD6, 0xFF);
	constexpr ImU32 kShellLight = IM_COL32(0x1F, 0x6F, 0x9E, 0xFF);
	constexpr ImU32 kProjectDark = IM_COL32(0xA7, 0x7F, 0xE0, 0xFF);
	constexpr ImU32 kProjectLight = IM_COL32(0x6B, 0x3F, 0xB0, 0xFF);
	constexpr ImU32 kImageDark = IM_COL32(0xD0, 0xA0, 0x60, 0xFF);
	constexpr ImU32 kImageLight = IM_COL32(0x9A, 0x6A, 0x20, 0xFF);

	constexpr ColourCase kColourCases[] = {
		{FileKind::CppSource, kCppDark, kCppLight},
		{FileKind::CSource, kCppDark, kCppLight},
		{FileKind::Header, IM_COL32(0xB3, 0x92, 0xE8, 0xFF), IM_COL32(0x7A, 0x46, 0xC8, 0xFF)},
		{FileKind::CSharp, IM_COL32(0x7F, 0xBF, 0x6A, 0xFF), IM_COL32(0x2E, 0x7D, 0x32, 0xFF)},
		{FileKind::Json, IM_COL32(0xD8, 0xB2, 0x5A, 0xFF), IM_COL32(0x9A, 0x74, 0x00, 0xFF)},
		{FileKind::Markdown, IM_COL32(0x8F, 0xA3, 0xB8, 0xFF), IM_COL32(0x55, 0x69, 0x7E, 0xFF)},
		{FileKind::Lua, IM_COL32(0x6C, 0x8C, 0xE0, 0xFF), IM_COL32(0x2F, 0x4F, 0xB0, 0xFF)},
		{FileKind::PowerShell, kShellDark, kShellLight},
		{FileKind::Batch, kShellDark, kShellLight},
		{FileKind::Solution, kProjectDark, kProjectLight},
		{FileKind::CppProject, kProjectDark, kProjectLight},
		{FileKind::CSharpProject, kProjectDark, kProjectLight},
		{FileKind::Image, kImageDark, kImageLight},
		{FileKind::VectorImage, kImageDark, kImageLight},
	};

	constexpr FileKind kDimKinds[] = {FileKind::Python, FileKind::PlainText, FileKind::Ini, FileKind::ResourceScript,
									  FileKind::Other};

	bool SameGlyph(const char* left, const char* right)
	{
		return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
	}

	struct DrawnIcon
	{
		ImU32 Expected = 0;
		float FontSize = 0.0f;
		float Scale = 0.0f;
		std::optional<HorizontalExtent> Extent;
		std::optional<VerticalExtent> Height;
		int Vertices = 0;
	};

	enum class BadgeFace
	{
		TheTestFace,
		TheRealOne
	};

	// WARNING: Fonts added to a built atlas leave it unbuilt, and with no renderer backend a frame asserts on an
	// unbuilt atlas, so the atlas is built again once the font set is in it.
	ImFont* LoadTheRealBadgeFace()
	{
		ImFont* const face = Core::LoadFontSet(*ImGui::GetIO().Fonts).Badge;
		ImGui::GetIO().Fonts->Build();

		return face;
	}

	// WARNING: The expected colour is asked for inside the frame, after the drawing, because it is derived from the
	// style that the drawing may have changed.
	DrawnIcon DrawOneIcon(const std::function<void()>& drawIcon, const std::function<ImU32()>& expectedColour,
						  BadgeFace badgeFace = BadgeFace::TheTestFace)
	{
		constexpr float kKeepTheSize = 0.0f;

		ImGuiFrames frames;
		DrawnIcon drawn;

		// WARNING: Without a font set a badge draws in the current face, and the tests' own, ProggyClean, is wider
		// for its size than JetBrains Mono Bold, so a badge only fits its box when the real face is made current.
		ImFont* const face = badgeFace == BadgeFace::TheRealOne ? LoadTheRealBadgeFace() : nullptr;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("##iconWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

			if (face != nullptr)
			{
				ImGui::PushFont(face, kKeepTheSize);
			}

			drawIcon();

			if (face != nullptr)
			{
				ImGui::PopFont();
			}

			drawn.Expected = expectedColour();
			drawn.FontSize = ImGui::GetFontSize();
			drawn.Scale = Core::UiScale();
			const ImDrawList& drawing = *ImGui::GetWindowDrawList();
			drawn.Extent = HorizontalExtentInColour(drawing, drawn.Expected);
			drawn.Height = VerticalExtentInColour(drawing, drawn.Expected);
			drawn.Vertices = VerticesInColour(drawing, drawn.Expected);
			ImGui::End();
		});

		return drawn;
	}

	// WARNING: A failed REQUIRE returns at once, so the context goes with the destructor; one left current breaks the
	// tests after it.
	struct FontContext
	{
		FontContext() { ImGui::SetCurrentContext(Context); }
		~FontContext()
		{
			ImGui::DestroyContext(Context);
			ImGui::SetCurrentContext(Previous);
		}

		FontContext(const FontContext&) = delete;
		FontContext& operator=(const FontContext&) = delete;

		ImGuiContext* Previous = ImGui::GetCurrentContext();
		ImGuiContext* Context = ImGui::CreateContext();
	};

	int CharactersIn(std::string_view text)
	{
		int characters = 0;
		std::size_t offset = 0;

		while (offset < text.size())
		{
			REQUIRE(Core::DecodeUtf8(text, offset).has_value());
			++characters;
		}

		return characters;
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
	CHECK(Graphite::KindOfFile("Core.vcxproj.filters") == FileKind::Other);
	CHECK(Graphite::KindOfFile("archive.zip") == FileKind::Other);
	CHECK(Graphite::KindOfFile("LICENSE") == FileKind::Other);
	CHECK(Graphite::KindOfFile("") == FileKind::Other);
}

TEST_CASE("Each kind's badge is the mockup's short text, and anything unknown is the identical-to sign")
{
	for (const BadgeCase& expected : kBadgeCases)
	{
		CAPTURE(static_cast<int>(expected.Kind));
		REQUIRE(Graphite::BadgeOf(expected.Kind) != nullptr);
		CHECK(std::string_view{Graphite::BadgeOf(expected.Kind)} == expected.Badge);
	}
}

TEST_CASE("GlyphOf gives a tab title a Lucide glyph for each kind, and a header another glyph than a source")
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

	CHECK(SameGlyph(Graphite::FolderGlyphOf(kOpen), ICON_LC_FOLDER_OPEN));
	CHECK(SameGlyph(Graphite::FolderGlyphOf(kClosed), ICON_LC_FOLDER));
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

TEST_CASE("A kind the mockup draws dim takes the disabled-text colour, in either scheme")
{
	constexpr bool kLightScheme = true;
	constexpr bool kDarkScheme = false;

	ImGuiFrames frames;
	const ImU32 disabledText = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

	for (const FileKind kind : kDimKinds)
	{
		CAPTURE(static_cast<int>(kind));
		CHECK(Graphite::ColourOf(kind, kDarkScheme) == disabledText);
		CHECK(Graphite::ColourOf(kind, kLightScheme) == disabledText);
	}
}

TEST_CASE("The folder grey is the text colour mixed 77 % toward the disabled-text colour")
{
	ImGuiFrames frames;

	ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

	const ImVec4 folderGrey = ImGui::ColorConvertU32ToFloat4(Graphite::DimFolderColour());

	constexpr float kChannelTolerance = 1.0f / 255.0f;
	CHECK(folderGrey.x == doctest::Approx(0.23f).epsilon(kChannelTolerance));
	CHECK(folderGrey.y == doctest::Approx(0.23f).epsilon(kChannelTolerance));
	CHECK(folderGrey.z == doctest::Approx(0.23f).epsilon(kChannelTolerance));
	CHECK(folderGrey.w == doctest::Approx(1.0f));
}

TEST_CASE("Under the dark scheme's text colours the folder grey is the mockup's 8f96a1, within a step")
{
	constexpr int kOneStep = 1;

	ImGuiFrames frames;

	ImGui::GetStyle().Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(IM_COL32(214, 219, 226, 255));
	ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(IM_COL32(123, 130, 141, 255));

	const ImU32 folderGrey = Graphite::DimFolderColour();

	CHECK(std::abs(static_cast<int>((folderGrey >> IM_COL32_R_SHIFT) & 0xFF) - 0x8F) <= kOneStep);
	CHECK(std::abs(static_cast<int>((folderGrey >> IM_COL32_G_SHIFT) & 0xFF) - 0x96) <= kOneStep);
	CHECK(std::abs(static_cast<int>((folderGrey >> IM_COL32_B_SHIFT) & 0xFF) - 0xA1) <= kOneStep);
}

TEST_CASE("The scheme is light when the window background is past half luminance")
{
	ImGuiFrames frames;

	ImGui::StyleColorsLight();
	CHECK(Graphite::IsLightScheme());

	ImGui::StyleColorsDark();
	CHECK_FALSE(Graphite::IsLightScheme());
}

TEST_CASE("A file's icon is its badge and a folder's its glyph, and a dimmed one keeps its colour at half its alpha")
{
	constexpr bool kDarkScheme = false;
	constexpr bool kOpen = true;

	ImGuiFrames frames;

	const Graphite::RowIcon normal = Graphite::FileIconOf(FileKind::Json);
	const Graphite::RowIcon dimmed = Graphite::FileIconOf(FileKind::Json, Graphite::IconTone::Dimmed);

	CHECK(normal.Form == Graphite::IconForm::Badge);
	CHECK(SameGlyph(normal.Text, "{}"));
	CHECK(normal.Colour == Graphite::ColourOf(FileKind::Json, kDarkScheme));
	CHECK(dimmed.Form == Graphite::IconForm::Badge);
	CHECK(SameGlyph(dimmed.Text, "{}"));
	CHECK(dimmed.Colour == Core::WithAlphaScaled(normal.Colour, kHalf));

	const Graphite::RowIcon folder = Graphite::FolderIconOf(kOpen);
	const Graphite::RowIcon dimmedFolder = Graphite::FolderIconOf(kOpen, Graphite::IconTone::Dimmed);

	CHECK(folder.Form == Graphite::IconForm::Glyph);
	CHECK(SameGlyph(folder.Text, ICON_LC_FOLDER_OPEN));
	CHECK(folder.Colour == Graphite::DimFolderColour());
	CHECK(dimmedFolder.Colour == Core::WithAlphaScaled(folder.Colour, kHalf));
}

TEST_CASE("A C++ file draws the text C++ in the C++ colour inside a 16 by 14 box")
{
	constexpr bool kDarkScheme = false;
	constexpr int kCharactersOfCpp = 3;

	const DrawnIcon drawn = DrawOneIcon([] { Graphite::DrawFileIcon(FileKind::CppSource, kIconPoint); },
										[] { return Graphite::ColourOf(FileKind::CppSource, kDarkScheme); },
										BadgeFace::TheRealOne);

	REQUIRE(drawn.Extent.has_value());
	REQUIRE(drawn.Height.has_value());

	const float boxLeft = kIconPoint.x;
	const float boxRight = kIconPoint.x + kBadgeWidth * drawn.Scale;
	const float boxTop = kIconPoint.y + (drawn.FontSize - kBadgeHeight * drawn.Scale) * kHalf;
	const float boxBottom = boxTop + kBadgeHeight * drawn.Scale;

	CHECK(drawn.Vertices == kCharactersOfCpp * kVerticesPerCharacter);
	CHECK(drawn.Extent->Left >= boxLeft);
	CHECK(drawn.Extent->Right <= boxRight);
	CHECK(drawn.Height->Top >= boxTop);
	CHECK(drawn.Height->Bottom <= boxBottom);

	const float drawnMiddle = (drawn.Extent->Left + drawn.Extent->Right) * kHalf;
	CHECK(std::abs(drawnMiddle - (boxLeft + boxRight) * kHalf) <= kOnePixel);
}

TEST_CASE("A file of an unknown kind draws the identical-to sign in the disabled-text colour")
{
	const DrawnIcon drawn = DrawOneIcon([] { Graphite::DrawFileIcon(FileKind::Other, kIconPoint); },
										[] { return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); });

	CHECK(drawn.Vertices == CharactersIn(kIdenticalTo) * kVerticesPerCharacter);
}

TEST_CASE("Under the light scheme a file badge takes its kind's light colour")
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

TEST_CASE("A folder is drawn in the folder grey, as its glyph at 15 px centred on its line")
{
	constexpr bool kClosed = false;
	constexpr float kReferenceShift = 100.0f;
	constexpr ImU32 kReferenceInk = IM_COL32(1, 2, 3, 255);

	ImGuiFrames frames;
	std::optional<HorizontalExtent> folder;
	std::optional<VerticalExtent> folderHeight;
	std::optional<HorizontalExtent> reference;
	std::optional<VerticalExtent> referenceHeight;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kWindowSize);
		ImGui::Begin("##folderWindow", nullptr, ImGuiWindowFlags_NoSavedSettings);

		Graphite::DrawFolderIcon(kClosed, kIconPoint);

		const float size = Core::Scaled(kFolderSize);
		const ImVec2 glyphTop(kIconPoint.x + kReferenceShift, kIconPoint.y + (ImGui::GetFontSize() - size) * kHalf);
		ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, glyphTop, kReferenceInk,
											Graphite::FolderGlyphOf(kClosed));

		const ImDrawList& drawing = *ImGui::GetWindowDrawList();
		folder = HorizontalExtentInColour(drawing, Graphite::DimFolderColour());
		folderHeight = VerticalExtentInColour(drawing, Graphite::DimFolderColour());
		reference = HorizontalExtentInColour(drawing, kReferenceInk);
		referenceHeight = VerticalExtentInColour(drawing, kReferenceInk);
		ImGui::End();
	});

	REQUIRE(folder.has_value());
	REQUIRE(folderHeight.has_value());
	REQUIRE(reference.has_value());
	REQUIRE(referenceHeight.has_value());

	CHECK(folder->Left == doctest::Approx(reference->Left - kReferenceShift));
	CHECK(folder->Right == doctest::Approx(reference->Right - kReferenceShift));
	CHECK(folderHeight->Top == doctest::Approx(referenceHeight->Top));
	CHECK(folderHeight->Bottom == doctest::Approx(referenceHeight->Bottom));
}

TEST_CASE("An ignored file's badge is drawn at half its kind's alpha")
{
	constexpr bool kDarkScheme = false;

	const DrawnIcon drawn = DrawOneIcon(
		[] { Graphite::DrawFileIcon(FileKind::Json, kIconPoint, Graphite::IconTone::Dimmed); },
		[] { return Core::WithAlphaScaled(Graphite::ColourOf(FileKind::Json, kDarkScheme), kHalf); });

	CHECK(drawn.Extent.has_value());
}

TEST_CASE("A name follows its icon by the badge's width and a gap of 5 px, whatever the font size")
{
	constexpr float kLargeFontSize = 30.0f;
	constexpr float kGapInPixels = 5.0f;

	ImGuiFrames frames;

	frames.Next([] {
		CHECK(Graphite::FileIconWidth() == doctest::Approx(Core::Scaled(kBadgeWidth)));
		CHECK(Graphite::IconToNameOffset() == doctest::Approx(Core::Scaled(kBadgeWidth) + Core::Scaled(kGapInPixels)));

		ImGui::PushFont(nullptr, kLargeFontSize);
		CHECK(Graphite::IconToNameOffset() == doctest::Approx(Core::Scaled(kBadgeWidth) + Core::Scaled(kGapInPixels)));
		ImGui::PopFont();
	});
}

TEST_CASE("Every badge draws from the badge face, never its fallback")
{
	FontContext context;
	const Core::FontSet set = Core::LoadFontSet(*ImGui::GetIO().Fonts);
	REQUIRE(set.Badge != nullptr);

	ImFontBaked* const baked = set.Badge->GetFontBaked(Core::kBadgeFontSize);

	for (const BadgeCase& badge : kBadgeCases)
	{
		CAPTURE(badge.Badge);
		std::size_t offset = 0;

		while (offset < badge.Badge.size())
		{
			const std::optional<char32_t> character = Core::DecodeUtf8(badge.Badge, offset);
			REQUIRE(character.has_value());
			CHECK(baked->FindGlyphNoFallback(static_cast<ImWchar>(*character)) != nullptr);
		}
	}
}
