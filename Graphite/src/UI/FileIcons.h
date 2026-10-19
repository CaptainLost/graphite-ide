#pragma once

#include <imgui.h>

#include <cstdint>
#include <filesystem>

namespace Graphite {

	enum class FileKind : std::uint8_t
	{
		CppSource,
		Header,
		CSharp,
		Json,
		Markdown,
		Lua,
		Shell,
		Project,
		Image,
		Other
	};

	enum class IconTone : std::uint8_t
	{
		Normal,
		Dimmed
	};

	struct RowIcon
	{
		const char* Glyph = nullptr;
		ImU32 Colour = 0;
	};

	inline constexpr float kIconToNameGap = 8.0f;

	FileKind KindOfFile(const std::filesystem::path& path);

	FileKind KindOfProject(const std::filesystem::path& project);

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
