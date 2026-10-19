#include "UI/FileIcons.h"

#include "Core/Base/Color.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui_internal.h>

#include <string>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kDimFolderShareOfDisabled = 0.45f;
		constexpr float kDimmedIconAlpha = 0.5f;

		struct ExtensionKind
		{
			std::string_view Extension;
			FileKind Kind;
		};

		constexpr ExtensionKind kExtensionKinds[] = {
			{".c", FileKind::CppSource},
			{".cc", FileKind::CppSource},
			{".cpp", FileKind::CppSource},
			{".cxx", FileKind::CppSource},
			{".h", FileKind::Header},
			{".hpp", FileKind::Header},
			{".hxx", FileKind::Header},
			{".cs", FileKind::CSharp},
			{".json", FileKind::Json},
			{".md", FileKind::Markdown},
			{".lua", FileKind::Lua},
			{".ps1", FileKind::Shell},
			{".psm1", FileKind::Shell},
			{".psd1", FileKind::Shell},
			{".bat", FileKind::Shell},
			{".cmd", FileKind::Shell},
			{".sln", FileKind::Project},
			{".slnx", FileKind::Project},
			{".vcxproj", FileKind::Project},
			{".csproj", FileKind::Project},
			{".png", FileKind::Image},
			{".jpg", FileKind::Image},
			{".jpeg", FileKind::Image},
			{".gif", FileKind::Image},
			{".bmp", FileKind::Image},
			{".ico", FileKind::Image},
			{".svg", FileKind::Image},
			{".webp", FileKind::Image},
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

		ImU32 SchemeColourOf(const KindColours& colours, bool lightScheme)
		{
			return lightScheme ? colours.Light : colours.Dark;
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

	const char* GlyphOf(FileKind kind)
	{
		switch (kind)
		{
		case FileKind::CppSource:
			return ICON_MDI_LANGUAGE_CPP;
		case FileKind::Header:
			return ICON_MDI_ALPHA_H_BOX_OUTLINE;
		case FileKind::CSharp:
			return ICON_MDI_LANGUAGE_CSHARP;
		case FileKind::Json:
			return ICON_MDI_CODE_JSON;
		case FileKind::Markdown:
			return ICON_MDI_LANGUAGE_MARKDOWN_OUTLINE;
		case FileKind::Lua:
			return ICON_MDI_LANGUAGE_LUA;
		case FileKind::Shell:
			return ICON_MDI_POWERSHELL;
		case FileKind::Project:
			return ICON_MDI_MICROSOFT_VISUAL_STUDIO;
		case FileKind::Image:
			return ICON_MDI_FILE_IMAGE_OUTLINE;
		case FileKind::Other:
			return ICON_MDI_FILE_OUTLINE;
		}

		return ICON_MDI_FILE_OUTLINE;
	}

	const char* FolderGlyphOf(bool open)
	{
		return open ? ICON_MDI_FOLDER_OPEN_OUTLINE : ICON_MDI_FOLDER_OUTLINE;
	}

	ImU32 ColourOf(FileKind kind, bool lightScheme)
	{
		switch (kind)
		{
		case FileKind::CppSource:
			return SchemeColourOf(kCppSourceColours, lightScheme);
		case FileKind::Header:
			return SchemeColourOf(kHeaderColours, lightScheme);
		case FileKind::CSharp:
			return SchemeColourOf(kCSharpColours, lightScheme);
		case FileKind::Json:
			return SchemeColourOf(kJsonColours, lightScheme);
		case FileKind::Markdown:
			return SchemeColourOf(kMarkdownColours, lightScheme);
		case FileKind::Lua:
			return SchemeColourOf(kLuaColours, lightScheme);
		case FileKind::Shell:
			return SchemeColourOf(kShellColours, lightScheme);
		case FileKind::Project:
			return SchemeColourOf(kProjectColours, lightScheme);
		case FileKind::Image:
			return SchemeColourOf(kImageColours, lightScheme);
		case FileKind::Other:
			return DimFolderColour();
		}

		return DimFolderColour();
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
		return RowIcon{.Glyph = GlyphOf(kind), .Colour = InTone(ColourOf(kind, IsLightScheme()), tone)};
	}

	RowIcon FolderIconOf(bool open, IconTone tone)
	{
		return RowIcon{.Glyph = FolderGlyphOf(open), .Colour = InTone(DimFolderColour(), tone)};
	}

	float FileIconWidth()
	{
		return ImGui::GetFontSize();
	}

	float IconToNameOffset()
	{
		return FileIconWidth() + Core::Scaled(kIconToNameGap);
	}

	void DrawRowIcon(const RowIcon& icon, ImVec2 at)
	{
		if (icon.Glyph == nullptr)
		{
			return;
		}

		ImGui::GetWindowDrawList()->AddText(at, ImGui::GetColorU32(icon.Colour), icon.Glyph);
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
