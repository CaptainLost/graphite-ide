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
		Badge,
		Loader
	};

	struct RowIcon
	{
		IconForm Form = IconForm::Glyph;
		const char* Text = nullptr;
		ImU32 Colour = 0;
	};

	inline constexpr float kIconToNameGap = 5.0f;
	inline constexpr float kBadgeWidth = 16.0f;
	inline constexpr float kBadgeHeight = 14.0f;
	inline constexpr float kFolderIconSize = 15.0f;

	FileKind KindOfFile(const std::filesystem::path& path);

	const char* BadgeOf(FileKind kind);

	// WARNING: A window's name is plain text before its "###", which cannot hold a badge's box, so this is a generic
	// Lucide glyph for the kind and not its badge. A file's tab draws the badge in its place (FileTabLook); the glyph
	// shows only where ImGui prints the name itself, as on a floating window's title bar.
	const char* GlyphOf(FileKind kind);

	const char* FolderGlyphOf(bool open);

	ImU32 ColourOf(FileKind kind, bool lightScheme);

	ImU32 DimFolderColour();

	bool IsLightScheme();

	RowIcon FileIconOf(FileKind kind, IconTone tone = IconTone::Normal);

	RowIcon FolderIconOf(bool open, IconTone tone = IconTone::Normal);

	// The loader's spinner in the icon's slot, for a row whose file is busy.
	RowIcon LoaderIcon();

	float FileIconWidth();

	float IconToNameOffset();

	void DrawRowIcon(const RowIcon& icon, ImVec2 at);

	void DrawFileIcon(FileKind kind, ImVec2 at, IconTone tone = IconTone::Normal);

	void DrawFolderIcon(bool open, ImVec2 at, IconTone tone = IconTone::Normal);
}
