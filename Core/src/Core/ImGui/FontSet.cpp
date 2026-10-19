#include "corepch.h"

#include "Core/ImGui/FontSet.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmbeddedFonts.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Platform/Process.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <optional>

namespace Core {

	namespace {
		constexpr char kAdvanceSampleCharacter = '0';
		// WARNING: ImGui rounds a face's ascent up and its descent down to whole pixels at the size it
		// bakes; read at 15 px, the ratio would set JetBrains Mono's icons a pixel low. At ImGui's
		// largest size the rounding is negligible.
		constexpr float kMetricsSampleSize = IMGUI_FONT_SIZE_MAX;
		constexpr const char* kSymbolFontFile = "seguisym.ttf";

		constexpr ImWchar kFirstControlCharacter = 0x0001;
		constexpr ImWchar kLastControlCharacter = 0x001F;
		constexpr ImWchar kDeleteCharacter = 0x007F;
		constexpr ImWchar kIdenticalTo = 0x2261;
		constexpr ImWchar kEndOfRanges = 0;

		// WARNING: ImGui's dynamic atlas reads GlyphRanges only to preload, and loads any other glyph of the face on
		// demand. Only excluded glyphs stay out, so the badge face excludes all but ASCII and the identical-to sign.
		constexpr ImWchar kBadgeExcludedRanges[] = {kFirstControlCharacter, kLastControlCharacter,
													kDeleteCharacter, kIdenticalTo - 1,
													kIdenticalTo + 1, IM_UNICODE_CODEPOINT_MAX,
													kEndOfRanges};

		// WARNING: without NoLoadError a missing system font trips ImGui's IM_ASSERT_USER_ERROR before
		// the call returns nullptr, which crashes a Debug build instead of letting the warning run.
		void AllowMissingFontFile(ImFontConfig& config)
		{
			config.Flags |= ImFontFlags_NoLoadError;
		}

		ImFont* LoadFace(ImFontAtlas& atlas, const EmbeddedFont& font, float size,
						 const ImWchar* excludedRanges = nullptr)
		{
			ImFontConfig config;
			config.SizePixels = size;
			config.GlyphExcludeRanges = excludedRanges;

			return atlas.AddFontFromMemoryCompressedTTF(font.CompressedData, font.CompressedSize, size, &config);
		}

		float DescentShareOf(ImFont& face)
		{
			const ImFontBaked* const baked = face.GetFontBaked(kMetricsSampleSize);

			return -baked->Descent / (baked->Ascent - baked->Descent);
		}

		// WARNING: Lucide's icons stand on the baseline in a square one font size tall, so beside text
		// they sit high by the face's descent. Lowered by it, the square spans the face's line from
		// ascender to descender and each icon centres on the capitals.
		void MergeIconFont(ImFontAtlas& atlas, ImFont& face, float size)
		{
			ImFontConfig config;
			config.MergeMode = true;
			config.DstFont = &face;
			// WARNING: PixelSnapH rounds each glyph's advance to a whole pixel and visibly
			// skews a small round icon, so the minimum advance is set instead.
			config.GlyphMinAdvanceX = size;
			config.GlyphOffset = ImVec2(0.0f, size * DescentShareOf(face));

			const EmbeddedFont font = Lucide();

			atlas.AddFontFromMemoryCompressedTTF(font.CompressedData, font.CompressedSize, size, &config,
												 UsedGlyphRanges());
		}

		// WARNING: the terminal draws a run of cells with one AddText call, so a proportional
		// symbol wider than a cell would push the rest of its row off its cells. The code face
		// passes its own cell advance to keep every fallback glyph one cell wide.
		void MergeSymbolFallback(ImFontAtlas& atlas, float size, std::optional<float> cellAdvance)
		{
			const Result<std::filesystem::path> fonts = SystemFontsDirectory();

			if (!fonts)
			{
				CORE_WARN("Cannot locate the system fonts, symbols will show as boxes: {}",
						  fonts.GetError().Message);
				return;
			}

			ImFontConfig config;
			config.MergeMode = true;

			if (cellAdvance.has_value())
			{
				config.GlyphMinAdvanceX = *cellAdvance;
				config.GlyphMaxAdvanceX = *cellAdvance;
			}

			AllowMissingFontFile(config);

			const std::filesystem::path path = fonts.GetValue() / kSymbolFontFile;

			if (atlas.AddFontFromFileTTF(ToUtf8(path).c_str(), size, &config) == nullptr)
			{
				CORE_WARN("Could not load the symbol font, symbols will show as boxes: {}", ToUtf8(path));
			}
		}

		float CellAdvanceOf(ImFont& face, float size)
		{
			return face.GetFontBaked(size)->GetCharAdvance(kAdvanceSampleCharacter);
		}
	}

	FontSet LoadFontSet(ImFontAtlas& atlas)
	{
		FontSet set;

		// WARNING: Inter has private-use glyphs of its own at most of Lucide's codepoints, and a face's
		// own glyph wins over a merged one, so Inter gives up the span of the used icons.
		set.Interface = LoadFace(atlas, InterRegular(), kInterfaceFontSize, UsedIconSpan());
		MergeIconFont(atlas, *set.Interface, kInterfaceFontSize);
		MergeSymbolFallback(atlas, kInterfaceFontSize, std::nullopt);

		// WARNING: JetBrains Mono keeps its Powerline glyphs, which terminal prompts draw, over Lucide's
		// at the same codepoints. Of the used icons only copy is among them, and the interface face
		// draws it.
		set.Code = LoadFace(atlas, JetBrainsMonoRegular(), kCodeFontSize);
		MergeIconFont(atlas, *set.Code, kCodeFontSize);
		MergeSymbolFallback(atlas, kCodeFontSize, CellAdvanceOf(*set.Code, kCodeFontSize));

		// WARNING: must come after every merge onto the code face, because ImGui's dynamic atlas
		// merges the symbol fallback onto whichever font was added last.
		set.Strong = LoadFace(atlas, InterSemiBold(), kInterfaceFontSize, UsedIconSpan());
		MergeIconFont(atlas, *set.Strong, kInterfaceFontSize);

		set.Badge = LoadFace(atlas, JetBrainsMonoBold(), kBadgeFontSize, kBadgeExcludedRanges);

		// A preview tab's name is all this face draws, so it takes no icons.
		set.Italic = LoadFace(atlas, InterItalic(), kInterfaceFontSize, UsedIconSpan());

		return set;
	}

	void PushInterfaceFont(float size)
	{
		ImGui::PushFont(ImGuiRenderer::GetInterfaceFont(), size);
	}

	void PushCodeFont(float size)
	{
		ImGui::PushFont(ImGuiRenderer::GetMonoFont(), size);
	}
}
