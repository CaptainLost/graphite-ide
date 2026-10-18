#include "corepch.h"

#include "Core/ImGui/FontSet.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmbeddedFonts.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Platform/Process.h"

#include <imgui.h>

#include <filesystem>
#include <optional>

namespace Core {

	namespace {
		constexpr float kIconBaselineLift = 1.5f;
		constexpr char kAdvanceSampleCharacter = '0';
		constexpr const char* kSymbolFontFile = "seguisym.ttf";

		// WARNING: without NoLoadError a missing system font trips ImGui's IM_ASSERT_USER_ERROR before
		// the call returns nullptr, which crashes a Debug build instead of letting the warning run.
		void AllowMissingFontFile(ImFontConfig& config)
		{
			config.Flags |= ImFontFlags_NoLoadError;
		}

		// The data is static, so the atlas must not take ownership of it and free it.
		void KeepFontDataOutsideAtlas(ImFontConfig& config)
		{
			config.FontDataOwnedByAtlas = false;
		}

		ImFont* LoadFace(ImFontAtlas& atlas, const EmbeddedFont& font, float size)
		{
			ImFontConfig config;
			config.SizePixels = size;
			KeepFontDataOutsideAtlas(config);

			return atlas.AddFontFromMemoryCompressedTTF(font.CompressedData, font.CompressedSize, size, &config);
		}

		// WARNING: must run immediately after its face is added, because ImGui's dynamic
		// atlas merges onto whichever font was added last.
		void MergeIconFont(ImFontAtlas& atlas, float size)
		{
			ImFontConfig config;
			config.MergeMode = true;
			// WARNING: PixelSnapH rounds each glyph's advance to a whole pixel and visibly
			// skews a small round icon, so the minimum advance is set instead.
			config.GlyphMinAdvanceX = size;
			config.GlyphOffset = ImVec2(0.0f, kIconBaselineLift);
			KeepFontDataOutsideAtlas(config);

			const EmbeddedFont font = MaterialDesignIcons();

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

		set.Interface = LoadFace(atlas, InterRegular(), kInterfaceFontSize);
		MergeIconFont(atlas, kInterfaceFontSize);
		MergeSymbolFallback(atlas, kInterfaceFontSize, std::nullopt);

		set.Code = LoadFace(atlas, JetBrainsMonoRegular(), kCodeFontSize);
		MergeIconFont(atlas, kCodeFontSize);
		MergeSymbolFallback(atlas, kCodeFontSize, CellAdvanceOf(*set.Code, kCodeFontSize));

		// WARNING: must come after every merge onto the code face, because ImGui's dynamic atlas
		// merges onto whichever font was added last.
		set.Strong = LoadFace(atlas, InterSemiBold(), kInterfaceFontSize);

		return set;
	}

	void PushInterfaceFont()
	{
		ImGui::PushFont(ImGuiRenderer::GetInterfaceFont(), ImGui::GetStyle().FontSizeBase);
	}

	void PushCodeFont()
	{
		ImGui::PushFont(ImGuiRenderer::GetMonoFont(), kCodeFontSize);
	}
}
