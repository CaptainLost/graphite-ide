#pragma once

#include <imgui.h>

#include <cstdint>
#include <filesystem>

namespace Graphite {

	enum class FileKind : std::uint8_t
	{
		CppSource,
		CSource,
		Header,
		CSharp,
		Json,
		Markdown,
		Lua,
		PowerShell,
		Batch,
		Solution,
		CppProject,
		CSharpProject,
		Image,
		VectorImage,
		Python,
		PlainText,
		Ini,
		ResourceScript,
		Other
	};

	enum class IconTone : std::uint8_t
	{
		Normal,
		Dimmed
	};

	enum class IconForm : std::uint8_t
	{
		Glyph,
		Badge
	};

	struct RowIcon
	{
		IconForm Form = IconForm::Glyph;
		const char* Text = nullptr;
		ImU32 Colour = 0;
	};

	inline constexpr float kIconToNameGap = 5.0f;
	inline constexpr float kBadgeWidth = 17.0f;
	inline constexpr float kBadgeHeight = 15.0f;
	inline constexpr float kFolderIconSize = 16.0f;

	FileKind KindOfFile(const std::filesystem::path& path);

	const char* BadgeOf(FileKind kind);

	// WARNING: A tab's title is plain text before its "###", which cannot hold a badge's box, so this is a generic
	// Lucide glyph for the kind and not its badge. A tab that draws the badge itself must not prefix this as well.
	const char* GlyphOf(FileKind kind);

	const char* FolderGlyphOf(bool open);

	ImU32 ColourOf(FileKind kind, bool lightScheme);

	ImU32 DimFolderColour();

	bool IsLightScheme();

	RowIcon FileIconOf(FileKind kind, IconTone tone = IconTone::Normal);

	RowIcon FolderIconOf(bool open, IconTone tone = IconTone::Normal);

	float FileIconWidth();

	float IconToNameOffset();

	void DrawRowIcon(const RowIcon& icon, ImVec2 at);

	void DrawFileIcon(FileKind kind, ImVec2 at, IconTone tone = IconTone::Normal);

	void DrawFolderIcon(bool open, ImVec2 at, IconTone tone = IconTone::Normal);
}
