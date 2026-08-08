#pragma once

#include <imgui.h>

#include <string_view>

namespace core
{
    /// Maps a file extension — with or without the leading dot, any case, e.g. ".cpp",
    /// "CPP", or "cpp" — to the Material Design Icons glyph to draw before the file's name.
    /// An extension this app doesn't recognize gets a generic file icon. Directories are
    /// not this function's job: a caller that already knows a node is a directory should
    /// use ICON_MDI_FOLDER / ICON_MDI_FOLDER_OPEN directly.
    [[nodiscard]] const char* IconForExtension(std::string_view extension);

    /// The codepoint ranges every icon this app can display falls into — everything
    /// IconForExtension can return, plus the small set of general UI icons used directly
    /// at call sites (folder, folder-open, refresh). Zero-terminated, as ImGui glyph
    /// ranges are. Passed to ImFontAtlas::AddFontFromFileTTF so only these glyphs, not
    /// MDI's several thousand, get baked into the font atlas.
    [[nodiscard]] const ImWchar* UsedGlyphRanges();
}
