#include "UI/FileIcons.h"

#include "Core/Base/Color.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kDimFolderShareOfDisabled = 0.77f;
		constexpr float kDimmedIconAlpha = 0.5f;
		constexpr float kHalf = 0.5f;
		constexpr float kBadgeLetterSpacing = -0.3f;

		constexpr const char* kIdenticalTo = "\xE2\x89\xA1";

		struct ExtensionKind
		{
			std::string_view Extension;
			FileKind Kind;
		};

		constexpr ExtensionKind kExtensionKinds[] = {
			{".cpp", FileKind::CppSource},
			{".cc", FileKind::CppSource},
			{".cxx", FileKind::CppSource},
			{".c", FileKind::CSource},
			{".h", FileKind::Header},
			{".hpp", FileKind::Header},
			{".hxx", FileKind::Header},
			{".inl", FileKind::Header},
			{".ipp", FileKind::Header},
			{".cs", FileKind::CSharp},
			{".json", FileKind::Json},
			{".md", FileKind::Markdown},
			{".lua", FileKind::Lua},
			{".ps1", FileKind::PowerShell},
			{".psm1", FileKind::PowerShell},
			{".psd1", FileKind::PowerShell},
			{".bat", FileKind::Batch},
			{".cmd", FileKind::Batch},
			{".sln", FileKind::Solution},
			{".slnx", FileKind::Solution},
			{".vcxproj", FileKind::CppProject},
			{".csproj", FileKind::CSharpProject},
			{".png", FileKind::Image},
			{".jpg", FileKind::Image},
			{".jpeg", FileKind::Image},
			{".gif", FileKind::Image},
			{".bmp", FileKind::Image},
			{".ico", FileKind::Image},
			{".webp", FileKind::Image},
			{".svg", FileKind::VectorImage},
			{".py", FileKind::Python},
			{".txt", FileKind::PlainText},
			{".ini", FileKind::Ini},
			{".rc", FileKind::ResourceScript},
		};

		struct KindColours
		{
			ImU32 Dark = 0;
			ImU32 Light = 0;
		};

		constexpr ImU32 Opaque(ImU32 red, ImU32 green, ImU32 blue)
		{
			constexpr ImU32 kOpaqueAlpha = 0xFF;

			return IM_COL32(red, green, blue, kOpaqueAlpha);
		}

		constexpr KindColours kCppSourceColours{.Dark = Opaque(0x6A, 0xA7, 0xE6), .Light = Opaque(0x1E, 0x64, 0xB4)};
		constexpr KindColours kHeaderColours{.Dark = Opaque(0xB3, 0x92, 0xE8), .Light = Opaque(0x7A, 0x46, 0xC8)};
		constexpr KindColours kCSharpColours{.Dark = Opaque(0x7F, 0xBF, 0x6A), .Light = Opaque(0x2E, 0x7D, 0x32)};
		constexpr KindColours kJsonColours{.Dark = Opaque(0xD8, 0xB2, 0x5A), .Light = Opaque(0x9A, 0x74, 0x00)};
		constexpr KindColours kMarkdownColours{.Dark = Opaque(0x8F, 0xA3, 0xB8), .Light = Opaque(0x55, 0x69, 0x7E)};
		constexpr KindColours kLuaColours{.Dark = Opaque(0x6C, 0x8C, 0xE0), .Light = Opaque(0x2F, 0x4F, 0xB0)};
		constexpr KindColours kShellColours{.Dark = Opaque(0x5F, 0xA9, 0xD6), .Light = Opaque(0x1F, 0x6F, 0x9E)};
		constexpr KindColours kProjectColours{.Dark = Opaque(0xA7, 0x7F, 0xE0), .Light = Opaque(0x6B, 0x3F, 0xB0)};
		constexpr KindColours kImageColours{.Dark = Opaque(0xD0, 0xA0, 0x60), .Light = Opaque(0x9A, 0x6A, 0x20)};
		constexpr std::optional<KindColours> kInTheDisabledTextColour = std::nullopt;

		struct KindLook
		{
			FileKind Kind;
			const char* Badge;
			const char* TabGlyph;
			std::optional<KindColours> Colours;
		};

		constexpr KindLook kKindLooks[] = {
			{FileKind::CppSource, "C++", ICON_LC_FILE_CODE, kCppSourceColours},
			{FileKind::CSource, "C", ICON_LC_FILE_CODE, kCppSourceColours},
			{FileKind::Header, "H", ICON_LC_FILE_CODE_2, kHeaderColours},
			{FileKind::CSharp, "C#", ICON_LC_HASH, kCSharpColours},
			{FileKind::Json, "{}", ICON_LC_FILE_JSON, kJsonColours},
			{FileKind::Markdown, "MD", ICON_LC_FILE_TEXT, kMarkdownColours},
			{FileKind::Lua, "LUA", ICON_LC_FILE_CODE, kLuaColours},
			{FileKind::PowerShell, "PS", ICON_LC_TERMINAL, kShellColours},
			{FileKind::Batch, "BAT", ICON_LC_TERMINAL, kShellColours},
			{FileKind::Solution, "SLN", ICON_LC_APP_WINDOW, kProjectColours},
			{FileKind::CppProject, "VCX", ICON_LC_APP_WINDOW, kProjectColours},
			{FileKind::CSharpProject, "CSP", ICON_LC_APP_WINDOW, kProjectColours},
			{FileKind::Image, "IMG", ICON_LC_FILE_IMAGE, kImageColours},
			{FileKind::VectorImage, "SVG", ICON_LC_FILE_IMAGE, kImageColours},
			{FileKind::Python, "PY", ICON_LC_FILE, kInTheDisabledTextColour},
			{FileKind::PlainText, "TXT", ICON_LC_FILE, kInTheDisabledTextColour},
			{FileKind::Ini, "INI", ICON_LC_FILE, kInTheDisabledTextColour},
			{FileKind::ResourceScript, "RC", ICON_LC_FILE, kInTheDisabledTextColour},
			{FileKind::Other, kIdenticalTo, ICON_LC_FILE, kInTheDisabledTextColour},
		};

		consteval bool EveryLookSitsAtItsOwnKind()
		{
			for (std::size_t lookIndex = 0; lookIndex < std::size(kKindLooks); ++lookIndex)
			{
				if (static_cast<std::size_t>(kKindLooks[lookIndex].Kind) != lookIndex)
				{
					return false;
				}
			}

			return std::size(kKindLooks) == static_cast<std::size_t>(FileKind::Other) + 1;
		}

		static_assert(EveryLookSitsAtItsOwnKind(), "kKindLooks must list every FileKind once, in FileKind order");

		const KindLook& LookOf(FileKind kind)
		{
			return kKindLooks[static_cast<std::size_t>(kind)];
		}

		ImU32 InTone(ImU32 colour, IconTone tone)
		{
			switch (tone)
			{
			case IconTone::Normal:
				return colour;
			case IconTone::Dimmed:
				return Core::WithAlphaScaled(colour, kDimmedIconAlpha);
			}

			return colour;
		}

		float LineMiddle(ImVec2 lineTop)
		{
			return lineTop.y + ImGui::GetFontSize() * kHalf;
		}

		ImFont* BadgeFace()
		{
			ImFont* const badgeFace = Core::ImGuiRenderer::GetBadgeFont();

			return badgeFace != nullptr ? badgeFace : ImGui::GetFont();
		}

		// WARNING: A face may hand back a bake of another size than the one asked for, which AddText scales to the
		// size it is given, so an advance read from the bake is scaled the same way.
		float AdvanceOf(ImFontBaked& baked, float size, unsigned int codepoint)
		{
			return baked.GetCharAdvance(static_cast<ImWchar>(codepoint)) * (size / baked.Size);
		}

		float BadgeTextWidth(ImFontBaked& baked, float size, std::string_view text, float letterSpacing)
		{
			float width = 0.0f;
			int characters = 0;
			const char* character = text.data();
			const char* const end = text.data() + text.size();

			while (character < end)
			{
				unsigned int codepoint = 0;
				character += ImTextCharFromUtf8(&codepoint, character, end);
				width += AdvanceOf(baked, size, codepoint);
				++characters;
			}

			return characters > 0 ? width + letterSpacing * static_cast<float>(characters - 1) : 0.0f;
		}

		// WARNING: ImGui has no letter spacing, so each character is drawn on its own and placed by hand. AddText
		// snaps the start of every run to a whole pixel, which keeps each tightened character sharp.
		void DrawBadge(const char* text, ImU32 colour, ImVec2 lineTop)
		{
			ImFont* const face = BadgeFace();
			const float size = Core::Scaled(Core::kBadgeFontSize);
			const float letterSpacing = Core::Scaled(kBadgeLetterSpacing);
			ImFontBaked* const baked = face->GetFontBaked(size);

			const std::string_view badge{text};
			const ImVec2 box = Core::Scaled(ImVec2(kBadgeWidth, kBadgeHeight));
			const float boxTop = LineMiddle(lineTop) - box.y * kHalf;
			const float textTop = boxTop + (box.y - size) * kHalf;
			float x = lineTop.x + (box.x - BadgeTextWidth(*baked, size, badge, letterSpacing)) * kHalf;

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const char* character = badge.data();
			const char* const end = badge.data() + badge.size();

			while (character < end)
			{
				unsigned int codepoint = 0;
				const char* const next = character + ImTextCharFromUtf8(&codepoint, character, end);
				drawList->AddText(face, size, ImVec2(x, textTop), colour, character, next);
				x += AdvanceOf(*baked, size, codepoint) + letterSpacing;
				character = next;
			}
		}

		void DrawGlyph(const char* glyph, ImU32 colour, ImVec2 lineTop)
		{
			const float size = Core::Scaled(kFolderIconSize);
			const ImVec2 glyphTop(lineTop.x, LineMiddle(lineTop) - size * kHalf);

			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, glyphTop, colour, glyph);
		}

		void DrawLoader(ImVec2 lineTop)
		{
			Core::DrawLoadingSpinner(ImVec2(lineTop.x + FileIconWidth() * kHalf, LineMiddle(lineTop)));
		}
	}

	FileKind KindOfFile(const std::filesystem::path& path)
	{
		const std::string extension = Core::LoweredExtension(path);

		for (const ExtensionKind& entry : kExtensionKinds)
		{
			if (entry.Extension == extension)
			{
				return entry.Kind;
			}
		}

		return FileKind::Other;
	}

	const char* BadgeOf(FileKind kind)
	{
		return LookOf(kind).Badge;
	}

	const char* GlyphOf(FileKind kind)
	{
		return LookOf(kind).TabGlyph;
	}

	const char* FolderGlyphOf(bool open)
	{
		return open ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER;
	}

	ImU32 ColourOf(FileKind kind, bool lightScheme)
	{
		const std::optional<KindColours>& colours = LookOf(kind).Colours;

		if (!colours.has_value())
		{
			return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		return lightScheme ? colours->Light : colours->Dark;
	}

	ImU32 DimFolderColour()
	{
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

		return ImGui::ColorConvertFloat4ToU32(ImLerp(text, disabled, kDimFolderShareOfDisabled));
	}

	bool IsLightScheme()
	{
		const ImVec4 windowBackground = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

		return Core::IsLightBackground(Core::Color{.R = windowBackground.x,
												   .G = windowBackground.y,
												   .B = windowBackground.z,
												   .A = windowBackground.w});
	}

	RowIcon FileIconOf(FileKind kind, IconTone tone)
	{
		return RowIcon{.Form = IconForm::Badge,
					   .Text = BadgeOf(kind),
					   .Colour = InTone(ColourOf(kind, IsLightScheme()), tone)};
	}

	RowIcon FolderIconOf(bool open, IconTone tone)
	{
		return RowIcon{.Form = IconForm::Glyph, .Text = FolderGlyphOf(open), .Colour = InTone(DimFolderColour(), tone)};
	}

	float FileIconWidth()
	{
		return Core::Scaled(kBadgeWidth);
	}

	float IconToNameOffset()
	{
		return FileIconWidth() + Core::Scaled(kIconToNameGap);
	}

	RowIcon LoaderIcon()
	{
		return RowIcon{.Form = IconForm::Loader};
	}

	void DrawRowIcon(const RowIcon& icon, ImVec2 at)
	{
		if (icon.Form == IconForm::Loader)
		{
			DrawLoader(at);
			return;
		}

		if (icon.Text == nullptr)
		{
			return;
		}

		const ImU32 colour = ImGui::GetColorU32(icon.Colour);

		switch (icon.Form)
		{
		case IconForm::Glyph:
			DrawGlyph(icon.Text, colour, at);
			return;
		case IconForm::Badge:
			DrawBadge(icon.Text, colour, at);
			return;
		case IconForm::Loader:
			return;
		}
	}

	void DrawFileIcon(FileKind kind, ImVec2 at, IconTone tone)
	{
		DrawRowIcon(FileIconOf(kind, tone), at);
	}

	void DrawFolderIcon(bool open, ImVec2 at, IconTone tone)
	{
		DrawRowIcon(FolderIconOf(open, tone), at);
	}
}
