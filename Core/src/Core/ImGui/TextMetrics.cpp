#include "corepch.h"

#include "Core/ImGui/TextMetrics.h"

#include "Core/Base/Assert.h"
#include "Core/Text/Utf8.h"

#include <imgui.h>

#include <cstddef>
#include <optional>

namespace Core {

	namespace {

		struct AtlasBox
		{
			int Left = 0;
			int Top = 0;
			int Right = 0;
			int Bottom = 0;
		};

		constexpr float kRoundToNearest = 0.5f;

		int ToAtlasPixel(float textureCoordinate, int textureSize)
		{
			return static_cast<int>(textureCoordinate * static_cast<float>(textureSize) + kRoundToNearest);
		}

		AtlasBox AtlasBoxOf(const ImFontGlyph& glyph, const ImTextureData& atlas)
		{
			return AtlasBox{.Left = ToAtlasPixel(glyph.U0, atlas.Width),
							.Top = ToAtlasPixel(glyph.V0, atlas.Height),
							.Right = ToAtlasPixel(glyph.U1, atlas.Width),
							.Bottom = ToAtlasPixel(glyph.V1, atlas.Height)};
		}

		int AlphaByteOffset(ImTextureFormat format)
		{
			switch (format)
			{
			case ImTextureFormat_RGBA32:
				return 3;
			case ImTextureFormat_Alpha8:
				return 0;
			}

			CORE_ASSERT(false, "The font atlas has a pixel format with no known alpha byte");
			return 0;
		}

		bool ColumnHasInk(const ImTextureData& atlas, const AtlasBox& box, int column)
		{
			const int alphaByte = AlphaByteOffset(atlas.Format);

			for (int row = box.Top; row < box.Bottom; ++row)
			{
				const std::size_t pixelIndex = static_cast<std::size_t>(row) * static_cast<std::size_t>(atlas.Width) +
											   static_cast<std::size_t>(column);
				const unsigned char* const pixel =
					atlas.Pixels + pixelIndex * static_cast<std::size_t>(atlas.BytesPerPixel);

				if (pixel[alphaByte] != 0)
				{
					return true;
				}
			}

			return false;
		}
	}

	float MonospaceCellWidth()
	{
		// WARNING: CalcTextSize rounds the advance up to a whole pixel, so cells measured with it drift away from the glyphs ImDrawList::AddText draws.
		ImFontBaked* const baked = ImGui::GetFontBaked();

		const char referenceGlyph = '0';
		const float bakedToCurrentSize = ImGui::GetFontSize() / baked->Size;

		return baked->GetCharAdvance(referenceGlyph) * bakedToCurrentSize;
	}

	// WARNING: an empty view may carry a null pointer, which CalcTextSize would read as "up to the terminator".
	float TextWidth(std::string_view text)
	{
		if (text.empty())
		{
			return 0.0f;
		}

		return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
	}

	// WARNING: the icon webfont gives every glyph an empty point at the origin, so a glyph's box says nothing about where its ink begins.
	GlyphInk MeasureGlyphInk(std::string_view character)
	{
		std::size_t decodeOffset = 0;
		const std::optional<char32_t> codePoint = DecodeUtf8(character, decodeOffset);
		CORE_ASSERT(codePoint.has_value(), "MeasureGlyphInk needs one UTF-8 encoded character");

		ImFontBaked* const baked = ImGui::GetFontBaked();
		const ImFontGlyph* const glyph =
			baked->FindGlyph(static_cast<ImWchar>(codePoint.value_or(kReplacementCharacter)));

		const ImTextureData& atlas = *ImGui::GetIO().Fonts->TexData;
		CORE_ASSERT(atlas.Pixels != nullptr, "The font atlas no longer holds its pixels");

		const AtlasBox box = AtlasBoxOf(*glyph, atlas);

		if (!glyph->Visible || box.Right <= box.Left)
		{
			return GlyphInk{};
		}

		int firstInkColumn = box.Left;

		while (firstInkColumn < box.Right && !ColumnHasInk(atlas, box, firstInkColumn))
		{
			++firstInkColumn;
		}

		if (firstInkColumn == box.Right)
		{
			return GlyphInk{};
		}

		int endInkColumn = box.Right;

		while (!ColumnHasInk(atlas, box, endInkColumn - 1))
		{
			--endInkColumn;
		}

		const float screenPixelsPerAtlasPixel =
			(glyph->X1 - glyph->X0) / static_cast<float>(box.Right - box.Left);

		const float inkStartInBake =
			glyph->X0 + static_cast<float>(firstInkColumn - box.Left) * screenPixelsPerAtlasPixel;
		const float inkEndInBake =
			glyph->X0 + static_cast<float>(endInkColumn - box.Left) * screenPixelsPerAtlasPixel;

		const float bakedToCurrentSize = ImGui::GetFontSize() / baked->Size;

		return GlyphInk{.StartFromDrawX = inkStartInBake * bakedToCurrentSize,
						.EndFromDrawX = inkEndInBake * bakedToCurrentSize};
	}
}
