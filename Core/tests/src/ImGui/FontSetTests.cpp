#include <doctest/doctest.h>

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/Icons.h"
#include "Core/Platform/Process.h"
#include "Core/Text/Utf8.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

	// A context without a backend: building the atlas needs no window.
	struct HeadlessContext
	{
		HeadlessContext() { ImGui::CreateContext(); }
		~HeadlessContext() { ImGui::DestroyContext(); }
	};

	Core::FontSet LoadIntoContext()
	{
		return Core::LoadFontSet(*ImGui::GetIO().Fonts);
	}

	ImWchar CodepointOf(std::string_view utf8)
	{
		std::size_t offset = 0;
		const std::optional<char32_t> codepoint = Core::DecodeUtf8(utf8, offset);
		REQUIRE(codepoint.has_value());
		return static_cast<ImWchar>(codepoint.value_or(Core::kReplacementCharacter));
	}

	bool Draws(ImFont& font, float size, ImWchar codepoint)
	{
		return font.GetFontBaked(size)->FindGlyphNoFallback(codepoint) != nullptr;
	}

	float AdvanceOf(ImFont& font, float size, ImWchar codepoint)
	{
		return font.GetFontBaked(size)->GetCharAdvance(codepoint);
	}

	// WARNING: read from the atlas's pixels, not from the glyph's box: Lucide's glyphs carry a box
	// that reaches down to the baseline whatever they draw, so the box's middle is not the icon's.
	std::optional<float> InkMiddleOf(const ImFontGlyph& glyph)
	{
		ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
		ImTextureData* const texture = atlas->TexData;
		const ImTextureRect* const rect = ImFontAtlasPackGetRect(atlas, glyph.PackId);
		REQUIRE(texture != nullptr);
		REQUIRE(rect != nullptr);

		const int alphaByte = texture->BytesPerPixel - 1;
		std::optional<int> firstInkRow;
		std::optional<int> lastInkRow;

		for (int row = 0; row < rect->h; ++row)
		{
			const auto* const pixels = static_cast<const unsigned char*>(texture->GetPixelsAt(rect->x, rect->y + row));

			for (int column = 0; column < rect->w; ++column)
			{
				if (pixels[column * texture->BytesPerPixel + alphaByte] != 0)
				{
					firstInkRow = firstInkRow.value_or(row);
					lastInkRow = row;
					break;
				}
			}
		}

		if (!firstInkRow.has_value() || !lastInkRow.has_value())
		{
			return std::nullopt;
		}

		const float rowHeight = (glyph.Y1 - glyph.Y0) / static_cast<float>(rect->h);
		const float inkTop = glyph.Y0 + static_cast<float>(*firstInkRow) * rowHeight;
		const float inkBottom = glyph.Y0 + static_cast<float>(*lastInkRow + 1) * rowHeight;

		return (inkTop + inkBottom) * 0.5f;
	}

	struct FaceAtSize
	{
		std::string Name;
		ImFont* Face;
		float Size;
	};

	std::array<FaceAtSize, 3> FacesOf(const Core::FontSet& set)
	{
		return {{{"interface", set.Interface, Core::kInterfaceFontSize},
				 {"code", set.Code, Core::kCodeFontSize},
				 {"strong", set.Strong, Core::kInterfaceFontSize}}};
	}

	// WARNING: a face's own glyph wins over a merged font's at the same codepoint, so finding a glyph
	// does not say it is Lucide's; the source that baked it does.
	const ImFontConfig* SourceOf(ImFont& face, float size, ImWchar codepoint)
	{
		const ImFontGlyph* const glyph = face.GetFontBaked(size)->FindGlyphNoFallback(codepoint);

		return glyph != nullptr ? face.Sources[glyph->SourceIdx] : nullptr;
	}

	bool IsIconFont(const ImFontConfig* source)
	{
		return source != nullptr && source->GlyphRanges == Core::UsedGlyphRanges();
	}

	bool IsFacesOwnFont(ImFont& face, const ImFontConfig* source)
	{
		return source != nullptr && source == face.Sources[0];
	}

	constexpr ImWchar kFirstPowerlineSymbol = 0xE0A0;
	constexpr ImWchar kLastPowerlineSymbol = 0xE0A2;
	constexpr ImWchar kFirstPowerlineArrow = 0xE0B0;
	constexpr ImWchar kLastPowerlineArrow = 0xE0B3;

	bool IsPowerlineGlyph(ImWchar codepoint)
	{
		const bool isSymbol = codepoint >= kFirstPowerlineSymbol && codepoint <= kLastPowerlineSymbol;
		const bool isArrow = codepoint >= kFirstPowerlineArrow && codepoint <= kLastPowerlineArrow;

		return isSymbol || isArrow;
	}

	struct HeadlessFrame
	{
		HeadlessFrame()
			: Context(ImGui::CreateContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.IniFilename = nullptr;
			io.Fonts->Build();
			ImGui::NewFrame();
		}

		~HeadlessFrame()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(Context);
		}

		HeadlessFrame(const HeadlessFrame&) = delete;
		HeadlessFrame& operator=(const HeadlessFrame&) = delete;

		ImGuiContext* Context;
	};

	bool SymbolFontPresent()
	{
		const Core::Result<std::filesystem::path> fonts = Core::SystemFontsDirectory();
		std::error_code error;
		return fonts && std::filesystem::exists(fonts.GetValue() / "seguisym.ttf", error);
	}
}

TEST_CASE("The interface face is loaded first, at 14 px, and the code face at 15 px")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();

	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);
	REQUIRE(set.Strong != nullptr);

	CHECK(ImGui::GetIO().Fonts->Fonts[0] == set.Interface);
	CHECK(set.Interface->LegacySize == 14.0f);
	CHECK(set.Code->LegacySize == 15.0f);
	CHECK(set.Strong->LegacySize == 14.0f);

	CHECK(set.Interface != set.Code);
	CHECK(set.Interface != set.Strong);
	CHECK(set.Code != set.Strong);
}

TEST_CASE("The interface, the code and the strong face all draw the icons")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);
	REQUIRE(set.Strong != nullptr);

	const ImWchar close = CodepointOf(ICON_LC_X);

	CHECK(Draws(*set.Interface, Core::kInterfaceFontSize, close));
	CHECK(Draws(*set.Code, Core::kCodeFontSize, close));
	CHECK(Draws(*set.Strong, Core::kInterfaceFontSize, close));
}

TEST_CASE("Every used icon decodes to a glyph in the Lucide font, never the fallback, in the interface and the strong face")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();

	const FaceAtSize faces[] = {{"interface", set.Interface, Core::kInterfaceFontSize},
								{"strong", set.Strong, Core::kInterfaceFontSize}};

	for (const FaceAtSize& face : faces)
	{
		REQUIRE(face.Face != nullptr);
		CAPTURE(face.Name);

		for (const char* icon : Core::UsedIcons())
		{
			const ImWchar codepoint = CodepointOf(icon);
			CAPTURE(static_cast<unsigned int>(codepoint));

			CHECK(IsIconFont(SourceOf(*face.Face, face.Size, codepoint)));
		}
	}
}

TEST_CASE("In the code face every used icon decodes to a glyph in the Lucide font, but where JetBrains Mono has a Powerline glyph")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Code != nullptr);

	for (const char* icon : Core::UsedIcons())
	{
		const ImWchar codepoint = CodepointOf(icon);
		CAPTURE(static_cast<unsigned int>(codepoint));

		const ImFontConfig* const source = SourceOf(*set.Code, Core::kCodeFontSize, codepoint);

		if (IsPowerlineGlyph(codepoint))
		{
			CHECK(IsFacesOwnFont(*set.Code, source));
		}
		else
		{
			CHECK(IsIconFont(source));
		}
	}
}

TEST_CASE("The code face keeps JetBrains Mono's Powerline glyphs, which terminal prompts draw")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Code != nullptr);

	constexpr ImWchar kBranch = 0xE0A0;
	constexpr ImWchar kPadlock = 0xE0A2;
	constexpr ImWchar kRightArrow = 0xE0B0;

	for (const ImWchar codepoint : {kBranch, kPadlock, kRightArrow})
	{
		CAPTURE(static_cast<unsigned int>(codepoint));

		CHECK(IsFacesOwnFont(*set.Code, SourceOf(*set.Code, Core::kCodeFontSize, codepoint)));
	}
}

TEST_CASE("An icon is centred on the capitals beside it, within a pixel, in every face at every interface scale")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();

	constexpr std::array<float, 5> kWindowsMonitorScales = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
	constexpr float kTolerance = 1.0f;
	constexpr ImWchar kCapital = 'H';

	// WARNING: measured on x because it is drawn symmetrically about the middle of Lucide's grid;
	// most icons are not, so their ink's middle is not the middle of the icon.
	const ImWchar close = CodepointOf(ICON_LC_X);

	for (const FaceAtSize& face : FacesOf(set))
	{
		REQUIRE(face.Face != nullptr);
		CAPTURE(face.Name);

		for (const float scale : kWindowsMonitorScales)
		{
			CAPTURE(scale);

			ImFontBaked* const baked = face.Face->GetFontBaked(face.Size * scale);
			const ImFontGlyph* const icon = baked->FindGlyphNoFallback(close);
			const ImFontGlyph* const capital = baked->FindGlyphNoFallback(kCapital);
			REQUIRE(icon != nullptr);
			REQUIRE(capital != nullptr);

			const std::optional<float> iconMiddle = InkMiddleOf(*icon);
			const std::optional<float> capitalMiddle = InkMiddleOf(*capital);
			REQUIRE(iconMiddle.has_value());
			REQUIRE(capitalMiddle.has_value());
			CAPTURE(*iconMiddle);
			CAPTURE(*capitalMiddle);

			CHECK(std::abs(*iconMiddle - *capitalMiddle) <= kTolerance);
		}
	}
}

TEST_CASE("The code face is monospaced and the interface face is not")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);

	CHECK(AdvanceOf(*set.Code, Core::kCodeFontSize, 'i') == AdvanceOf(*set.Code, Core::kCodeFontSize, 'M'));
	CHECK(AdvanceOf(*set.Interface, Core::kInterfaceFontSize, 'i') <
		  AdvanceOf(*set.Interface, Core::kInterfaceFontSize, 'M'));
}

TEST_CASE("Chrome symbols resolve in the interface face")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);

	constexpr std::array<ImWchar, 4> kChromeSymbols = {0x00B7, 0x203A, 0x2026, 0x2192};
	const bool symbolFontPresent = SymbolFontPresent();

	for (const ImWchar symbol : kChromeSymbols)
	{
		CAPTURE(symbol);

		const bool drawn = Draws(*set.Interface, Core::kInterfaceFontSize, symbol);

		if (!drawn && !symbolFontPresent)
		{
			MESSAGE("seguisym.ttf is absent and Inter lacks U+", static_cast<unsigned int>(symbol), "; skipped");
			continue;
		}

		CHECK(drawn);
	}
}

TEST_CASE("With no fonts loaded, the push helpers keep the current face and set only the size")
{
	HeadlessFrame frame;

	ImFont* const face = ImGui::GetFont();

	Core::PushCodeFont();
	CHECK(ImGui::GetFont() == face);
	CHECK(ImGui::GetStyle().FontSizeBase == Core::kCodeFontSize);
	ImGui::PopFont();

	Core::PushInterfaceFont();
	CHECK(ImGui::GetFont() == face);
	CHECK(ImGui::GetStyle().FontSizeBase == Core::kInterfaceFontSize);
	ImGui::PopFont();
}

TEST_CASE("With no fonts loaded, a sized interface push keeps the current face and sets that size")
{
	HeadlessFrame frame;

	constexpr float kRequestedSize = 13.0f;

	const ImGuiStyle& style = ImGui::GetStyle();
	const float scale = style.FontScaleMain * style.FontScaleDpi;
	ImFont* const face = ImGui::GetFont();

	Core::PushInterfaceFont(kRequestedSize);
	CHECK(ImGui::GetFont() == face);
	CHECK(ImGui::GetFontSize() == kRequestedSize * scale);
	ImGui::PopFont();
}

TEST_CASE("A code push at the interface size gives the interface size")
{
	HeadlessFrame frame;

	const ImGuiStyle& style = ImGui::GetStyle();
	const float scale = style.FontScaleMain * style.FontScaleDpi;

	Core::PushCodeFont(Core::kInterfaceFontSize);
	CHECK(ImGui::GetFontSize() == Core::kInterfaceFontSize * scale);
	ImGui::PopFont();
}

TEST_CASE("The interface font inside a code push is still the interface size")
{
	HeadlessFrame frame;

	const ImGuiStyle& style = ImGui::GetStyle();
	const float scale = style.FontScaleMain * style.FontScaleDpi;
	const int stackDepth = ImGui::GetCurrentContext()->FontStack.Size;
	const float sizeBefore = ImGui::GetFontSize();

	Core::PushCodeFont();
	Core::PushInterfaceFont();
	CHECK(ImGui::GetFontSize() == Core::kInterfaceFontSize * scale);
	ImGui::PopFont();
	ImGui::PopFont();

	CHECK(ImGui::GetCurrentContext()->FontStack.Size == stackDepth);
	CHECK(ImGui::GetFontSize() == sizeBefore);
}
